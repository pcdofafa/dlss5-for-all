/* =====================================================================
   pipe_up.cpp -- ver pipe_up.h. X-FG-6 (18/09/2026).

   A conexao e a copia enxuta do PipeIO que os dois modulos tinham (o
   mesmo overlapped, os mesmos prazos: 4 s para escrever, 60 s para ler o
   ack do Build). O que e novo e so o dono e o pedido.
   ===================================================================== */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>

#include "ponte_comum.h"   /* FeedHello/FeedHelloAck, kMagic, kVersao, ComoValor */
#include "pipe_up.h"

namespace Ponte { namespace PipeUp {
namespace {

LogFn L = nullptr;
std::mutex muIo;            /* o I/O de um quadro inteiro (tag + mensagens) */
std::mutex muDono;          /* dono, pedido, abertura/fechamento */
HANDLE pipe = nullptr;
DWORD  hostPid = 0;
std::atomic<int>  dono{ DONO_NENHUM };
std::atomic<int>  pedido{ DONO_NENHUM };   /* quem esta esperando o pipe */
std::atomic<bool> quebrado{ false };       /* uma escrita/leitura falhou: o app nao reconecta este pipe */
std::atomic<uint64_t> n{ 0 };
unsigned trocas = 0;

const char* Nome(int d) { return d == DONO_NGX ? "DLSS (ngx_upstream)" : d == DONO_SR ? "FSR/XeSS (sr_hook)" : "ninguem"; }

bool PipeIO(void* buf, DWORD len, bool escrever, DWORD timeoutMs)
{
    if (pipe == nullptr) return false;
    BYTE* p = static_cast<BYTE*>(buf);
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ev == nullptr) return false;
    bool ok = true;
    while (len > 0)
    {
        OVERLAPPED ov = {}; ov.hEvent = ev; ResetEvent(ev);
        const BOOL comecou = escrever ? WriteFile(pipe, p, len, nullptr, &ov) : ReadFile(pipe, p, len, nullptr, &ov);
        if (!comecou && GetLastError() != ERROR_IO_PENDING) { ok = false; break; }
        const DWORD r = WaitForSingleObject(ev, timeoutMs);
        if (r != WAIT_OBJECT_0) { CancelIoEx(pipe, &ov); DWORD m; GetOverlappedResult(pipe, &ov, &m, TRUE); ok = false; break; }
        DWORD moveu = 0;
        if (!GetOverlappedResult(pipe, &ov, &moveu, FALSE) || moveu == 0) { ok = false; break; }
        p += moveu; len -= moveu;
    }
    CloseHandle(ev);
    if (!ok) quebrado = true;
    return ok;
}

/* CreateFile + hello. Com muDono. */
bool Abrir(const wchar_t* pipeNome, char* motivo, size_t motivoN)
{
    wchar_t nome[160];
    if (pipeNome && pipeNome[0]) { wcsncpy(nome, pipeNome, 159); nome[159] = 0; }
    else _snwprintf(nome, 160, L"\\\\.\\pipe\\dlss5-feed.%lu.up", GetCurrentProcessId());
    HANDLE h = CreateFileW(nome, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { motivo[0] = 0; return false; }   /* o app nao serve o .up: sem motivo = tentar depois */
    pipe = h; quebrado = false;
    if (!GetNamedPipeServerProcessId(pipe, &hostPid)) { snprintf(motivo, motivoN, "GetNamedPipeServerProcessId falhou"); CloseHandle(pipe); pipe = nullptr; return false; }
    HANDLE hApp = OpenProcess(PROCESS_DUP_HANDLE, FALSE, hostPid);
    HANDLE selfNoApp = nullptr;
    if (hApp) { DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), hApp, &selfNoApp, PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0); CloseHandle(hApp); }
    if (selfNoApp == nullptr) { snprintf(motivo, motivoN, "self_process nao duplicado"); CloseHandle(pipe); pipe = nullptr; return false; }
    FeedHello hello = { kMagic, kVersao, GetCurrentProcessId(), CLIENTE_D3D11, ComoValor(selfNoApp) };
    FeedHelloAck ack = {};
    if (!PipeIO(&hello, sizeof hello, true, 4000) || !PipeIO(&ack, sizeof ack, false, 60000) || ack.magic != kMagic || ack.version != kVersao)
    { snprintf(motivo, motivoN, "handshake falhou"); CloseHandle(pipe); pipe = nullptr; quebrado = true; return false; }
    L("pipe-up: conectado em %ls (host pid=%lu)", nome, hostPid);
    return true;
}

} // namespace anonimo

void Iniciar(LogFn log) { L = log; }

bool Tomar(int quem, const wchar_t* pipeNome, bool* ocupado, char* motivo, size_t motivoN)
{
    if (ocupado) *ocupado = false;
    if (motivo && motivoN) motivo[0] = 0;
    std::lock_guard<std::mutex> g(muDono);
    const int d = dono.load();
    if (d == quem) return true;
    if (quebrado.load()) { if (motivo && motivoN) snprintf(motivo, motivoN, "o pipe .up ja fechou nesta sessao (o app nao reconecta)"); return false; }
    if (d != DONO_NENHUM)
    {
        /* o outro modulo esta com o pipe: registra o pedido; ele larga quando nao tiver contexto */
        if (pedido.exchange(quem) != quem && L) L("pipe-up: %s pediu o pipe .up; o dono e %s (larga quando o jogo parar de usar o upscaler dele)", Nome(quem), Nome(d));
        if (ocupado) *ocupado = true;
        return false;
    }
    if (pipe == nullptr)
    {
        if (quebrado.load()) { if (motivo && motivoN) snprintf(motivo, motivoN, "o pipe .up ja fechou nesta sessao (o app nao reconecta)"); return false; }
        char m[160] = "";
        if (!Abrir(pipeNome, m, sizeof m)) { if (motivo && motivoN && m[0]) snprintf(motivo, motivoN, "%s", m); return false; }
    }
    else if (quebrado.load()) { if (motivo && motivoN) snprintf(motivo, motivoN, "o pipe .up quebrou (escrita/leitura falhou)"); return false; }
    dono = quem;
    if (pedido.load() == quem) pedido = DONO_NENHUM;
    if (trocas++ > 0 && L) L("pipe-up: o pipe .up passou para %s (troca %u; n continua em %llu)", Nome(quem), trocas - 1, (unsigned long long)n.load());
    return true;
}

void Largar(int quem)
{
    std::lock_guard<std::mutex> g(muDono);
    if (dono.load() != quem) return;
    dono = DONO_NENHUM;
    if (L) L("pipe-up: %s largou o pipe .up (a conexao fica aberta%s)", Nome(quem), pedido.load() != DONO_NENHUM ? "; ha quem espere por ela" : "");
}

bool AlguemQuer(int meuDono)
{
    const int p = pedido.load();
    return p != DONO_NENHUM && p != meuDono && dono.load() == meuDono;
}

int  Dono()  { return dono.load(); }
bool Aberto() { return pipe != nullptr && !quebrado.load(); }

void Fechar()
{
    std::lock_guard<std::mutex> g(muDono);
    if (pipe != nullptr) { CloseHandle(pipe); pipe = nullptr; }
    dono = DONO_NENHUM; pedido = DONO_NENHUM;
}

std::mutex& Mutex() { return muIo; }
bool Escrever(const void* b, DWORD len) { return PipeIO(const_cast<void*>(b), len, true, 4000); }
bool Ler(void* b, DWORD len)            { return PipeIO(b, len, false, 60000); }
DWORD HostPid() { return hostPid; }

uint64_t ProximoN() { return ++n; }
uint64_t UltimoN()  { return n.load(); }

} } // namespace Ponte::PipeUp
