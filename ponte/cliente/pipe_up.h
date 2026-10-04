/* =====================================================================
   pipe_up.h -- X-FG-6 (18/09/2026): UM SO DONO para o pipe `.up`.

   O app serve o pipe \\.\pipe\dlss5-feed.<pid>.up com UMA instancia e a
   thread dele vive UMA conexao: quem conecta faz o hello uma vez e, dali
   em diante, manda Builds ('B') e quadros ('F'). Ate aqui cada modulo da
   DLL (ngx_upstream para o DLSS, sr_hook para FSR/XeSS) abria a SUA
   conexao: quem chegava primeiro segurava o pipe e o outro tentava a cada
   2 s sem nunca entrar (X-MODOS, "Novos problemas 1"). Trocar DLSS <-> FSR
   no menu do jogo com o Upstream ligado virava pausa e so voltava com a
   reabertura do app.

   Agora a CONEXAO e uma so e passa de mao: o modulo que serve o upscaler
   ativo e o DONO; quando o jogo troca de upscaler, o outro modulo pede
   (Quer) e o dono, sem contexto vivo ou sem quadro ha 1 s, LARGA (Largar)
   sem fechar o pipe; o pedinte toma (Tomar) e manda o seu Build no mesmo
   pipe - o app refaz o conjunto como ja fazia numa troca de resolucao.
   O contador `n` dos quadros e UM SO e nunca volta atras: as fences in/out
   do app sao as mesmas nas duas sessoes e um `n` menor que o ja completado
   soltaria o jogo antes de o app escrever o Output.

   Threads: Tomar/Largar/Quer/Fechar sao chamadas da thread da ponte
   (Servico dos dois modulos); Escrever/Ler com o Mutex() em maos, da
   thread do jogo (por quadro) ou da ponte (Build). Nada aqui derruba o
   jogo: falha de pipe e devolvida como false e quem chamou faz o Falhar
   de sempre.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <cstdint>
#include <mutex>

namespace Ponte { namespace PipeUp {

typedef void (*LogFn)(const char* fmt, ...);
enum Dono { DONO_NENHUM = 0, DONO_NGX = 1, DONO_SR = 2 };

void Iniciar(LogFn log);

/* Tenta ficar com o pipe para `dono`. Se a conexao ainda nao existe, abre
   (CreateFile + hello) - `pipeNome` vazio = o nome padrao do pid. Se ja
   existe e esta livre, toma. Se pertence ao OUTRO modulo, registra o
   pedido e devolve false com *ocupado = true (o outro ve em AlguemQuer e
   larga quando nao tem mais contexto). false com *ocupado = false = o app
   nao serve o pipe (ou o handshake falhou; `motivo` diz). */
bool Tomar(int dono, const wchar_t* pipeNome, bool* ocupado, char* motivo, size_t motivoN);
/* O dono larga sem fechar: o pipe fica aberto para o outro modulo. */
void Largar(int dono);
/* O outro modulo pediu o pipe (e o pedido ainda vale). */
bool AlguemQuer(int meuDono);
/* Quem e o dono agora (DONO_*). */
int  Dono();
/* A conexao esta aberta e sa. */
bool Aberto();
/* Fecha a conexao (encerramento da ponte). */
void Fechar();

/* I/O na conexao (o chamador segura Mutex() enquanto manda um quadro inteiro). */
std::mutex& Mutex();
bool Escrever(const void* b, DWORD n);
bool Ler(void* b, DWORD n);
DWORD HostPid();

/* O contador unico dos quadros das duas sessoes. */
uint64_t ProximoN();
uint64_t UltimoN();

} } // namespace Ponte::PipeUp
