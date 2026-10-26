// =====================================================================================
//  atualizacao.h - o lado do launcher que fala com a web: versao nova, publi remota,
//  contagem anonima de uso, relatorio do autor, o aviso de versao nova e (modo direto) a troca dos arquivos.
//
//  Usado SO pelo launcher (portable-launcher\launcher.cpp). O app (main.cpp) nao inclui
//  isto: quem troca arquivos e o launcher, porque e ele que esta na raiz do pacote e e o
//  unico que pode sair de cena enquanto os bins sao substituidos.
//
//  O CONTRATO COM O SERVIDOR (painel fafa-attribution, Cloudflare Workers + D1; um exemplo
//  equivalente fica em amd\servidor-exemplo\ so como documentacao). Uma chave liga tudo:
//  [atualizacao] url_base= no ofertas.ini da raiz. Vazia (padrao) = nenhuma requisicao.
//     GET  {base}/versao.json?v=&m=&l=&id=   manifesto de versao; o servidor conta a
//                                            abertura por este GET quando `id` vai junto
//     GET  {base}/ofertas.json?v=&m=&l=&id=  a publi da tela (titulo, mensagem, cupom...)
//     GET  {base}/evento?e=<nome>&id=&v=&m=&l=  um clique na tela (fire-and-forget, 2 s)
//     GET  {base}/resumo?token=...           o resumo de uso (SO na copia do autor)
//     POST {base}/publicar?token=...         quem publica e o publicar-*.ps1, nao o launcher
//  A VITRINE (coluna da esquerda da tela, vitrine.h) NAO usa o url_base: e a rota
//     https://grupo.pcdofafa.com.br/app-ofertas?l=&id=   (kUrlVitrinePadrao; JSON em
//     /app-ofertas.json), aberta pelo motor web, nao pelo WinHTTP daqui. [atualizacao]
//     Sempre ligada e sempre nesse endereco: vitrine= e vitrine_url= do ini NAO sao mais
//     lidas (so a falta de rede ou de WebView2 a esconde). "Tem rede?" vem do versao.json
//     (com url_base) ou de uma sonda de cabecalho na propria vitrine (sem url_base).
//  url_manifesto=, url_ofertas= e relatorio_url= sobrescrevem cada endereco se existirem.
//
//  O QUE VAI NA QUERY (e nada alem disto): v = versao do app, m = modulo escolhido na
//  ultima abertura (nvidia|amd|sem; omitido se nao souber), l = idioma (pt|en), id = um
//  GUID aleatorio gerado na primeira abertura ([atualizacao] id_instalacao= do
//  dlss5forall.ini da raiz), nunca derivado de hardware, nome, e-mail ou IP. Com
//  [atualizacao] contar=0 NENHUM desses parametros vai, nenhum evento e enviado, e as
//  buscas continuam funcionando. Nao ha nome de jogo, hardware, tempo de uso, nada.
//
//  AS BUSCAS nao atrasam a tela: cada uma roda numa thread propria, HTTPS apenas, sem
//  cookies, sem seguir para http, User-Agent "DLSS5ForAll/<versao>", 3 s no total. Se a
//  resposta chega depois de a janela abrir, a janela recebe WM_OFERTAS_ATUALIZA (ver
//  LauncherInfo em splash_ofertas.h). Sem rede = nada aparece; o motivo vai para o
//  launcher.log. O manifesto de versao respeita um intervalo de 6 h SO quando contar=0
//  (com contar=1 e justamente esse GET que conta a abertura, entao ele vai toda vez).
//
//  MANIFESTO (versao.json) - v2.1: DOIS MODOS, escolhidos pelo manifesto a cada lancamento.
//    campos: versao (obrigatorio), pagina, notas, notas_en, data, tamanho_bytes (opcionais) e,
//    SO para o modo direto, o zip: zip_url + zip_sha256 (os dois juntos).
//    MODO SITE (padrao, o mais simples): so `versao` (+ pagina/notas). A caixa "Nova versao
//      disponivel" abre a pagina de download no navegador ("pagina", so https de
//      pcdofafa.com.br; padrao kPaginaDownloadPadrao) e o programa INSTALADO segue abrindo.
//      Nada e baixado nem trocado pelo launcher.
//    MODO DIRETO: o manifesto traz tambem o zip. A caixa oferece "Atualizar agora" (baixa,
//      confere o SHA-256 e entrega a troca ao atualizar.cmd - ver APLICAR), "Abrir a pagina do
//      site" e "Agora nao". Se o download/hash/troca falhar, a mensagem oferece abrir a pagina.
//    {"versao":"2.2","pagina":"https://www.pcdofafa.com.br/dlss","notas":"texto curto","notas_en":"short text"}
//    {"versao":"2.2","zip_url":"https://.../DLSS5ForAll-2.2.zip","zip_sha256":"<64 hex>","notas":"..."}
//  COMPATIBILIDADE COM A 1.2 E A 2.0 (que ja tem a atualizacao direta e continuam a usa-la): o
//  manifesto no formato ANTIGO (`versao` + `url` + `sha256`) funciona para elas exatamente como
//  antes, e a 2.1+ o entende como modo direto. O parser da 1.2/2.0 so aceita manifesto com `url` +
//  `sha256` validos (senao "manifesto invalido", sem aviso) e ignora campo que nao conhece; entao:
//    direto para TODOS ......... versao + url + sha256            (o formato de sempre)
//    so aviso de site (2.1+) ... so `versao` [+ pagina, notas]     (1.2/2.0: invalido = nada)
//    direto so na 2.1+ ......... versao + zip_url + zip_sha256     (1.2/2.0: invalido = nada)
//    antigos direto, 2.1+ site . versao + url + sha256 + "modo":"site"  (2.1+ ignora o zip e abre a pagina)
//  `versao` compara por partes numericas ("1.10" > "1.9").
//  `minima_para_auto` (opcional) e a menor versao INSTALADA que pode atualizar sozinha (caixa
//  "automaticamente", so no modo direto); vazia = todas.
//
//  PUBLI (ofertas.json): todos os campos opcionais; os presentes sobrepoem o ofertas.ini
//    {"versao_conteudo":"2026-09-10a","titulo":"...","titulo_en":"...","mensagem":"...",
//     "mensagem_en":"...","cta":"...","cta_en":"...","botao":"...","botao_en":"...",
//     "url":"https://...","canal":"https://...","cupom":"PCDOFAFA10",
//     "cupom_texto":"10% no SSD X ate domingo","cupom_texto_en":"...","validade":"2026-09-14"}
//  O ultimo JSON valido fica em <raiz>\cache\ofertas.json: a tela abre com a ultima publi
//  mesmo sem rede. `validade` vencida (data local > validade) = cache ignorado, volta ao ini.
//  O worker (fafa-dlss5-app) devolve {} enquanto nada foi publicado: vale o ini.
//
//  AVISO DE VERSAO NOVA: nao e banner. Depois que a pessoa clica num botao de INICIAR, se a
//  verificacao ja respondeu que ha versao nova, aparece a caixa padrao do Windows
//  (TaskDialogIndirect, ver PerguntarAtualizar), UMA vez por abertura do launcher e sempre
//  que houver versao nova. Se a verificacao ainda nao respondeu, nao espera: o programa abre
//  sem pergunta. Fechou a tela sem escolher = sem pergunta. Seja qual for a resposta, o
//  programa instalado abre em seguida (a nao ser que a troca direta tenha sido aplicada: ai o
//  launcher sai e o atualizar.cmd o reabre). A pilula "atualizacao disponivel" da tela le o mesmo sinal.
//  Modo direto com a caixa "automaticamente" marcada (e versao instalada >= minima_para_auto)
//  nao pergunta: vai direto para a janela de progresso, que e o aviso curto na tela.
//
//  APLICAR (BaixarEAplicar, SO modo direto): baixa o zip para %TEMP%\DLSS5ForAll-atualizacao\ conferindo o
//  SHA-256 (BCrypt) enquanto grava; extrai com o tar.exe do Windows (sem PowerShell);
//  confere o versao.txt; mescla o ofertas.ini novo no da pessoa (so acrescenta chaves que
//  faltam); copia o launcher e os DLSS5ForAll.exe atuais para <raiz>\backup-versao-anterior\;
//  escreve e dispara o atualizar.cmd, que espera este launcher e os exes do pacote sairem
//  de uso, copia o preparo por cima com robocopy EXCETO o que e da pessoa
//  (kArquivosDaPessoa/kPastasDaPessoa, inclusive o mod AMD em bin\mod-amd; sem /PURGE nem /MIR:
//  nada e apagado), grava atualizacao.log, apaga o preparo e o zip, deixa o marcador
//  atualizacao-resultado.txt (ok/falhou) na raiz e reabre o launcher com
//  --atualizado=<versao anterior> e a MESMA escolha de modulo (--nvidia/--amd/--sem-dlss).
//  O launcher reaberto le o marcador e manda o evento atualizacao_ok/atualizacao_falhou.
//  Os caminhos vao para o .cmd por VARIAVEIS DE AMBIENTE (Unicode), nunca escritos no
//  arquivo. Se qualquer passo antes do .cmd falhar, nada foi tocado; se o robocopy falhar,
//  o .cmd devolve os exes do backup e mostra o caminho do log.
//
//  EXCECAO DE TESTE: um url_base (ou url_*) em http://127.0.0.1 ou localhost e aceito SEM
//  https e sem o intervalo de 6 h. Qualquer outro host exige https.
//  DLSS5_ATUALIZACAO_FORCAR=1 ignora so o intervalo de 6 h.
//
//  Comentarios sem acento, como o resto do projeto. Textos de tela via T()/TS().
// =====================================================================================
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstdio>
#include <cstdarg>
#include <cwctype>
#include <cstring>
#include <algorithm>
#include "idioma.h"
#include "versao.h"
#include "splash_ofertas.h"

namespace atualizacao {

// ----------------------------------------------------------------- constantes
constexpr DWORD  kIntervaloVerificacaoS = 6 * 60 * 60;   // 6 horas entre verificacoes (so com contar=0)
constexpr DWORD  kTimeoutBuscaMs        = 3000;          // total, do DNS ao ultimo byte
constexpr DWORD  kTimeoutEventoMs       = 2000;
constexpr size_t kJsonMax               = 64 * 1024;     // teto de qualquer JSON que baixamos
constexpr ULONGLONG kPacoteMax          = 2ull * 1024 * 1024 * 1024;   // 2 GB
// v2.1: a pagina de download do site (o modo "site" abre esta, salvo `pagina` valida no manifesto) e o
// evento de contagem do clique que a abre. O servidor so conhece os nomes da sua lista (resumo.js
// EVENTOS): enquanto `abriu_pagina_download` nao estiver la, o clique reaproveita `clique_atualizar`.
constexpr const wchar_t* kPaginaDownloadPadrao = L"https://www.pcdofafa.com.br/dlss";
constexpr const wchar_t* kEventoPaginaDownload = L"clique_atualizar";

// O que o atualizar.cmd NUNCA copia por cima (robocopy /XF): arquivos que a pessoa poe,
// edita ou que sao gerados na maquina dela. `*.ini` e `*.cfg` cobrem ReShade.ini,
// FafaDLSS5.ini, dlss5forall.ini, ofertas.ini, dlss5-feed.cfg, dlssnr_on_amd.ini e os
// presets; os .ini/.cfg que ainda NAO existem no pacote da pessoa entram na segunda passada.
//
// POR PADRAO, E NAO POR NOME EXATO. Esta lista dizia "renodx-dlss5.addon64", e a build em uso
// desde 02/09/2026 chama-se "renodx-dlss.addon64", sem o 5 - o add-on que a pessoa instalou
// pelo RHI ficava de fora da protecao. Hoje isso nao chega a apagar nada (o robocopy da
// passada 1 e /E, sem /MIR nem /PURGE: ele so copia o que existe na ORIGEM, e o pacote
// publico nao leva componente de terceiro nenhum), mas basta um pacote que os leve - o zip
// completo e privado leva - para a atualizacao sobrescrever o que a pessoa tem. O mesmo
// engano de um digito ja tinha deixado esse arquivo entrar no zip publico uma vez.
inline const wchar_t* kArquivosDaPessoa =
    L"*.ini *.cfg *.log *.log.* renodx-dlss*.addon64 nvngx_*.dll version.dll "
    L"dlssnr_on_amd* relatorio-uso.txt";
// pastas inteiras que ficam como estao (cache do ReShade, camada 2 gerada, o backup, o cache da publi)
// v2.1: `mod-amd` e o mod AMD (bin\mod-amd): robocopy /XD casa o NOME da pasta em qualquer nivel, entao
// ela nunca e tocada. Sem /PURGE nem /MIR em lugar nenhum, o atualizar.cmd tambem nunca APAGA nada.
inline const wchar_t* kPastasDaPessoa = L"reshade-cache bin-camada2 camada2 backup-versao-anterior cache mod-amd";

// ----------------------------------------------------------------- utilitarios
inline std::wstring Utf8ParaW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
inline std::string WParaUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
inline bool Existe(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline bool EhPasta(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool CriarPastas(const std::wstring& p) {
    const int r = SHCreateDirectoryExW(nullptr, p.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}
// apaga uma pasta com tudo dentro (so o que e nosso: preparo em %TEMP%)
inline void ApagarPasta(const std::wstring& p) {
    if (p.size() < 8 || !EhPasta(p)) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((p + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring c = p + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ApagarPasta(c);
            else { SetFileAttributesW(c.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(c.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(p.c_str());
}
inline std::wstring PastaTemp() {
    wchar_t t[MAX_PATH]{};
    GetTempPathW(MAX_PATH, t);
    std::wstring p = std::wstring(t);
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    p += L"DLSS5ForAll-atualizacao";
    CriarPastas(p);
    return p;
}
inline std::wstring HoraAgora() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t b[40];
    _snwprintf(b, 40, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}
inline std::wstring DataHoje() { return HoraAgora().substr(0, 10); }   // "YYYY-MM-DD" local
// uma linha, com hora, num log em UTF-8 (launcher.log ou atualizacao.log). Formatos wide
// com %ls: o fwprintf do MinGW segue C99 (%s e char*).
inline CRITICAL_SECTION& TrancaDoLog() {
    // as tres buscas rodam em threads e anotam ao mesmo tempo: sem isto as linhas se
    // misturam no launcher.log (aconteceu no teste)
    static CRITICAL_SECTION cs; static bool pronta = false;
    if (!pronta) { InitializeCriticalSection(&cs); pronta = true; }   // chamada primeiro pela thread principal, antes de haver threads
    return cs;
}
struct TrancaLog { TrancaLog() { EnterCriticalSection(&TrancaDoLog()); } ~TrancaLog() { LeaveCriticalSection(&TrancaDoLog()); } };
inline void Anotar(const std::wstring& arquivo, const wchar_t* fmt, ...) {
    wchar_t b[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(b, 2048, fmt, ap);
    va_end(ap);
    b[2047] = 0;
    TrancaLog tranca;
    FILE* f = _wfopen(arquivo.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"%ls [atualizacao] %ls\n", HoraAgora().c_str(), b);
    fclose(f);
}
inline std::wstring Escrever(const std::wstring& caminho, const std::string& bytes) {
    HANDLE h = CreateFileW(caminho.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"CreateFile " + std::to_wstring(GetLastError());
    DWORD w = 0;
    const BOOL ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &w, nullptr);
    CloseHandle(h);
    return (ok && w == bytes.size()) ? L"" : (L"WriteFile " + std::to_wstring(GetLastError()));
}
inline std::string LerBytes(const std::wstring& caminho, size_t max) {
    HANDLE h = CreateFileW(caminho.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || (size_t)sz.QuadPart > max) { CloseHandle(h); return ""; }
    std::string s((size_t)sz.QuadPart, '\0'); DWORD rd = 0;
    ReadFile(h, &s[0], (DWORD)s.size(), &rd, nullptr); CloseHandle(h); s.resize(rd);
    return s;
}
inline std::wstring LerTexto(const std::wstring& caminho) { return ofertas::detail::ReadTextFile(caminho); }
inline std::wstring Trim(const std::wstring& s) { return ofertas::detail::Trim(s); }
inline std::wstring Minusculas(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
inline std::wstring Megas(ULONGLONG bytes) {
    const double mb = (double)bytes / (1024.0 * 1024.0);
    wchar_t b[32];
    if (mb < 10) _snwprintf(b, 32, L"%.1f MB", mb); else _snwprintf(b, 32, L"%.0f MB", mb);
    return b;
}
// GUID v4 aleatorio (BCryptGenRandom), formato 8-4-4-4-12 minusculo. So acaso: nada de
// hardware, usuario ou rede entra aqui.
inline std::wstring GerarGuid() {
    unsigned char b[16]{};
    if (BCryptGenRandom(nullptr, b, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        ULONGLONG t = GetTickCount64() ^ ((ULONGLONG)GetCurrentProcessId() << 32);
        for (int i = 0; i < 16; i++) { t = t * 6364136223846793005ull + 1442695040888963407ull; b[i] = (unsigned char)(t >> 56); }
    }
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40); b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    wchar_t s[40];
    _snwprintf(s, 40, L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
               b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}
// percent-encoding do que vai na query (valores curtos e ASCII na pratica)
inline std::wstring UrlEnc(const std::wstring& w) {
    const std::string u = WParaUtf8(w); std::wstring o;
    for (unsigned char c : u) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (wchar_t)c;
        else { wchar_t t[4]; _snwprintf(t, 4, L"%%%02X", c); o += t; }
    }
    return o;
}

// ----------------------------------------------------------------- versao
// Compara por partes numericas separadas por ponto: -1 se a < b, 0 se iguais, 1 se a > b.
// "1.10" > "1.9"; "1.1" == "1.1.0"; o que nao e digito nem ponto encerra aquele lado.
inline int CompararVersao(const std::wstring& a, const std::wstring& b) {
    size_t ia = 0, ib = 0;
    auto parte = [](const std::wstring& s, size_t& i) -> unsigned long long {
        unsigned long long n = 0;
        while (i < s.size() && iswdigit(s[i])) n = n * 10 + (unsigned)(s[i++] - L'0');
        if (i < s.size() && s[i] == L'.') i++; else i = s.size();
        return n;
    };
    while (ia < a.size() || ib < b.size()) {
        const unsigned long long na = parte(a, ia), nb = parte(b, ib);
        if (na != nb) return na < nb ? -1 : 1;
    }
    return 0;
}
inline bool VersaoValida(const std::wstring& v) {
    if (v.empty() || v.size() > 32 || !iswdigit(v[0])) return false;
    for (wchar_t c : v) if (!iswdigit(c) && c != L'.') return false;
    return true;
}

// ----------------------------------------------------------------- JSON (arvore pequena)
// Um leitor de JSON do tamanho do problema: objetos, listas, strings (com escapes e \u,
// pares substitutos incluidos), numeros, true/false/null. Profundidade limitada. Nao e um
// parser geral, mas le tudo o que o nosso servidor devolve.
struct Json {
    enum Tipo { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json* Get(const char* k) const { for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
    std::wstring Str(const char* k) const {
        const Json* j = Get(k); if (!j) return L"";
        if (j->t == STR) return Utf8ParaW(j->s);
        if (j->t == NUM) {   // numero escrito sem aspas ("versao": 1.2): vira texto como estava
            char b2[64];
            if (j->n == (double)(long long)j->n) snprintf(b2, 64, "%lld", (long long)j->n); else snprintf(b2, 64, "%g", j->n);
            return Utf8ParaW(b2);
        }
        if (j->t == BOOL) return j->b ? L"1" : L"0";
        return L"";
    }
    long long Num(const char* k) const {
        const Json* j = Get(k); if (!j) return 0;
        if (j->t == NUM) return (long long)j->n;
        if (j->t == STR) return _atoi64(j->s.c_str());
        return 0;
    }
};
struct LeitorJson {
    const std::string& s; size_t i = 0;
    explicit LeitorJson(const std::string& src) : s(src) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; }
    static void Utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    unsigned Hex4() {
        if (i + 4 > s.size()) { i = s.size(); return 0; }
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            const char c = s[i++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        }
        return v;
    }
    bool String(std::string& o) {
        if (i >= s.size() || s[i] != '"') return false;
        i++; o.clear();
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) return false;
            const char e = s[i++];
            switch (e) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': case 'b': case 'f': o += ' '; break;
            case 'u': {
                unsigned cp = Hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    const size_t volta = i; i += 2;
                    const unsigned lo = Hex4();
                    if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else i = volta;
                }
                Utf8(o, cp);
                break;
            }
            default: o += e; break;   // \" \\ \/
            }
        }
        return false;
    }
    bool Valor(Json& j, int prof) {
        if (prof > 12) return false;
        ws();
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '"') { j.t = Json::STR; return String(j.s); }
        if (c == '{') {
            j.t = Json::OBJ; i++;
            for (;;) {
                ws(); if (i < s.size() && s[i] == '}') { i++; return true; }
                std::string k; if (!String(k)) return false;
                ws(); if (i >= s.size() || s[i] != ':') return false; i++;
                Json v; if (!Valor(v, prof + 1)) return false;
                if (j.o.size() < 256) j.o.emplace_back(k, std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return false;
            }
        }
        if (c == '[') {
            j.t = Json::ARR; i++;
            for (;;) {
                ws(); if (i < s.size() && s[i] == ']') { i++; return true; }
                Json v; if (!Valor(v, prof + 1)) return false;
                if (j.a.size() < 1024) j.a.push_back(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return false;
            }
        }
        if (s.compare(i, 4, "true") == 0)  { j.t = Json::BOOL; j.b = true;  i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { j.t = Json::BOOL; j.b = false; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0)  { j.t = Json::NUL; i += 4; return true; }
        // numero
        size_t k = i;
        while (k < s.size() && (isdigit((unsigned char)s[k]) || s[k] == '-' || s[k] == '+' || s[k] == '.' || s[k] == 'e' || s[k] == 'E')) k++;
        if (k == i) return false;
        j.t = Json::NUM; j.n = atof(s.substr(i, k - i).c_str()); i = k;
        return true;
    }
};
inline bool LerJson(const std::string& txt, Json& out) {
    LeitorJson r(txt);
    if (!r.Valor(out, 0)) return false;
    r.ws();
    return r.i == txt.size() || true;   // lixo depois do JSON nao invalida (o servidor e nosso)
}

// controle e quebras nao desenham: viram espaco; e ha um teto de tamanho por campo
inline std::wstring Limpo(std::wstring s, size_t max) {
    if (s.size() > max) s.resize(max);
    for (auto& c : s) if (c < L' ') c = L' ';
    return Trim(s);
}

// ----------------------------------------------------------------- manifesto
struct Manifesto {
    std::wstring versao, data, notas, notasEn, minimaParaAuto;
    // v2.1: a PAGINA DE DOWNLOAD do site (opcional no JSON; quem a valida e ChecarVersao, que
    // grava aqui a pagina aceita ou kPaginaDownloadPadrao) e o ZIP do modo direto (zip_url +
    // zip_sha256, ou `url` + `sha256` do manifesto antigo). Sem os dois, e o modo "site".
    std::wstring pagina, zipUrl, zipSha256;
    // O TAMANHO DO DOWNLOAD, em bytes (tamanho_bytes), so faz sentido no modo direto: a caixa o
    // mostra porque "quer baixar?" sem dizer quanto pesa e uma pergunta pela metade. Ausente ou
    // absurdo = a caixa simplesmente nao fala de tamanho.
    long long bytes = 0;
    bool Direto() const { return !zipUrl.empty() && !zipSha256.empty(); }
    const std::wstring& NotasNoIdioma() const { return (EmIngles() && !notasEn.empty()) ? notasEn : notas; }
    // "48,3 MB" / "980 KB" - o tamanho como a pessoa o le. Vazio quando nao ha numero.
    std::wstring TamanhoLegivel() const {
        if (bytes <= 0) return L"";
        wchar_t b[32]{};
        if (bytes >= 1024 * 1024) {
            const double mb = (double)bytes / (1024.0 * 1024.0);
            swprintf(b, 32, EmIngles() ? L"%.1f MB" : L"%.1f MB", mb);
            // virgula decimal em portugues, ponto em ingles
            if (!EmIngles()) for (wchar_t* p = b; *p; p++) if (*p == L'.') *p = L',';
        } else {
            swprintf(b, 32, L"%lld KB", bytes / 1024);
        }
        return b;
    }
};
// 64 hexadecimais (o resto e tratado como ausente)
inline bool Sha256Valido(const std::wstring& s) {
    if (s.size() != 64) return false;
    for (wchar_t c : s) if (!iswxdigit(c)) return false;
    return true;
}
// As notas vao para dentro do TaskDialog, que com hyperlinks ligados interpreta <a href>: sem < e >
// o texto de um arquivo editavel nao desenha link nenhum.
inline std::wstring SemMarcacao(std::wstring s) { for (auto& c : s) if (c == L'<' || c == L'>') c = L' '; return Trim(s); }
// `aviso` sai com o motivo quando o manifesto trouxe um zip incompleto ou com hash ruim (vira modo site).
inline bool LerManifesto(const std::string& json, Manifesto& m, std::wstring& erro, std::wstring* aviso = nullptr) {
    Json j;
    if (!LerJson(json, j) || j.t != Json::OBJ) { erro = L"JSON invalido"; return false; }
    m.versao = Limpo(j.Str("versao"), 32);
    m.data = Limpo(j.Str("data"), 32);
    m.pagina = Limpo(j.Str("pagina"), 2048);
    m.notas = SemMarcacao(Limpo(j.Str("notas"), 400));
    m.notasEn = SemMarcacao(Limpo(j.Str("notas_en"), 400));
    m.minimaParaAuto = Limpo(j.Str("minima_para_auto"), 32);
    {   // tamanho_bytes: opcional. Um valor fora de faixa (negativo, ou maior que 4 GB) e
        // tratado como ausente - melhor a caixa nao falar de tamanho do que anunciar um
        // numero que nao faz sentido.
        const long long t = j.Num("tamanho_bytes");
        if (t > 0 && t < 4LL * 1024 * 1024 * 1024) m.bytes = t;
    }
    if (!VersaoValida(m.versao)) { erro = L"versao ausente ou invalida"; return false; }
    // O ZIP (modo direto): zip_url + zip_sha256 e o nome novo; url + sha256 e o do manifesto da
    // 1.2 (continua valendo, para quem quer a 1.2 atualizando direto tambem). Um par pela metade
    // ou com hash que nao e hexadecimal NAO invalida o aviso: cai no modo site, com o motivo no log.
    m.zipUrl.clear(); m.zipSha256.clear();
    const std::wstring zu = Limpo(j.Str("zip_url"), 2048), zs = Minusculas(Limpo(j.Str("zip_sha256"), 64));
    const std::wstring au = Limpo(j.Str("url"), 2048),     as = Minusculas(Limpo(j.Str("sha256"), 64));
    if (!zu.empty() && Sha256Valido(zs))      { m.zipUrl = zu; m.zipSha256 = zs; }
    else if (!au.empty() && Sha256Valido(as)) { m.zipUrl = au; m.zipSha256 = as; }
    // "modo":"site" forca o modo site na 2.1+ mesmo com zip no manifesto (a 1.2/2.0 ignoram o campo e
    // seguem no direto com url+sha256): o aviso da 2.1+ vira "abra a pagina" e nada e baixado por ela.
    const bool modoSite = Minusculas(Limpo(j.Str("modo"), 16)) == L"site";
    if (modoSite) { m.zipUrl.clear(); m.zipSha256.clear(); }
    if (aviso && !modoSite && !m.Direto() && (!zu.empty() || !zs.empty() || !au.empty() || !as.empty()))
        *aviso = L"zip incompleto (precisa de zip_url + zip_sha256 com 64 hexadecimais) - fica o modo site";
    if (!m.minimaParaAuto.empty() && !VersaoValida(m.minimaParaAuto)) m.minimaParaAuto.clear();
    return true;
}

// ----------------------------------------------------------------- publi remota (ofertas.json)
// Escolhe o idioma, saneia (Sanitiza) e valida enderecos (UrlEhWeb). `validade` e
// `versaoConteudo` saem a parte (o cache decide por elas).
inline bool LerOfertas(const std::string& json, ofertas::detail::OfertaRemota& o, std::wstring& validade, std::wstring& versaoConteudo) {
    Json j;
    if (!LerJson(json, j) || j.t != Json::OBJ) return false;
    auto idioma = [&](const char* k, const char* kEn) {
        std::wstring pt = j.Str(k), en = j.Str(kEn);
        return (EmIngles() && !en.empty()) ? en : pt;
    };
    using ofertas::detail::Sanitiza;
    o.titulo     = Sanitiza(ofertas::detail::Unescape(idioma("titulo", "titulo_en")), true);
    o.mensagem   = Sanitiza(ofertas::detail::Unescape(idioma("mensagem", "mensagem_en")), true);
    o.cta        = Sanitiza(idioma("cta", "cta_en"), false);
    o.botao      = Sanitiza(idioma("botao", "botao_en"), false);
    o.cupom      = Limpo(Sanitiza(j.Str("cupom"), false), 32);
    o.cupomTexto = Sanitiza(idioma("cupom_texto", "cupom_texto_en"), false);
    // TIPO e PUBLICADO: os dois sao opcionais. `tipo` aceita a palavra ("oferta"/"cupom"/
    // "aviso") ou o numero; qualquer outra coisa deixa a deducao decidir (OfertaRemota::
    // TipoEfetivo). `publicado` aceita segundos unix; sem ele a faixa nao fala em tempo.
    {
        const std::wstring t = Limpo(j.Str("tipo"), 16);
        if      (t == L"oferta" || t == L"0") o.tipo = ofertas::detail::ITEM_OFERTA;
        else if (t == L"cupom"  || t == L"1") o.tipo = ofertas::detail::ITEM_CUPOM;
        else if (t == L"aviso"  || t == L"2") o.tipo = ofertas::detail::ITEM_AVISO;
        const long long p = j.Num("publicado");
        // um carimbo absurdo (antes de 2020 ou depois de 2100) e pior que nenhum
        if (p > 1577836800LL && p < 4102444800LL) o.publicado = p;
    }
    const std::wstring url = Limpo(j.Str("url"), 2048), canal = Limpo(j.Str("canal"), 2048);
    o.url = ofertas::UrlEhWeb(url) ? url : L"";
    o.canal = ofertas::UrlEhWeb(canal) ? canal : L"";
    if (!url.empty() && o.url.empty()) ofertas::LogUrlRecusada(L"ofertas.json (url)", url);
    if (!canal.empty() && o.canal.empty()) ofertas::LogUrlRecusada(L"ofertas.json (canal)", canal);
    validade = Limpo(j.Str("validade"), 10);
    versaoConteudo = Limpo(j.Str("versao_conteudo"), 32);
    return true;
}
// "YYYY-MM-DD" comparada como texto com a data local de hoje; sem validade = nao vence
inline bool ValidadeVencida(const std::wstring& validade) {
    return validade.size() == 10 && validade < DataHoje();
}

// ----------------------------------------------------------------- resumo de uso (autor)
// mapa chave -> numero. O worker devolve por_botao_* como numeros e por_versao/por_modulo
// como objetos {aberturas, pessoas}: nesse caso vale `pessoas` (gente, nao aberturas).
inline std::vector<std::pair<std::wstring, long long>> MapaDe(const Json* m) {
    std::vector<std::pair<std::wstring, long long>> r;
    if (!m || m->t != Json::OBJ) return r;
    for (auto& p : m->o) {
        long long n = 0;
        if (p.second.t == Json::NUM) n = (long long)p.second.n;
        else if (p.second.t == Json::OBJ) n = p.second.Get("pessoas") ? p.second.Num("pessoas") : p.second.Num("aberturas");
        r.emplace_back(Utf8ParaW(p.first), n);
    }
    return r;
}
inline bool LerResumo(const std::string& json, ofertas::detail::ResumoUso& r) {
    Json j;
    if (!LerJson(json, j) || j.t != Json::OBJ) return false;
    r.gerado = Limpo(j.Str("gerado"), 32);
    if (r.gerado.size() >= 16) r.gerado = r.gerado.substr(0, 10) + L" " + r.gerado.substr(11, 5);
    auto par = [&](const char* k, long long& ab, long long& us) { const Json* o = j.Get(k); if (o && o->t == Json::OBJ) { ab = o->Num("aberturas"); us = o->Num("usuarios"); } };
    par("hoje", r.hojeAb, r.hojeUs); par("dias7", r.d7Ab, r.d7Us); par("dias30", r.d30Ab, r.d30Us);
    r.porDia.clear(); r.porDiaAb.clear();
    if (const Json* pd = j.Get("por_dia")) if (pd->t == Json::ARR)
        for (auto& d : pd->a) if (d.t == Json::OBJ) { r.porDia.emplace_back(d.Str("data"), d.Num("usuarios")); r.porDiaAb.emplace_back(d.Str("data"), d.Num("aberturas")); }
    // do mais antigo ao mais novo, ate 14
    if (r.porDia.size() > 1 && r.porDia.front().first > r.porDia.back().first) { std::reverse(r.porDia.begin(), r.porDia.end()); std::reverse(r.porDiaAb.begin(), r.porDiaAb.end()); }
    while (r.porDia.size() > 14) { r.porDia.erase(r.porDia.begin()); r.porDiaAb.erase(r.porDiaAb.begin()); }
    r.porVersao = MapaDe(j.Get("por_versao"));
    r.porModulo = MapaDe(j.Get("por_modulo"));
    r.porBotaoHoje = MapaDe(j.Get("por_botao_hoje"));
    r.porBotao7 = MapaDe(j.Get("por_botao_7"));
    r.ok = true;
    return true;
}
// relatorio-uso.txt ao lado do launcher: uma linha por dia (data TAB aberturas TAB usuarios),
// atualizada (upsert) a cada resumo recebido. E o historico do autor, alem dos 14 dias da tela.
inline void GravarRelatorioUso(const std::wstring& raiz, const ofertas::detail::ResumoUso& r) {
    const std::wstring arq = raiz + L"relatorio-uso.txt";
    std::vector<std::pair<std::wstring, std::wstring>> linhas;   // data -> "ab\tus"
    {
        const std::wstring txt = LerTexto(arq); size_t pos = 0;
        while (pos < txt.size()) {
            size_t nl = txt.find(L'\n', pos);
            std::wstring l = Trim(txt.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
            pos = (nl == std::wstring::npos) ? txt.size() : nl + 1;
            if (l.size() < 10 || l[0] == L'#') continue;
            linhas.emplace_back(l.substr(0, 10), l.size() > 11 ? l.substr(11) : L"");
        }
    }
    for (size_t i = 0; i < r.porDia.size(); i++) {
        const std::wstring d = r.porDia[i].first;
        const std::wstring v = std::to_wstring(i < r.porDiaAb.size() ? r.porDiaAb[i].second : 0) + L"\t" + std::to_wstring(r.porDia[i].second);
        bool achou = false;
        for (auto& l : linhas) if (l.first == d) { l.second = v; achou = true; break; }
        if (!achou) linhas.emplace_back(d, v);
    }
    std::sort(linhas.begin(), linhas.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out = "\xEF\xBB\xBF# data\taberturas\tusuarios  (gerado pelo launcher a partir do resumo do servidor)\r\n";
    for (auto& l : linhas) out += WParaUtf8(l.first + L"\t" + l.second) + "\r\n";
    Escrever(arq, out);
}
inline std::wstring UltimoRegistroUso(const std::wstring& raiz) {
    const std::wstring txt = LerTexto(raiz + L"relatorio-uso.txt");
    std::wstring ultima; size_t pos = 0;
    while (pos < txt.size()) {
        size_t nl = txt.find(L'\n', pos);
        std::wstring l = Trim(txt.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
        pos = (nl == std::wstring::npos) ? txt.size() : nl + 1;
        if (l.size() >= 10 && l[0] != L'#') ultima = l;
    }
    if (ultima.empty()) return L"";
    // "2026-09-10\t123\t98" -> "2026-09-10 · 98 pessoas"
    size_t t1 = ultima.find(L'\t'), t2 = t1 == std::wstring::npos ? t1 : ultima.find(L'\t', t1 + 1);
    const std::wstring us = (t2 != std::wstring::npos) ? ultima.substr(t2 + 1) : L"";
    return ultima.substr(0, 10) + (us.empty() ? L"" : (L" · " + us + TS(L" pessoas", L" people")));
}

// ----------------------------------------------------------------- configuracao e estado
// Enderecos vem do ofertas.ini da RAIZ (editado a mao, nunca reescrito). O estado que o
// launcher grava (caixa automatica, carimbo, id, modulo) fica no dlss5forall.ini da RAIZ,
// ao lado do launcher - e um .ini, entao o atualizar.cmd nunca o sobrescreve.
inline std::wstring ArquivoEstado(const std::wstring& raiz) { return raiz + L"dlss5forall.ini"; }
inline std::wstring ArquivoLogLauncher(const std::wstring& raiz) { return raiz + L"launcher.log"; }
inline std::wstring PastaCache(const std::wstring& raiz) { return raiz + L"cache"; }
inline std::wstring ArquivoCacheOfertas(const std::wstring& raiz) { return raiz + L"cache\\ofertas.json"; }

inline std::wstring LerChaveIni(const std::wstring& arq, const wchar_t* sec, const wchar_t* k, const wchar_t* padrao = L"") {
    wchar_t b[2048]{};
    GetPrivateProfileStringW(sec, k, padrao, b, 2048, arq.c_str());
    return Trim(b);
}
inline void GravarEstado(const std::wstring& raiz, const wchar_t* k, const std::wstring& v) {
    WritePrivateProfileStringW(L"atualizacao", k, v.c_str(), ArquivoEstado(raiz).c_str());
}
inline std::wstring LerEstado(const std::wstring& raiz, const wchar_t* k, const wchar_t* padrao = L"") {
    return LerChaveIni(ArquivoEstado(raiz), L"atualizacao", k, padrao);
}
// A VITRINE mora na landing do grupo, em OUTRO dominio que o do url_base (que continua so
// para versao.json / ofertas.json / evento): e uma rota fixa, com o JSON dela ao lado
// (/app-ofertas.json). O endereco e SEMPRE este: vitrine_url= no ini nao e mais lida.
constexpr const wchar_t* kUrlVitrinePadrao = L"https://grupo.pcdofafa.com.br/app-ofertas";
struct Config {
    std::wstring urlBase, urlManifesto, urlOfertas, urlEvento, urlResumo;
    bool contar = true;
    // a vitrine (coluna da esquerda da tela): sempre ligada e sempre em kUrlVitrinePadrao
    // (independente de url_base; vitrine= e vitrine_url= do ini nao sao lidas). urlVitrine
    // sai daqui SEM a query (l=, id=): ver UrlVitrine().
    bool vitrine = true;
    std::wstring urlVitrine;
    // A GAVETA DA VITRINE vem aberta no PRIMEIRO uso e, depois, a cada N aberturas; nas
    // demais vale o que a pessoa deixou (ver [atualizacao] gaveta= / aberturas= no
    // dlss5forall.ini da raiz). vitrine_lembrar_a_cada=0 desliga a batida: a gaveta so abre
    // quando a pessoa mandar.
    int vitrineLembrar = 10;
    // O JSON que a propria vitrine publica ao lado da pagina (/app-ofertas.json): dele sai
    // so o CONTADOR da aba ("3 novas"). Vazio = o endereco e derivado da pagina; se nao
    // existir ou nao trouxer numero, a aba diz so "Ofertas" e nada quebra.
    std::wstring urlVitrineJson;
    std::wstring urlNovidades;                    // [ofertas] url_novidades: o "o que mudou"
};
inline std::wstring SemBarraFinal(std::wstring s) { while (!s.empty() && s.back() == L'/') s.pop_back(); return s; }
inline bool Desligado(const std::wstring& v) {
    return v == L"0" || _wcsicmp(v.c_str(), L"nao") == 0 || _wcsicmp(v.c_str(), L"não") == 0 || _wcsicmp(v.c_str(), L"false") == 0;
}
// A vitrine e uma pagina aberta por um motor web, nao pelo WinHTTP daqui - mas a mesma
// regra vale: https, ou http://127.0.0.1 / localhost para teste. file:/// tambem, SO para
// teste (uma pagina local); nada mais passa (mesmo motivo do UrlEhWeb: o ini e editavel).
// Aqui so a peneira de caracteres e de esquema; o host e conferido por QuebrarUrl em
// IniciarVerificacao (que ja faz isso com os outros enderecos).
inline bool UrlVitrineEhLocal(const std::wstring& u) { return _wcsnicmp(u.c_str(), L"file:///", 8) == 0; }
inline bool UrlVitrineAceita(const std::wstring& u) {
    if (u.size() > 2048) return false;
    for (wchar_t c : u) if (c <= L' ' || c == 0x7F || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L'^') return false;
    if (UrlVitrineEhLocal(u)) return u.size() > 8;
    return ofertas::UrlEhWeb(u);
}
inline Config LerConfig(const std::wstring& raiz) {
    const std::wstring ini = raiz + L"ofertas.ini";
    Config c;
    c.urlBase = SemBarraFinal(LerChaveIni(ini, L"atualizacao", L"url_base"));
    c.urlManifesto = LerChaveIni(ini, L"atualizacao", L"url_manifesto");
    c.urlOfertas   = LerChaveIni(ini, L"atualizacao", L"url_ofertas");
    c.urlResumo    = LerChaveIni(ini, L"atualizacao", L"relatorio_url");
    const std::wstring token = LerChaveIni(ini, L"atualizacao", L"relatorio_token");
    const std::wstring contar = LerChaveIni(ini, L"atualizacao", L"contar", L"1");
    c.contar = !Desligado(contar);
    // vitrine= e vitrine_url= do ini NAO sao lidas: a vitrine fica ligada (c.vitrine = true)
    // e o endereco e sempre o embutido.
    c.urlVitrine = kUrlVitrinePadrao;   // a vitrine nao depende de url_base: e uma rota da landing
    {
        const std::wstring n = LerChaveIni(ini, L"atualizacao", L"vitrine_lembrar_a_cada", L"10");
        const int v = _wtoi(n.c_str());
        c.vitrineLembrar = (v >= 0 && v <= 1000) ? v : 10;   // valor absurdo: fica o padrao
    }
    // "o que mudou": o mesmo endereco que o launcher mostra na tela ([ofertas] url_novidades).
    // A caixa de atualizacao o oferece no rodape, porque as notas de versao sairam de dentro
    // dela - quem quer ler a lista clica; quem so quer atualizar nao precisa passar por ela.
    c.urlNovidades = LerChaveIni(ini, L"ofertas", L"url_novidades");
    if (!c.urlNovidades.empty() && !ofertas::UrlEhWeb(c.urlNovidades)) c.urlNovidades.clear();
    c.urlVitrineJson = LerChaveIni(ini, L"atualizacao", L"vitrine_json");
    if (!c.urlVitrineJson.empty() && !UrlVitrineAceita(c.urlVitrineJson)) {
        Anotar(ArquivoLogLauncher(raiz), L"vitrine_json recusado (so https, 127.0.0.1 ou file:/// de teste) - fica o derivado da pagina");
        c.urlVitrineJson.clear();
    }
    if (c.urlVitrineJson.empty()) {
        // /app-ofertas -> /app-ofertas.json (a query da pagina nao vai junto: aqui nao se conta nada)
        std::wstring b = c.urlVitrine;
        const size_t q = b.find(L'?');
        if (q != std::wstring::npos) b = b.substr(0, q);
        while (!b.empty() && b.back() == L'/') b.pop_back();
        if (!b.empty()) c.urlVitrineJson = b + L".json";
    }
    if (!c.urlBase.empty()) {
        if (c.urlManifesto.empty()) c.urlManifesto = c.urlBase + L"/versao.json";
        if (c.urlOfertas.empty())   c.urlOfertas   = c.urlBase + L"/ofertas.json";
        c.urlEvento = c.urlBase + L"/evento";
        if (c.urlResumo.empty() && !token.empty()) c.urlResumo = c.urlBase + L"/resumo?token=" + UrlEnc(token);
    }
    if (!c.urlResumo.empty() && !token.empty() && c.urlResumo.find(L"token=") == std::wstring::npos)
        c.urlResumo += (c.urlResumo.find(L'?') == std::wstring::npos ? L"?" : L"&") + std::wstring(L"token=") + UrlEnc(token);
    return c;
}
// Quem somos para o servidor: so o que o contrato permite. Vazio quando contar=0.
struct Identidade {
    std::wstring id, v, m, l; bool contar = true;
    std::wstring Query() const {
        if (!contar) return L"";
        std::wstring q = L"v=" + UrlEnc(v) + (m.empty() ? L"" : (L"&m=" + UrlEnc(m))) + L"&l=" + UrlEnc(l) + L"&id=" + UrlEnc(id);
        return q;
    }
};
inline std::wstring ComQuery(const std::wstring& url, const std::wstring& q) {
    if (q.empty()) return url;
    return url + (url.find(L'?') == std::wstring::npos ? L"?" : L"&") + q;
}
inline Identidade LerIdentidade(const std::wstring& raiz, bool contar) {
    Identidade i; i.contar = contar; i.v = DLSS5_VERSAO; i.l = CodigoLingua(g_lingua);
    i.m = LerEstado(raiz, L"modulo");
    if (i.m != L"nvidia" && i.m != L"amd" && i.m != L"sem") i.m.clear();
    if (!contar) return i;
    i.id = LerEstado(raiz, L"id_instalacao");
    if (i.id.size() != 36) { i.id = GerarGuid(); GravarEstado(raiz, L"id_instalacao", i.id); }
    return i;
}
inline void SalvarModulo(const std::wstring& raiz, const wchar_t* m) { GravarEstado(raiz, L"modulo", m); }
inline bool LerAutomatica(const std::wstring& raiz) { return LerEstado(raiz, L"automatica", L"0") == L"1"; }
inline void SalvarAutomatica(const std::wstring& raiz, bool v) { GravarEstado(raiz, L"automatica", v ? L"1" : L"0"); }
inline ULONGLONG AgoraUnix() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart - 116444736000000000ull) / 10000000ull;
}
inline void CarimbarVerificacao(const std::wstring& raiz) { GravarEstado(raiz, L"ultima_verificacao", std::to_wstring(AgoraUnix())); }
inline ULONGLONG UltimaVerificacao(const std::wstring& raiz) { return _wcstoui64(LerEstado(raiz, L"ultima_verificacao", L"0").c_str(), nullptr, 10); }
// A CAIXA DA ATUALIZACAO QUE FALHOU (07/10/2026). O atualizar.cmd, ao falhar, deixa o marcador "falhou" e reabre o
// launcher; e o launcher que avisa a pessoa, com MessageBoxW. Ate a 2.1 o proprio .cmd chamava o PowerShell
// (System.Windows.Forms.MessageBox) so para mostrar esta caixa.
inline void AvisarAtualizacaoFalhou() {
    const std::wstring log = PastaTemp() + L"\\atualizacao.log";
    MessageBoxW(nullptr,
                (TS(L"A atualização não foi concluída. Os arquivos do programa foram devolvidos como estavam. Veja o registro em:",
                    L"The update did not complete. The program files were restored as they were. See the log at:") + L"\n\n" + log).c_str(),
                L"DLSS5 For All", MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}
// o marcador que o atualizar.cmd deixa na raiz ("ok" ou "falhou"); ler apaga
inline std::wstring LerResultadoAnterior(const std::wstring& raiz) {
    const std::wstring arq = raiz + L"atualizacao-resultado.txt";
    if (!Existe(arq)) return L"";
    const std::wstring r = Minusculas(Trim(LerTexto(arq)).substr(0, 6));
    DeleteFileW(arq.c_str());
    return r.rfind(L"ok", 0) == 0 ? L"ok" : L"falhou";
}

// ----------------------------------------------------------------- URL
struct Url { std::wstring host, caminho; INTERNET_PORT porta = 0; bool https = false, local = false; };
// So https - com a excecao de teste 127.0.0.1/localhost em http. Nada mais passa.
inline bool QuebrarUrl(const std::wstring& u, Url& o) {
    if (u.size() > 2048) return false;
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{}, extra[2048]{};
    uc.lpszHostName = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(u.c_str(), 0, 0, &uc)) return false;
    o.host = std::wstring(host, uc.dwHostNameLength);
    o.caminho = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    if (o.caminho.empty()) o.caminho = L"/";
    o.porta = uc.nPort;
    o.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring h = Minusculas(o.host);
    o.local = (h == L"127.0.0.1" || h == L"localhost");
    if (o.host.empty()) return false;
    if (!o.https && !o.local) return false;
    if (!o.https && uc.nScheme != INTERNET_SCHEME_HTTP) return false;
    return true;
}
inline bool UrlDeTeste(const std::wstring& u) { Url x; return QuebrarUrl(u, x) && x.local; }
// v2.1: a PAGINA DE DOWNLOAD que o manifesto pode indicar. So https, so o dominio do projeto
// (pcdofafa.com.br e subdominios): o manifesto vem de um endereco que o ini deixa editar, e um
// botao "baixar a nova versao" nao pode levar a lugar nenhum que o dono nao controla. QuebrarUrl
// separa usuario@host, entao "https://pcdofafa.com.br@outro.com/" tem host outro.com e e recusada.
inline bool PaginaDoSiteValida(const std::wstring& u) {
    if (u.empty() || u.size() > 2048) return false;
    for (wchar_t c : u) if (c <= L' ' || c == 0x7F || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L'^' || c == L'\\') return false;
    Url x;
    if (!QuebrarUrl(u, x) || !x.https || x.local) return false;
    const std::wstring h = Minusculas(x.host), d = L"pcdofafa.com.br";
    return h == d || (h.size() > d.size() + 1 && h.compare(h.size() - d.size() - 1, std::wstring::npos, L"." + d) == 0);
}

// ----------------------------------------------------------------- HTTP (WinHTTP)
// Um GET. `corpo(dados, n)` recebe cada pedaco e devolve false para abortar (cancelamento).
// `deadlineMs` (0 = sem) e o teto absoluto em GetTickCount64 - e o "3 s no total" das
// buscas. `total` recebe o Content-Length (0 se o servidor nao disser).
struct Timeouts { int resolver, conectar, enviar, receber; };
inline bool Get(const std::wstring& url, const Timeouts& to, ULONGLONG deadlineMs,
                std::atomic<bool>* cancelar, ULONGLONG* total,
                const std::function<bool(const BYTE*, DWORD)>& corpo, std::wstring& erro) {
    Url u;
    if (!QuebrarUrl(url, u)) { erro = TS(L"endereço recusado (só https)", L"address refused (https only)"); return false; }
    if (total) *total = 0;
    const std::wstring ua = std::wstring(L"DLSS5ForAll/") + DLSS5_VERSAO;
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { erro = L"WinHttpOpen " + std::to_wstring(GetLastError()); return false; }
    WinHttpSetTimeouts(s, to.resolver, to.conectar, to.enviar, to.receber);
    DWORD pol = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &pol, sizeof(pol));
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, u.host.c_str(), u.porta, 0);
    HINTERNET r = nullptr;
    do {
        if (!c) { erro = L"WinHttpConnect " + std::to_wstring(GetLastError()); break; }
        r = WinHttpOpenRequest(c, L"GET", u.caminho.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!r) { erro = L"WinHttpOpenRequest " + std::to_wstring(GetLastError()); break; }
        DWORD semCookies = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &semCookies, sizeof(semCookies));
        if (!WinHttpSendRequest(r, L"Cache-Control: no-cache\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(r, nullptr)) {
            const DWORD e = GetLastError();
            erro = (e == ERROR_WINHTTP_TIMEOUT || e == ERROR_WINHTTP_CANNOT_CONNECT || e == ERROR_WINHTTP_NAME_NOT_RESOLVED)
                 ? TS(L"sem conexão com o servidor", L"no connection to the server") + L" (WinHTTP " + std::to_wstring(e) + L")"
                 : (e == ERROR_WINHTTP_SECURE_FAILURE ? TS(L"certificado do servidor recusado", L"server certificate refused") + L" (WinHTTP " + std::to_wstring(e) + L")"
                                                      : L"WinHTTP " + std::to_wstring(e));
            break;
        }
        DWORD status = 0, n = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX);
        if (status / 100 != 2) { erro = L"HTTP " + std::to_wstring(status); break; }   // 200 (JSON) ou 204 (/evento)
        wchar_t cl[32]{}; n = sizeof(cl);
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &n, WINHTTP_NO_HEADER_INDEX) && total)
            *total = _wcstoui64(cl, nullptr, 10);
        std::vector<BYTE> buf(64 * 1024);
        ok = true;
        for (;;) {
            if (cancelar && cancelar->load()) { ok = false; erro = L"cancelado"; break; }
            if (deadlineMs && GetTickCount64() > deadlineMs) { ok = false; erro = TS(L"tempo esgotado", L"timed out"); break; }
            DWORD disp = 0;
            if (!WinHttpQueryDataAvailable(r, &disp)) { ok = false; erro = L"WinHttpQueryDataAvailable " + std::to_wstring(GetLastError()); break; }
            if (disp == 0) break;
            if (disp > buf.size()) disp = (DWORD)buf.size();
            DWORD lido = 0;
            if (!WinHttpReadData(r, buf.data(), disp, &lido)) { ok = false; erro = L"WinHttpReadData " + std::to_wstring(GetLastError()); break; }
            if (lido == 0) break;
            if (!corpo(buf.data(), lido)) { ok = false; if (erro.empty()) erro = L"cancelado"; break; }
        }
    } while (false);
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
// GET curto que devolve o corpo em memoria (JSONs): 3 s no total, teto kJsonMax
inline bool GetTexto(const std::wstring& url, std::string& corpo, std::wstring& erro, DWORD totalMs = kTimeoutBuscaMs) {
    corpo.clear();
    const ULONGLONG t0 = GetTickCount64();
    const int t = (int)(totalMs / 2);
    return Get(url, Timeouts{ t, t, t, t }, t0 + totalMs, nullptr, nullptr,
        [&](const BYTE* d, DWORD n) { if (corpo.size() + n > kJsonMax) return false; corpo.append((const char*)d, n); return true; }, erro);
}

// ----------------------------------------------------------------- SHA-256 (BCrypt)
struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr; std::vector<UCHAR> obj; bool ok = false;
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return;
        DWORD len = 0, cb = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&len, sizeof(len), &cb, 0) < 0) return;
        obj.resize(len);
        ok = BCryptCreateHash(alg, &h, obj.data(), len, nullptr, 0, 0) >= 0;
    }
    ~Sha256() { if (h) BCryptDestroyHash(h); if (alg) BCryptCloseAlgorithmProvider(alg, 0); }
    void Dados(const BYTE* p, DWORD n) { if (ok) BCryptHashData(h, (PUCHAR)p, n, 0); }
    std::wstring Hex() {
        UCHAR d[32]{};
        if (!ok || BCryptFinishHash(h, d, 32, 0) < 0) return L"";
        std::wstring s;
        for (UCHAR b : d) { wchar_t t[4]; _snwprintf(t, 4, L"%02x", b); s += t; }
        return s;
    }
};
inline std::wstring Sha256DoArquivo(const std::wstring& caminho) {
    HANDLE f = CreateFileW(caminho.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    Sha256 sh; std::vector<BYTE> b(1 << 20); DWORD n = 0;
    while (ReadFile(f, b.data(), (DWORD)b.size(), &n, nullptr) && n) sh.Dados(b.data(), n);
    CloseHandle(f);
    return sh.Hex();
}

// ----------------------------------------------------------------- as buscas (threads)
struct Verificacao {
    // versao nova
    std::atomic<int> pronto{ 2 };                 // 0 verificando, 1 ha versao nova, 2 nada
    Manifesto manifesto;
    bool automatica = false, podeAuto = false;
    // publi remota
    std::atomic<int> remotoPronto{ 2 };           // 0 buscando, 1 conteudo fresco em `remoto`, 2 nada
    ofertas::detail::OfertaRemota remoto, remotoCache;
    // A FAIXA QUE SE ATUALIZA SOZINHA. A ThreadOfertas nao busca uma vez e morre: ela repete a
    // busca a cada `pollMs` enquanto a janela viver, e SO incrementa `geracaoRemoto` quando o
    // conteudo e diferente do que ja esta na tela (comparado pelo SHA-256 do JSON) - repintar
    // uma faixa igual seria um piscar sem motivo. A tela le a geracao e anima a troca.
    std::atomic<int> geracaoRemoto{ 0 };
    // 1 = pode buscar; 0 = a faixa esta oculta, ou a tela esta em ingles (onde nao ha ofertas):
    // ninguem deve pagar trafego por uma faixa que nao esta na tela.
    std::atomic<int> pollLigado{ 1 };
    // sinalizado em EncerrarVerificacao: a thread acorda na hora em vez de esperar o intervalo
    HANDLE pararPoll = nullptr;
    int pollMs = 60000;                           // 60 s; o teste usa um valor menor
    // De quanto em quanto tempo o versao.json e reconsultado com a janela ABERTA. 30 min e um
    // meio-termo: o launcher costuma ficar aberto por segundos, e quem o deixa aberto a tarde
    // toda merece saber que saiu versao nova sem ter de fechar e abrir.
    int versaoMs = 30 * 60 * 1000;
    // resumo do autor
    bool cartao = false;
    std::atomic<int> resumoPronto{ 2 };           // 0 buscando, 1 ok, 2 indisponivel
    ofertas::detail::ResumoUso resumo;
    // a rede respondeu? E o sinal que a VITRINE espera (ate 1,5 s) antes de a tela abrir com
    // duas colunas: sem resposta nao vale abrir uma coluna que vai ficar vazia. Vem do GET
    // de versao.json (ThreadManifesto) - ou da SONDA (ThreadSonda em urlSonda): quando o
    // intervalo de 6 h segura esse GET, ou quando nao ha url_base e a sonda bate na propria
    // vitrine (so cabecalho; o corpo e descartado no primeiro pedaco).
    std::atomic<int> rede{ 0 };                   // 0 ainda nao respondeu, 1 respondeu, 2 falhou
    std::wstring urlSonda;
    // comum
    Config cfg; Identidade ident;
    std::wstring raiz;
    bool teste = false;                           // url local: sem https, sem intervalo de 6 h
    HANDLE threads[4]{};
    std::vector<HANDLE> eventos;                  // threads dos eventos (esperadas na saida)
};
// O endereco final da vitrine: {vitrine_url}?l=<pt|en>[&id=<id_instalacao>] (o id so com
// contar=1, como nas outras buscas). Vazio so se o endereco foi recusado e nao ha padrao.
inline std::wstring UrlVitrine(const Verificacao& v) {
    if (v.cfg.urlVitrine.empty()) return L"";
    std::wstring q = L"l=" + UrlEnc(v.ident.l);
    if (v.cfg.contar && !v.ident.id.empty()) q += L"&id=" + UrlEnc(v.ident.id);
    return ComQuery(v.cfg.urlVitrine, q);
}
// "Tem rede ate ali?": um GET que para no primeiro pedaco do corpo (ou num 204). Serve para
// a sonda da vitrine; nunca le a pagina inteira.
inline bool Sondar(const std::wstring& url, std::wstring& erro, DWORD totalMs = 2000) {
    bool chegou = false;
    const ULONGLONG t0 = GetTickCount64();
    const int t = (int)totalMs;
    const bool ok = Get(url, Timeouts{ t, t, t, t }, t0 + totalMs, nullptr, nullptr,
                        [&](const BYTE*, DWORD) { chegou = true; return false; }, erro);
    if (chegou) erro.clear();
    return ok || chegou;
}
inline void Acordar() { if (HWND h = ofertas::detail::g_janelaLauncher.load()) PostMessageW(h, ofertas::detail::WM_OFERTAS_ATUALIZA, 0, 0); }

// Uma consulta ao versao.json. `primeira` separa a checagem da abertura (que loga tudo e
// mexe no sinal `rede` da vitrine) das repeticoes com a janela aberta, que so interessam
// quando ENCONTRAM versao nova - um tropeco de rede aos 30 minutos nao e noticia.
inline void ChecarVersao(Verificacao* v, bool primeira) {
    const std::wstring log = ArquivoLogLauncher(v->raiz);
    std::string json; std::wstring erro;
    const ULONGLONG t0 = GetTickCount64();
    if (!GetTexto(ComQuery(v->cfg.urlManifesto, v->ident.Query()), json, erro)) {
        if (primeira) {
            Anotar(log, L"versao: sem resposta em %llu ms (%ls) - nada aparece", (unsigned long long)(GetTickCount64() - t0), erro.c_str());
            v->rede.store(2);
            v->pronto.store(2);
        }
        return;
    }
    if (primeira) v->rede.store(1);   // a rede esta ai (a vitrine pode abrir), valha o manifesto ou nao
    CarimbarVerificacao(v->raiz);
    Manifesto m; std::wstring aviso;
    if (!LerManifesto(json, m, erro, &aviso)) { if (primeira) { Anotar(log, L"versao: manifesto invalido (%ls)", erro.c_str()); v->pronto.store(2); } return; }
    if (primeira && !aviso.empty()) Anotar(log, L"versao: %ls", aviso.c_str());
    // A pagina de download: a do manifesto so se for https do pcdofafa.com.br; senao a embutida.
    if (!m.pagina.empty() && !PaginaDoSiteValida(m.pagina)) {
        if (primeira) Anotar(log, L"versao: pagina do manifesto recusada (so https de pcdofafa.com.br): %ls", m.pagina.substr(0, 200).c_str());
        m.pagina.clear();
    }
    if (m.pagina.empty()) m.pagina = kPaginaDownloadPadrao;
    // O zip do modo direto: https (ou 127.0.0.1 de teste); recusado = vira modo site, o aviso continua
    if (m.Direto()) {
        Url uz;
        if (!QuebrarUrl(m.zipUrl, uz) || (uz.local && !v->teste)) {
            if (primeira) Anotar(log, L"versao: url do pacote recusada (%ls) - fica o modo site", m.zipUrl.substr(0, 200).c_str());
            m.zipUrl.clear(); m.zipSha256.clear();
        }
    }
    if (CompararVersao(m.versao, DLSS5_VERSAO) <= 0) {
        if (primeira) {
            Anotar(log, L"versao: manifesto %ls, instalada %ls - ja esta na ultima (%llu ms)", m.versao.c_str(), DLSS5_VERSAO, (unsigned long long)(GetTickCount64() - t0));
            v->pronto.store(2);
        }
        return;
    }
    if (!primeira && v->pronto.load() == 1 && v->manifesto.versao == m.versao) return;   // ja anunciada
    v->manifesto = m;
    v->automatica = LerAutomatica(v->raiz);
    v->podeAuto = m.Direto() && (m.minimaParaAuto.empty() || CompararVersao(DLSS5_VERSAO, m.minimaParaAuto) >= 0);
    Anotar(log, primeira ? L"versao: NOVA VERSAO %ls (instalada %ls) em %llu ms; modo=%ls automatica=%d podeAuto=%d"
                         : L"versao: NOVA VERSAO %ls publicada com a janela aberta (instalada %ls) em %llu ms; modo=%ls automatica=%d podeAuto=%d",
           m.versao.c_str(), DLSS5_VERSAO, (unsigned long long)(GetTickCount64() - t0), m.Direto() ? L"direto" : L"site", (int)v->automatica, (int)v->podeAuto);
    v->pronto.store(1);
    Acordar();   // a pilula "atualizacao disponivel" aparece sozinha na tela que ja esta aberta
}
// A VERIFICACAO DE VERSAO TAMBEM VIVE enquanto a janela viver. A da abertura acontece na
// hora; depois ela se repete a cada `versaoMs` (30 min), para o caso de o autor publicar uma
// versao com o launcher ja na tela. A pergunta continua sendo SO depois do clique em INICIAR -
// o que muda com a janela aberta e a pilula discreta ao lado do nome, nunca uma caixa que
// pula na frente de quem esta fazendo outra coisa.
inline DWORD WINAPI ThreadManifesto(LPVOID p) {
    Verificacao* v = (Verificacao*)p;
    bool primeira = true;
    for (;;) {
        ChecarVersao(v, primeira);
        primeira = false;
        if (!v->pararPoll) return 0;
        if (WaitForSingleObject(v->pararPoll, (DWORD)v->versaoMs) == WAIT_OBJECT_0) return 0;
    }
}
inline DWORD WINAPI ThreadSonda(LPVOID p) {
    Verificacao* v = (Verificacao*)p;
    std::wstring erro;
    const ULONGLONG t0 = GetTickCount64();
    const bool ok = Sondar(v->urlSonda, erro);
    Anotar(ArquivoLogLauncher(v->raiz), L"sonda de rede (vitrine): %ls em %llu ms%ls%ls", ok ? L"respondeu" : L"sem resposta",
           (unsigned long long)(GetTickCount64() - t0), ok ? L"" : L" - ", ok ? L"" : erro.c_str());
    v->rede.store(ok ? 1 : 2);
    return 0;
}
// A busca da publi, UMA vez. Devolve true quando o conteudo que chegou e DIFERENTE do que
// `assinatura` guardava - e e essa diferenca que faz a faixa animar a troca.
inline bool BuscarPubli(Verificacao* v, std::wstring& assinatura, bool primeira) {
    const std::wstring log = ArquivoLogLauncher(v->raiz);
    std::string json; std::wstring erro;
    const ULONGLONG t0 = GetTickCount64();
    if (!GetTexto(ComQuery(v->cfg.urlOfertas, v->ident.Query()), json, erro)) {
        if (primeira) {
            Anotar(log, L"publi: sem resposta em %llu ms (%ls) - fica o cache/ini", (unsigned long long)(GetTickCount64() - t0), erro.c_str());
            v->remotoPronto.store(2);
        }
        return false;   // nas repeticoes, um tropeco de rede nao vira linha de log nem apaga a faixa
    }
    // A ASSINATURA e o SHA-256 do proprio JSON: mais barata e mais confiavel do que comparar
    // campo a campo, e pega qualquer mudanca (titulo, preco, cupom, endereco).
    Sha256 sh; sh.Dados((const BYTE*)json.data(), (DWORD)json.size());
    const std::wstring nova = sh.Hex();
    if (nova == assinatura) return false;         // igual ao que ja esta na tela: nao mexe em nada
    ofertas::detail::OfertaRemota o; std::wstring validade, vc;
    if (!LerOfertas(json, o, validade, vc)) {
        if (primeira) { Anotar(log, L"publi: ofertas.json invalido - fica o cache/ini"); v->remotoPronto.store(2); }
        return false;
    }
    CriarPastas(PastaCache(v->raiz));
    Escrever(ArquivoCacheOfertas(v->raiz), json);   // cache do ultimo JSON valido (a validade e conferida ao ler)
    if (ValidadeVencida(validade)) {
        if (primeira) { Anotar(log, L"publi: %ls vencida em %ls - fica o ini", vc.c_str(), validade.c_str()); v->remotoPronto.store(2); }
        return false;
    }
    assinatura = nova;
    v->remoto = o;
    v->remotoPronto.store(o.Vazio() ? 2 : 1);
    if (o.Vazio()) return false;
    Anotar(log, primeira ? L"publi: conteudo %ls em %llu ms%ls%ls"
                         : L"faixa: oferta nova %ls em %llu ms%ls%ls",
           vc.empty() ? L"(sem versao_conteudo)" : vc.c_str(), (unsigned long long)(GetTickCount64() - t0),
           o.cupom.empty() ? L"" : L"; cupom ", o.cupom.c_str());
    return true;
}
// A thread da publi VIVE enquanto a janela viver: primeira busca imediata, depois de `pollMs`
// em `pollMs`. Com a faixa oculta (ou em ingles) ela continua acordando, mas nao vai a rede -
// e assim ela volta a buscar no instante em que a pessoa reativa a faixa, sem precisar de uma
// thread nova. Sai na hora quando `pararPoll` e sinalizado (EncerrarVerificacao).
inline DWORD WINAPI ThreadOfertas(LPVOID p) {
    Verificacao* v = (Verificacao*)p;
    std::wstring assinatura;
    bool primeira = true;
    for (;;) {
        if (v->pollLigado.load() != 0) {
            if (BuscarPubli(v, assinatura, primeira)) {
                v->geracaoRemoto.fetch_add(1);
                Acordar();
            } else if (primeira) {
                Acordar();                         // a primeira resposta sempre acorda a tela
            }
            primeira = false;
        }
        if (!v->pararPoll) return 0;               // sem evento de parada nao ha repeticao
        if (WaitForSingleObject(v->pararPoll, (DWORD)v->pollMs) == WAIT_OBJECT_0) return 0;
    }
}
inline DWORD WINAPI ThreadResumo(LPVOID p) {
    Verificacao* v = (Verificacao*)p;
    const std::wstring log = ArquivoLogLauncher(v->raiz);
    std::string json; std::wstring erro;
    ofertas::detail::ResumoUso r;
    if (!GetTexto(v->cfg.urlResumo, json, erro) || !LerResumo(json, r)) {
        Anotar(log, L"resumo: indisponivel (%ls)", erro.empty() ? L"JSON invalido" : erro.c_str());
        v->resumo.ok = false; v->resumo.ultimoRegistro = UltimoRegistroUso(v->raiz);
        v->resumoPronto.store(2); Acordar(); return 0;
    }
    v->resumo = r;
    GravarRelatorioUso(v->raiz, r);
    Anotar(log, L"resumo: hoje %lld/%lld, 7d %lld/%lld, 30d %lld/%lld (aberturas/usuarios)", r.hojeAb, r.hojeUs, r.d7Ab, r.d7Us, r.d30Ab, r.d30Us);
    v->resumoPronto.store(1); Acordar();
    return 0;
}

// Le a configuracao, a identidade e o cache, e dispara as buscas que existirem. Volta na
// hora; a tela nao espera. Sem url_base (e sem url_* avulsos) nao ha rede nenhuma.
inline void IniciarVerificacao(Verificacao& v, const std::wstring& raiz) {
    TrancaDoLog();   // inicializa a tranca do log AQUI, na thread principal, antes de existir thread
    // O EVENTO DE PARADA e o que transforma as buscas unicas em POLLING: a do versao.json e a
    // do ofertas.json repetem enquanto ele nao for sinalizado, e saem na hora quando o
    // launcher fecha (EncerrarVerificacao o sinaliza). Ele nasce aqui, antes das threads,
    // porque as duas o usam. Se a criacao falhar, cada thread busca uma vez e morre - o
    // comportamento antigo, que continua correto, so nao se atualiza sozinho.
    v.pararPoll = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    v.raiz = raiz;
    v.cfg = LerConfig(raiz);
    v.ident = LerIdentidade(raiz, v.cfg.contar);
    const std::wstring log = ArquivoLogLauncher(raiz);
    auto valida = [&](std::wstring& url, const wchar_t* nome) {
        if (url.empty()) return false;
        Url u;
        if (!QuebrarUrl(url, u)) { Anotar(log, L"%ls recusado (so https; %ls)", nome, url.substr(0, 200).c_str()); url.clear(); return false; }
        if (u.local) v.teste = true;
        return true;
    };
    const bool temManifesto = valida(v.cfg.urlManifesto, L"url_manifesto");
    const bool temOfertas   = valida(v.cfg.urlOfertas, L"url_ofertas");
    const bool temResumo    = valida(v.cfg.urlResumo, L"relatorio_url");
    valida(v.cfg.urlEvento, L"url_evento");
    // a vitrine: https ou local de teste, como os outros; file:/// (pagina local) passa direto
    if (!v.cfg.urlVitrine.empty() && !UrlVitrineEhLocal(v.cfg.urlVitrine) && !valida(v.cfg.urlVitrine, L"vitrine_url"))
        v.cfg.urlVitrine = kUrlVitrinePadrao;
    // cache da publi: entra na tela ja, se ainda vale
    if (temOfertas) {
        const std::string cache = LerBytes(ArquivoCacheOfertas(raiz), kJsonMax);
        std::wstring validade, vc;
        if (!cache.empty()) {
            ofertas::detail::OfertaRemota o;
            if (LerOfertas(cache, o, validade, vc) && !ValidadeVencida(validade)) v.remotoCache = o;
            else Anotar(log, L"publi: cache %ls ignorado (%ls)", vc.c_str(), ValidadeVencida(validade) ? L"vencido" : L"invalido");
        }
    }
    // versao: com contar=1 este GET e a contagem da abertura, entao vai toda vez; com
    // contar=0 vale o intervalo de 6 h (e a excecao de teste / DLSS5_ATUALIZACAO_FORCAR)
    if (temManifesto) {
        wchar_t forcar[4]{};
        const bool semLimite = v.cfg.contar || v.teste || (GetEnvironmentVariableW(L"DLSS5_ATUALIZACAO_FORCAR", forcar, 4) && forcar[0] == L'1');
        const ULONGLONG ultima = UltimaVerificacao(raiz), agora = AgoraUnix();
        if (semLimite || !(ultima && agora >= ultima && agora - ultima < kIntervaloVerificacaoS)) {
            v.pronto.store(0);
            v.threads[0] = CreateThread(nullptr, 0, ThreadManifesto, &v, 0, nullptr);
            if (!v.threads[0]) v.pronto.store(2);
        } else if (v.cfg.vitrine) {
            // O intervalo de 6 h segurou o GET de versao.json (contar=0), mas a vitrine precisa
            // saber se ha rede: uma sonda bate no mesmo endereco (sem parametro nenhum, como
            // manda o contar=0) so para o sinal `rede`. O manifesto nao e lido: e uma sonda.
            v.urlSonda = ComQuery(v.cfg.urlManifesto, v.ident.Query());
            v.threads[3] = CreateThread(nullptr, 0, ThreadSonda, &v, 0, nullptr);
        }
    } else if (v.cfg.vitrine && !v.cfg.urlVitrine.empty()) {
        // Sem url_base nao ha versao.json para dizer se ha rede: a sonda bate na propria vitrine
        // (so o cabecalho). Pagina local (file:///) nao precisa de rede nenhuma.
        if (UrlVitrineEhLocal(v.cfg.urlVitrine)) v.rede.store(1);
        else { v.urlSonda = v.cfg.urlVitrine; v.threads[3] = CreateThread(nullptr, 0, ThreadSonda, &v, 0, nullptr); }
    }
    if (temOfertas) {
        v.remotoPronto.store(0);
        {   // intervalo de teste: DLSS5_POLL_MS, com DLSS5_DIAG=1, para filmar a troca
            wchar_t d[8]{}, b[16]{};
            if (GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) && d[0] == L'1' &&
                GetEnvironmentVariableW(L"DLSS5_POLL_MS", b, 16)) {
                const int ms = _wtoi(b);
                if (ms >= 1000 && ms <= 600000) v.pollMs = ms;
            }
            // e o mesmo para a reconsulta do versao.json, que no uso normal e de 30 min
            if (GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) && d[0] == L'1' &&
                GetEnvironmentVariableW(L"DLSS5_VERSAO_MS", b, 16)) {
                const int ms = _wtoi(b);
                if (ms >= 1000 && ms <= 3600000) v.versaoMs = ms;
            }
        }
        v.threads[1] = CreateThread(nullptr, 0, ThreadOfertas, &v, 0, nullptr);
        if (!v.threads[1]) v.remotoPronto.store(2);
    }
    if (temResumo) {
        v.cartao = true;
        v.resumoPronto.store(0);
        v.threads[2] = CreateThread(nullptr, 0, ThreadResumo, &v, 0, nullptr);
        if (!v.threads[2]) { v.resumoPronto.store(2); v.resumo.ultimoRegistro = UltimoRegistroUso(raiz); }
    }
}
// ----------------------------------------------------------------- eventos (fire-and-forget)
struct EventoArgs { std::wstring url, log, nome; };
inline DWORD WINAPI ThreadEvento(LPVOID p) {
    EventoArgs* a = (EventoArgs*)p;
    std::string corpo; std::wstring erro;
    const bool ok = GetTexto(a->url, corpo, erro, kTimeoutEventoMs);
    Anotar(a->log, L"evento %ls: %ls", a->nome.c_str(), ok ? L"enviado" : erro.c_str());
    delete a;
    return 0;
}
// Registra um clique na tela. Nada e enviado com contar=0 ou sem url_base. Resposta ignorada.
inline void EnviarEvento(Verificacao& v, const wchar_t* nome) {
    if (!v.cfg.contar || v.cfg.urlEvento.empty() || !nome || !*nome) return;
    EventoArgs* a = new EventoArgs{ ComQuery(v.cfg.urlEvento, L"e=" + UrlEnc(nome) + L"&" + v.ident.Query()), ArquivoLogLauncher(v.raiz), nome };
    HANDLE h = CreateThread(nullptr, 0, ThreadEvento, a, 0, nullptr);
    if (h) v.eventos.push_back(h); else delete a;
}
// No fim do launcher: da uma chance curta as threads de terminar (o app ja esta aberto ou a
// pessoa ja fechou; ninguem percebe a espera). Eventos ate `msEventos`; buscas ate 300 ms.
inline void EncerrarVerificacao(Verificacao& v, DWORD msEventos = 2000) {
    // a thread da publi dorme entre as buscas: acorda-la aqui e o que a faz sair na hora, em
    // vez de o launcher esperar o intervalo inteiro para fechar
    if (v.pararPoll) SetEvent(v.pararPoll);
    if (!v.eventos.empty()) {
        WaitForMultipleObjects((DWORD)v.eventos.size(), v.eventos.data(), TRUE, msEventos);
        for (HANDLE h : v.eventos) CloseHandle(h);
        v.eventos.clear();
    }
    for (HANDLE& h : v.threads) if (h) { WaitForSingleObject(h, 300); CloseHandle(h); h = nullptr; }
    if (v.pararPoll) { CloseHandle(v.pararPoll); v.pararPoll = nullptr; }
}

// ----------------------------------------------------------------- a pergunta (caixa do Windows)
// TaskDialogIndirect (comctl32 v6, ativado pelo manifesto do launcher): titulo "DLSS5 For
// All", "Nova versao disponivel", a versao e as notas, icone de informacao. Dois jeitos, pelo
// manifesto (Manifesto::Direto):
//   MODO SITE   botoes "Abrir a pagina de download" (padrao, Enter) e "Agora nao" (Esc). Sem
//               caixa "automaticamente": nada e baixado por aqui.
//   MODO DIRETO botoes "Atualizar agora" (padrao), "Abrir a pagina do site" e "Agora nao", e a
//               caixa de verificacao "Atualizar automaticamente nas proximas vezes".
// Sem TaskDialogIndirect (comctl32 antigo) cai no MessageBoxW Sim/Nao (no modo direto, Sim =
// atualizar agora; o site fica para a mensagem de falha) e a caixa automatica fica so no ini.
// Devolve RESP_NADA (agora nao / fechou), RESP_ATUALIZAR (so no modo direto: baixar e trocar) ou
// RESP_SITE (abrir a pagina; quem abre e o chamador). `automatica` sai com o estado da caixa.
// O programa instalado abre depois de QUALQUER resposta, menos a troca direta que deu certo.
// A CAIXA E CURTA DE PROPOSITO (11/09/2026): uma decisao de dois caminhos. O que e detalhe
// (tamanho, data, o link "o que mudou") desce para o rodape, em letra menor. As notas da versao
// voltaram para o corpo na 2.1 (uma ou duas frases, ate 400 caracteres): no modo site e por
// elas que a pessoa decide ir baixar.
enum RespostaAviso { RESP_NADA = 0, RESP_ATUALIZAR = 1, RESP_SITE = 2 };
inline int PerguntarAtualizar(const Manifesto& m, bool& automatica,
                              const std::wstring& urlNovidades = std::wstring()) {
    const bool direto = m.Direto();
    const std::wstring titulo = L"DLSS5 For All";
    const std::wstring instrucao = TS(L"Nova versão disponível", L"New version available");
    std::wstring conteudo = direto
        ? (TS(L"A versão ", L"DLSS5 For All version ") + m.versao + TS(L" do DLSS5 For All está pronta.", L" is ready."))
        : (TS(L"A versão ", L"DLSS5 For All version ") + m.versao +
           TS(L" do DLSS5 For All está no site. Baixe e instale por lá, do zero; o programa instalado continua abrindo normalmente.",
              L" is on the website. Download and install it from there, from scratch; the installed program keeps opening normally."));
    if (!m.NotasNoIdioma().empty()) conteudo += L"\n\n" + m.NotasNoIdioma();
    // O RODAPE: tamanho (so do zip, modo direto), data e o link das novidades. Nada e obrigatorio:
    // sem nada disso o rodape simplesmente nao aparece.
    std::wstring rodape;
    if (direto && !m.TamanhoLegivel().empty()) rodape = m.TamanhoLegivel();
    if (!m.data.empty()) rodape += (rodape.empty() ? L"" : L" · ") + m.data;
    if (!urlNovidades.empty()) {
        // o <a href> so vira link com TDF_ENABLE_HYPERLINKS; o clique chega no callback
        rodape += (rodape.empty() ? L"" : L" · ");
        rodape += L"<a href=\"novidades\">" + std::wstring(TS(L"o que mudou", L"what changed")) + L"</a>";
    }
    const std::wstring caixa = TS(L"Atualizar automaticamente", L"Update automatically");
    const std::wstring atualizar = TS(L"Atualizar agora", L"Update now");
    const std::wstring site = direto ? std::wstring(TS(L"Abrir a página do site", L"Open the website"))
                                     : std::wstring(TS(L"Abrir a página de download", L"Open the download page"));
    const std::wstring nao = TS(L"Agora não", L"Not now");

    typedef HRESULT(WINAPI* PFN)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE cc = LoadLibraryW(L"comctl32.dll");
    PFN td = cc ? (PFN)(void*)GetProcAddress(cc, "TaskDialogIndirect") : nullptr;
    int resposta = RESP_NADA;
    if (td) {
        // 100 = o botao principal (Enter); 102 = o site (so no modo direto); 101 = agora nao (Esc)
        TASKDIALOG_BUTTON botoes[3];
        UINT n = 0;
        if (direto) { botoes[n++] = { 100, atualizar.c_str() }; botoes[n++] = { 102, site.c_str() }; }
        else        { botoes[n++] = { 102, site.c_str() }; }
        botoes[n++] = { 101, nao.c_str() };
        TASKDIALOGCONFIG cfg{};
        cfg.cbSize = sizeof(cfg);
        cfg.hInstance = nullptr;
        cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_POSITION_RELATIVE_TO_WINDOW;
        cfg.pszWindowTitle = titulo.c_str();
        cfg.pszMainIcon = TD_INFORMATION_ICON;
        cfg.pszMainInstruction = instrucao.c_str();
        cfg.pszContent = conteudo.c_str();
        cfg.cButtons = n; cfg.pButtons = botoes; cfg.nDefaultButton = direto ? 100 : 102;
        if (direto) cfg.pszVerificationText = caixa.c_str();
        if (!rodape.empty()) {
            cfg.pszFooter = rodape.c_str();
        }
        // Hyperlinks ligados SEMPRE que ha link no rodape. O clique no "o que mudou" abre o
        // navegador e NAO fecha a caixa: a pessoa le a lista e volta para responder. lpCallbackData
        // leva o endereco, porque o callback e uma funcao livre (sem captura) - a assinatura do
        // TaskDialog exige.
        if (!urlNovidades.empty() && !rodape.empty()) {
            cfg.dwFlags |= TDF_ENABLE_HYPERLINKS;
            cfg.lpCallbackData = (LONG_PTR)&urlNovidades;
            cfg.pfCallback = [](HWND, UINT msg, WPARAM, LPARAM lp, LONG_PTR dados) -> HRESULT {
                if (msg == TDN_HYPERLINK_CLICKED && dados) {
                    (void)lp;
                    ofertas::detail::OpenLink(*(const std::wstring*)dados);
                }
                return S_OK;
            };
        }
        if (direto && automatica) cfg.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
        int botao = 0; BOOL marcada = (direto && automatica) ? TRUE : FALSE;
        if (SUCCEEDED(td(&cfg, &botao, nullptr, &marcada))) {
            resposta = (botao == 100) ? RESP_ATUALIZAR : (botao == 102) ? RESP_SITE : RESP_NADA;   // IDCANCEL (Esc/X) = agora nao
            if (direto) automatica = marcada != FALSE;
        }
    } else {
        const std::wstring texto = instrucao + L"\n\n" + conteudo + (direto ? L"" : (L"\n\n" + site + L"?"));
        const int r = MessageBoxW(nullptr, texto.c_str(), titulo.c_str(),
                                  MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1 | MB_TOPMOST);
        resposta = (r == IDYES) ? (direto ? RESP_ATUALIZAR : RESP_SITE) : RESP_NADA;
    }
    if (cc) FreeLibrary(cc);
    return resposta;
}
// Falhou o download, o hash ou a troca (modo direto): diz o motivo e OFERECE a pagina do site, para
// instalar do zero. Sim abre a pagina (quem chama conta o clique); o programa instalado abre em seguida.
inline bool ErroOferecerSite(const std::wstring& erro, const std::wstring& log, const std::wstring& pagina) {
    const std::wstring txt = erro + TS(L"\n\nDetalhes em:\n", L"\n\nDetails in:\n") + log +
        TS(L"\n\nQuer abrir a página de download no site para instalar a nova versão do zero?",
           L"\n\nOpen the download page on the website to install the new version from scratch?");
    const int r = MessageBoxW(nullptr, txt.c_str(), L"DLSS5 For All", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1);
    if (r != IDYES) return false;
    ofertas::detail::OpenLink(pagina);
    return true;
}

// ----------------------------------------------------------------- mescla do ofertas.ini
// Acrescenta no ini da pessoa SO as chaves (por secao) que o ini novo tem e o dela nao.
// Nao muda valor nenhum, nao apaga comentario. Devolve quantas chaves entraram (-1 = erro).
inline int MesclarIni(const std::wstring& novoPath, const std::wstring& atualPath) {
    const std::wstring novo = LerTexto(novoPath), atual = LerTexto(atualPath);
    if (novo.empty() || atual.empty()) return 0;
    struct Chave { std::wstring secao, linha, nome; };
    auto ler = [](const std::wstring& txt, std::vector<Chave>& out) {
        std::wstring sec; size_t pos = 0;
        while (pos < txt.size()) {
            size_t nl = txt.find(L'\n', pos);
            std::wstring l = Trim(txt.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
            pos = (nl == std::wstring::npos) ? txt.size() : nl + 1;
            if (l.empty() || l[0] == L';' || l[0] == L'#') continue;
            if (l[0] == L'[') { size_t e = l.find(L']'); if (e != std::wstring::npos) sec = Minusculas(Trim(l.substr(1, e - 1))); continue; }
            size_t eq = l.find(L'=');
            if (eq == std::wstring::npos || eq == 0) continue;
            out.push_back({ sec, l, Minusculas(Trim(l.substr(0, eq))) });
        }
    };
    std::vector<Chave> cn, ca;
    ler(novo, cn); ler(atual, ca);
    std::vector<Chave> faltam;
    for (const Chave& k : cn) {
        bool tem = false;
        for (const Chave& a : ca) if (a.secao == k.secao && a.nome == k.nome) { tem = true; break; }
        if (!tem) faltam.push_back(k);
    }
    if (faltam.empty()) return 0;
    std::vector<std::wstring> linhas;
    { size_t pos = 0; while (pos <= atual.size()) { size_t nl = atual.find(L'\n', pos); if (nl == std::wstring::npos) { linhas.push_back(atual.substr(pos)); break; } std::wstring l = atual.substr(pos, nl - pos); if (!l.empty() && l.back() == L'\r') l.pop_back(); linhas.push_back(l); pos = nl + 1; } }
    std::vector<std::wstring> saida; std::wstring sec; std::vector<bool> usada(faltam.size(), false);
    auto despeja = [&](const std::wstring& s) {
        for (size_t i = 0; i < faltam.size(); i++)
            if (!usada[i] && faltam[i].secao == s) { saida.push_back(L"; (acrescentada pela atualizacao)"); saida.push_back(faltam[i].linha); usada[i] = true; }
    };
    for (size_t i = 0; i < linhas.size(); i++) {
        const std::wstring t = Trim(linhas[i]);
        if (!t.empty() && t[0] == L'[') { despeja(sec); size_t e = t.find(L']'); sec = (e != std::wstring::npos) ? Minusculas(Trim(t.substr(1, e - 1))) : sec; }
        saida.push_back(linhas[i]);
    }
    despeja(sec);
    for (size_t i = 0; i < faltam.size(); i++) {
        if (usada[i]) continue;
        saida.push_back(L""); saida.push_back(L"[" + faltam[i].secao + L"]");
        const std::wstring s = faltam[i].secao;
        despeja(s);
    }
    bool tinhaBom = false;
    {
        HANDLE hf = CreateFileW(atualPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf != INVALID_HANDLE_VALUE) { unsigned char b3[3]{}; DWORD rd = 0; ReadFile(hf, b3, 3, &rd, nullptr); CloseHandle(hf); tinhaBom = rd == 3 && b3[0] == 0xEF && b3[1] == 0xBB && b3[2] == 0xBF; }
    }
    const char* fim = (atual.find(L'\r') != std::wstring::npos) ? "\r\n" : "\n";
    std::string bytes = tinhaBom ? "\xEF\xBB\xBF" : "";
    for (size_t i = 0; i < saida.size(); i++) { bytes += WParaUtf8(saida[i]); if (i + 1 < saida.size()) bytes += fim; }
    return Escrever(atualPath, bytes).empty() ? (int)faltam.size() : -1;
}

// ----------------------------------------------------------------- processos auxiliares
inline int RodarEEsperar(const std::wstring& linha, DWORD tetoMs, std::atomic<bool>* cancelar) {
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring buf = linha;
    if (!CreateProcessW(nullptr, &buf[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    CloseHandle(pi.hThread);
    const ULONGLONG fim = GetTickCount64() + tetoMs;
    DWORD codigo = (DWORD)-2;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) { GetExitCodeProcess(pi.hProcess, &codigo); break; }
        if (GetTickCount64() > fim || (cancelar && cancelar->load())) { TerminateProcess(pi.hProcess, 1); codigo = (DWORD)-3; break; }
    }
    CloseHandle(pi.hProcess);
    return (int)codigo;
}
inline std::wstring PastaSystem32() { wchar_t s[MAX_PATH]{}; GetSystemDirectoryW(s, MAX_PATH); return std::wstring(s) + L"\\"; }
// A extracao e so pelo tar.exe do Windows (System32, Windows 10 1803 ou mais novo - o programa ja exige
// Windows 10/11 com D3D12). 07/10/2026: o reserva pelo PowerShell (Expand-Archive) saiu; sem tar.exe, ou
// com ele falhando, a atualizacao direta falha sem trocar nada e a mensagem oferece a pagina do site.
inline bool Extrair(const std::wstring& zip, const std::wstring& pasta, std::atomic<bool>* cancelar, const std::wstring& log) {
    const std::wstring tar = PastaSystem32() + L"tar.exe";
    if (!Existe(tar)) { Anotar(log, L"extracao: tar.exe nao existe em %ls", tar.c_str()); return false; }
    const int rc = RodarEEsperar(L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + pasta + L"\"", 15 * 60 * 1000, cancelar);
    Anotar(log, L"extracao: tar.exe -> %d", rc);
    return rc == 0;
}

// ----------------------------------------------------------------- o atualizar.cmd
// Tudo em ASCII: caminhos e mensagens chegam por variaveis de ambiente (Unicode), postas
// pelo launcher antes do CreateProcess. O .cmd espera os exes do pacote sairem de uso
// (abrir para escrita falha enquanto o Windows os tem mapeados), copia com robocopy em
// duas passadas, restaura os exes do backup se algo falhar, deixa o marcador do resultado
// e reabre o launcher com a mesma escolha de modulo.
inline std::string TextoDoCmd() {
    return
        "@echo off\r\n"
        "setlocal EnableExtensions\r\n"
        "title DLSS5 For All - atualizando para %DLSS5_VERSAO_NOVA%\r\n"
        "rem Gerado pelo launcher (amd\\atualizacao.h). Variaveis: DLSS5_RAIZ (pasta do pacote, com \\ no fim),\r\n"
        "rem DLSS5_PREPARO (arquivos novos), DLSS5_ZIP, DLSS5_LOG, DLSS5_BACKUP, DLSS5_VERSAO_NOVA, DLSS5_VERSAO_ANTERIOR,\r\n"
        "rem DLSS5_FLAG_MODULO (--nvidia/--amd/--sem-dlss), DLSS5_XF (arquivos da pessoa), DLSS5_XD (pastas).\r\n"
        "echo.\r\n"
        "echo   DLSS5 For All - aplicando a versao %DLSS5_VERSAO_NOVA%\r\n"
        "echo   %DLSS5_TXT_ESPERANDO%\r\n"
        "echo [%date% %time%] atualizar.cmd: inicio raiz=%DLSS5_RAIZ% >> \"%DLSS5_LOG%\"\r\n"
        "set /a TENTATIVAS=0\r\n"
        ":esperar\r\n"
        "set \"OCUPADO=\"\r\n"
        "for %%F in (\"%DLSS5_RAIZ%DLSS5ForAll.exe\" \"%DLSS5_RAIZ%bin\\DLSS5ForAll.exe\" \"%DLSS5_RAIZ%bin-amd\\DLSS5ForAll.exe\") do (\r\n"
        "    if exist \"%%~F\" ( (type nul >> \"%%~F\") 2>nul || set \"OCUPADO=1\" )\r\n"
        ")\r\n"
        "if defined OCUPADO (\r\n"
        "    set /a TENTATIVAS+=1\r\n"
        "    if %TENTATIVAS% GEQ 180 goto ocupado\r\n"
        "    timeout /t 1 /nobreak >nul\r\n"
        "    goto esperar\r\n"
        ")\r\n"
        "echo   %DLSS5_TXT_COPIANDO%\r\n"
        "echo [%date% %time%] passada 1 (tudo menos o que e da pessoa) >> \"%DLSS5_LOG%\"\r\n"
        "robocopy \"%DLSS5_PREPARO%\" \"%DLSS5_RAIZ%.\" /E /R:3 /W:1 /NP /NJH /NDL /XX /XF %DLSS5_XF% /XD %DLSS5_XD% >> \"%DLSS5_LOG%\"\r\n"
        "set RC=%ERRORLEVEL%\r\n"
        "if %RC% GEQ 8 goto falhou\r\n"
        "echo [%date% %time%] passada 2 (.ini/.cfg que a pessoa ainda nao tem) >> \"%DLSS5_LOG%\"\r\n"
        "robocopy \"%DLSS5_PREPARO%\" \"%DLSS5_RAIZ%.\" *.ini *.cfg /E /XC /XN /XO /R:3 /W:1 /NP /NJH /NDL /XX /XD %DLSS5_XD% >> \"%DLSS5_LOG%\"\r\n"
        "set RC=%ERRORLEVEL%\r\n"
        "if %RC% GEQ 8 goto falhou\r\n"
        "echo [%date% %time%] copiado; limpando o preparo >> \"%DLSS5_LOG%\"\r\n"
        "rd /s /q \"%DLSS5_PREPARO%\" 2>nul\r\n"
        "del /q \"%DLSS5_ZIP%\" 2>nul\r\n"
        "echo ok %DLSS5_VERSAO_NOVA%> \"%DLSS5_RAIZ%atualizacao-resultado.txt\"\r\n"
        "echo   %DLSS5_TXT_PRONTO%\r\n"
        "echo [%date% %time%] ok - reabrindo o launcher (%DLSS5_FLAG_MODULO%) >> \"%DLSS5_LOG%\"\r\n"
        "start \"\" /D \"%DLSS5_RAIZ%.\" \"%DLSS5_RAIZ%DLSS5ForAll.exe\" --atualizado=%DLSS5_VERSAO_ANTERIOR% %DLSS5_FLAG_MODULO%\r\n"
        "(goto) 2>nul & del \"%~f0\"\r\n"
        "exit /b 0\r\n"
        ":ocupado\r\n"
        "echo [%date% %time%] os exes continuaram em uso por 3 minutos; nada foi trocado >> \"%DLSS5_LOG%\"\r\n"
        "goto erro\r\n"
        ":falhou\r\n"
        "echo [%date% %time%] robocopy devolveu %RC%; devolvendo os exes do backup >> \"%DLSS5_LOG%\"\r\n"
        "copy /y \"%DLSS5_BACKUP%\\DLSS5ForAll.exe\" \"%DLSS5_RAIZ%DLSS5ForAll.exe\" >> \"%DLSS5_LOG%\"\r\n"
        "if exist \"%DLSS5_BACKUP%\\bin\\DLSS5ForAll.exe\" copy /y \"%DLSS5_BACKUP%\\bin\\DLSS5ForAll.exe\" \"%DLSS5_RAIZ%bin\\DLSS5ForAll.exe\" >> \"%DLSS5_LOG%\"\r\n"
        "if exist \"%DLSS5_BACKUP%\\bin-amd\\DLSS5ForAll.exe\" copy /y \"%DLSS5_BACKUP%\\bin-amd\\DLSS5ForAll.exe\" \"%DLSS5_RAIZ%bin-amd\\DLSS5ForAll.exe\" >> \"%DLSS5_LOG%\"\r\n"
        ":erro\r\n"
        "echo falhou %DLSS5_VERSAO_NOVA%> \"%DLSS5_RAIZ%atualizacao-resultado.txt\"\r\n"
        "rem a caixa de erro e do proprio launcher (MessageBoxW), ao ler o marcador \"falhou\".\r\n"
        "start \"\" /D \"%DLSS5_RAIZ%.\" \"%DLSS5_RAIZ%DLSS5ForAll.exe\" %DLSS5_FLAG_MODULO%\r\n"
        "exit /b 1\r\n";
}

// ----------------------------------------------------------------- janela de progresso
// Uma janelinha na paleta da tela do grupo: titulo, uma linha de estado ("Baixando 12 MB
// de 230 MB..."), a barra no gradiente da marca e um CANCELAR (que so vale enquanto e
// download: extrair e preparar levam segundos e nao deixam nada pela metade no pacote).
// Na atualizacao automatica ela E o aviso curto na tela ("Atualizando automaticamente...").
struct Progresso {
    HWND h = nullptr;
    UINT dpi = 96;
    HFONT fTit = nullptr, fTxt = nullptr, fBtn = nullptr;
    CRITICAL_SECTION cs;
    std::wstring estado;
    ULONGLONG feito = 0, total = 0;
    int fase = 0;                    // 0 baixando, 1 conferindo, 2 extraindo, 3 preparando
    std::atomic<bool> cancelar{ false };
    std::atomic<bool> terminou{ false };
    bool ok = false;
    std::wstring erro;               // vazio + !ok = cancelado
    bool hoverBtn = false;
    std::wstring versao;
    bool automatica = false;
};
constexpr UINT WM_PROG_ATUALIZA = WM_APP + 0x51;
constexpr UINT WM_PROG_FIM      = WM_APP + 0x52;
constexpr int  PRW = 540, PRH = 172;

inline RECT BotaoCancelar(int w, int h, UINT dpi) {
    using ofertas::detail::Esc;
    return RECT{ w - Esc(32, dpi) - Esc(140, dpi), h - Esc(24, dpi) - Esc(36, dpi), w - Esc(32, dpi), h - Esc(24, dpi) };
}
inline void PintarProgresso(HDC dc, int w, int h, Progresso& P) {
    using namespace ofertas::detail;
    const UINT dpi = P.dpi;
    auto E = [dpi](int v) { return Esc(v, dpi); };
    RECT all{ 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &all, bg); DeleteObject(bg);
    HBRUSH ed = CreateSolidBrush(C_CARD); FrameRect(dc, &all, ed); DeleteObject(ed);
    SetBkMode(dc, TRANSPARENT);
    RECT topo{ 0, 0, w, E(3) }; GradienteNoRecorte(dc, topo, C_CY, C_BLUE);
    RECT t{ E(32), E(22), w - E(32), E(48) };
    const std::wstring tit = (P.automatica ? TS(L"Atualizando automaticamente o DLSS5 For All para a versão ", L"Automatically updating DLSS5 For All to version ")
                                           : TS(L"Atualizando o DLSS5 For All para a versão ", L"Updating DLSS5 For All to version ")) + P.versao;
    DrawText1(dc, P.fTit, C_TEXT, t, tit.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    std::wstring estado; ULONGLONG feito, total; int fase;
    EnterCriticalSection(&P.cs); estado = P.estado; feito = P.feito; total = P.total; fase = P.fase; LeaveCriticalSection(&P.cs);
    RECT s{ E(32), E(56), w - E(32), E(80) };
    DrawText1(dc, P.fTxt, C_BODY, s, estado.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RECT trilho{ E(32), E(90), w - E(32), E(102) };
    FillRound(dc, trilho, E(6), C_SURF, 0, false);
    const int larg = trilho.right - trilho.left;
    RECT cheio = trilho;
    if (fase == 0 && total > 0)      cheio.right = trilho.left + (int)((double)larg * (double)feito / (double)total);
    else if (fase == 0)              { const int k = (int)((GetTickCount64() / 12) % (ULONGLONG)(larg + E(120))) - E(120); cheio.left = trilho.left + (k < 0 ? 0 : k); cheio.right = trilho.left + (std::min)(k + E(120), larg); }
    else                             cheio.right = trilho.right;
    if (cheio.right > cheio.left + E(4)) FillRoundGradient(dc, cheio, E(6), C_CY, C_BLUE_L);
    RECT b = BotaoCancelar(w, h, dpi);
    const bool podeCancelar = fase == 0;
    if (P.hoverBtn && podeCancelar) FillRound(dc, b, E(10), C_SURF, 0, false);
    StrokeRound(dc, b, E(10), podeCancelar ? (P.hoverBtn ? C_CY : C_CY_D) : C_CARD);
    DrawText1(dc, P.fBtn, podeCancelar ? (P.hoverBtn ? C_TEXT : C_CY_L) : C_MUTE, b,
              podeCancelar ? T(L"CANCELAR", L"CANCEL") : T(L"AGUARDE…", L"PLEASE WAIT…"), DT_CENTER | DT_SINGLELINE | DT_VCENTER, E(1));
    RECT rod{ E(32), h - E(24) - E(30), b.left - E(16), h - E(24) };
    DrawText1(dc, P.fTxt, C_MUTE, rod, T(L"Os seus arquivos e configurações são preservados.", L"Your files and settings are preserved."),
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RECT base{ 0, h - E(3), w, h }; GradienteNoRecorte(dc, base, C_CY, C_BLUE);
}
inline LRESULT CALLBACK ProgressoProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Progresso* P = (Progresso*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bm);
        if (P) PintarProgresso(mem, rc.right, rc.bottom, *P);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_PROG_ATUALIZA: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_PROG_FIM: if (P) P->terminou.store(true); DestroyWindow(h); return 0;
    case WM_MOUSEMOVE: {
        if (!P) return 0;
        RECT rc; GetClientRect(h, &rc);
        RECT b = BotaoCancelar(rc.right, rc.bottom, P->dpi);
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };
        const bool hv = PtInRect(&b, p) != 0;
        if (hv != P->hoverBtn) { P->hoverBtn = hv; InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 }; TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: if (P && P->hoverBtn) { P->hoverBtn = false; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        RECT rc; GetClientRect(h, &rc);
        RECT b = BotaoCancelar(rc.right, rc.bottom, P ? P->dpi : 96);
        SetCursor(LoadCursor(nullptr, (P && P->fase == 0 && PtInRect(&b, p)) ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_LBUTTONUP: {
        if (!P) return 0;
        RECT rc; GetClientRect(h, &rc);
        RECT b = BotaoCancelar(rc.right, rc.bottom, P->dpi);
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };
        if (PtInRect(&b, p) && P->fase == 0) P->cancelar.store(true);
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE && P && P->fase == 0) P->cancelar.store(true); return 0;
    case WM_CLOSE: if (P && P->fase == 0) P->cancelar.store(true); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ----------------------------------------------------------------- o trabalho (thread)
struct Trabalho {
    Progresso* P = nullptr;
    std::wstring raiz, temp, zip, preparo, log, flagModulo;
    Manifesto m;
};
inline void Estado(Progresso& P, int fase, const std::wstring& txt, ULONGLONG feito = 0, ULONGLONG total = 0) {
    EnterCriticalSection(&P.cs); P.fase = fase; P.estado = txt; P.feito = feito; P.total = total; LeaveCriticalSection(&P.cs);
    if (P.h) PostMessageW(P.h, WM_PROG_ATUALIZA, 0, 0);
}
inline ULONGLONG EspacoLivre(const std::wstring& pasta) {
    ULARGE_INTEGER livre{}; if (!GetDiskFreeSpaceExW(pasta.c_str(), &livre, nullptr, nullptr)) return ~0ull; return livre.QuadPart;
}

inline DWORD WINAPI ThreadTrabalho(LPVOID p) {
    Trabalho& W = *(Trabalho*)p;
    Progresso& P = *W.P;
    auto falha = [&](const std::wstring& e) { P.ok = false; P.erro = e; Anotar(W.log, L"FALHA: %ls", e.c_str()); PostMessageW(P.h, WM_PROG_FIM, 0, 0); return 0; };
    Anotar(W.log, L"---- versao %ls -> %ls | zip %ls", DLSS5_VERSAO, W.m.versao.c_str(), W.m.zipUrl.c_str());

    bool jaTem = false;
    if (Existe(W.zip)) {
        Estado(P, 1, TS(L"Conferindo um download anterior…", L"Checking an earlier download…"));
        jaTem = Sha256DoArquivo(W.zip) == W.m.zipSha256;
        if (!jaTem) DeleteFileW(W.zip.c_str());
        Anotar(W.log, L"zip anterior em %ls: %ls", W.zip.c_str(), jaTem ? L"hash confere, reaproveitado" : L"hash nao confere, apagado");
    }
    if (!jaTem) {
        Estado(P, 0, TS(L"Conectando…", L"Connecting…"));
        HANDLE f = CreateFileW(W.zip.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return falha(TS(L"Não consegui criar o arquivo em ", L"Could not create the file at ") + W.zip);
        Sha256 sh; ULONGLONG feito = 0, total = 0; std::wstring erro; bool semEspaco = false;
        ULONGLONG ultimoAviso = 0;
        const bool ok = Get(W.m.zipUrl, Timeouts{ 5000, 10000, 15000, 30000 }, 0, &P.cancelar, &total,
            [&](const BYTE* d, DWORD n) {
                if (feito == 0) {
                    if (total > kPacoteMax) { erro = TS(L"pacote grande demais", L"package too large"); return false; }
                    if (total > 0 && (EspacoLivre(W.temp) < total * 3 || EspacoLivre(W.raiz) < total * 3 / 2)) { semEspaco = true; return false; }
                }
                DWORD w = 0;
                if (!WriteFile(f, d, n, &w, nullptr) || w != n) { erro = TS(L"erro ao gravar o arquivo (disco cheio?)", L"error writing the file (disk full?)"); return false; }
                sh.Dados(d, n); feito += n;
                if (feito > kPacoteMax) { erro = TS(L"pacote grande demais", L"package too large"); return false; }
                if (feito - ultimoAviso > 256 * 1024 || feito == total) {
                    ultimoAviso = feito;
                    Estado(P, 0, total ? (TS(L"Baixando ", L"Downloading ") + Megas(feito) + TS(L" de ", L" of ") + Megas(total) + L"…")
                                       : (TS(L"Baixando ", L"Downloading ") + Megas(feito) + L"…"), feito, total);
                }
                return true;
            }, erro);
        CloseHandle(f);
        if (!ok) {
            DeleteFileW(W.zip.c_str());
            if (P.cancelar.load()) { P.ok = false; P.erro.clear(); Anotar(W.log, L"cancelado pela pessoa durante o download (%ls)", Megas(feito).c_str()); PostMessageW(P.h, WM_PROG_FIM, 0, 0); return 0; }
            if (semEspaco) return falha(TS(L"Espaço em disco insuficiente para baixar e preparar a atualização (precisa de cerca de ", L"Not enough disk space to download and prepare the update (needs about ") + Megas(total * 3) + L").");
            return falha(TS(L"O download falhou: ", L"The download failed: ") + erro);
        }
        Estado(P, 1, TS(L"Conferindo o arquivo (SHA-256)…", L"Verifying the file (SHA-256)…"), feito, total);
        const std::wstring hex = sh.Hex();
        Anotar(W.log, L"baixado %llu bytes; sha256=%ls esperado=%ls", (unsigned long long)feito, hex.c_str(), W.m.zipSha256.c_str());
        if (hex != W.m.zipSha256) {
            DeleteFileW(W.zip.c_str());
            return falha(TS(L"O arquivo baixado não confere com o manifesto (SHA-256). Nada foi alterado. Tente de novo mais tarde.",
                            L"The downloaded file does not match the manifest (SHA-256). Nothing was changed. Try again later."));
        }
    }

    Estado(P, 2, TS(L"Extraindo o pacote…", L"Extracting the package…"));
    ApagarPasta(W.preparo);
    if (!CriarPastas(W.preparo)) return falha(TS(L"Não consegui criar a pasta de preparo em ", L"Could not create the staging folder at ") + W.preparo);
    if (!Extrair(W.zip, W.preparo, nullptr, W.log)) { ApagarPasta(W.preparo); return falha(TS(L"Não consegui extrair o pacote (tar.exe do Windows falhou ou não existe).", L"Could not extract the package (the Windows tar.exe failed or is missing).")); }
    std::wstring origem = W.preparo;
    if (!Existe(origem + L"\\versao.txt")) {
        WIN32_FIND_DATAW fd; std::wstring unica; int n = 0;
        HANDLE hf = FindFirstFileW((origem + L"\\*").c_str(), &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            do { const std::wstring nm = fd.cFileName; if (nm == L"." || nm == L"..") continue; n++; if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) unica = nm; } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
        if (n == 1 && !unica.empty()) origem += L"\\" + unica;
    }
    const std::wstring vtxt = Trim(LerTexto(origem + L"\\versao.txt"));
    if (vtxt.empty() || CompararVersao(vtxt, W.m.versao) != 0) {
        ApagarPasta(W.preparo);
        return falha(TS(L"O pacote baixado não é um DLSS5 For All ", L"The downloaded package is not a DLSS5 For All ") + W.m.versao +
                     TS(L" (versao.txt ausente ou diferente). Nada foi alterado.", L" (versao.txt missing or different). Nothing was changed."));
    }
    Anotar(W.log, L"preparo ok em %ls (versao.txt=%ls)", origem.c_str(), vtxt.c_str());

    Estado(P, 3, TS(L"Preparando a troca dos arquivos…", L"Preparing to swap the files…"));
    if (Existe(origem + L"\\ofertas.ini") && Existe(W.raiz + L"ofertas.ini")) {
        const int n = MesclarIni(origem + L"\\ofertas.ini", W.raiz + L"ofertas.ini");
        Anotar(W.log, L"ofertas.ini da raiz: %d chave(s) acrescentada(s)%ls", n < 0 ? 0 : n, n < 0 ? L" (falha ao gravar; seguiu sem mesclar)" : L"");
    }
    const std::wstring bkp = W.raiz + L"backup-versao-anterior";
    CriarPastas(bkp); CriarPastas(bkp + L"\\bin"); CriarPastas(bkp + L"\\bin-amd");
    for (const wchar_t* rel : { L"DLSS5ForAll.exe", L"bin\\DLSS5ForAll.exe", L"bin-amd\\DLSS5ForAll.exe" }) {
        if (!Existe(W.raiz + rel)) continue;
        if (!CopyFileW((W.raiz + rel).c_str(), (bkp + L"\\" + rel).c_str(), FALSE))
            return falha(TS(L"Não consegui fazer a cópia de segurança de ", L"Could not back up ") + rel + L" (" + std::to_wstring(GetLastError()) + L")");
    }
    Escrever(bkp + L"\\versao.txt", WParaUtf8(std::wstring(DLSS5_VERSAO) + L"\r\n"));
    const std::wstring cmd = W.temp + L"\\atualizar.cmd";
    const std::wstring e = Escrever(cmd, TextoDoCmd());
    if (!e.empty()) return falha(TS(L"Não consegui escrever o atualizar.cmd: ", L"Could not write atualizar.cmd: ") + e);
    SetEnvironmentVariableW(L"DLSS5_RAIZ", W.raiz.c_str());
    SetEnvironmentVariableW(L"DLSS5_PREPARO", origem.c_str());
    SetEnvironmentVariableW(L"DLSS5_ZIP", W.zip.c_str());
    SetEnvironmentVariableW(L"DLSS5_LOG", W.log.c_str());
    SetEnvironmentVariableW(L"DLSS5_BACKUP", bkp.c_str());
    SetEnvironmentVariableW(L"DLSS5_VERSAO_NOVA", W.m.versao.c_str());
    SetEnvironmentVariableW(L"DLSS5_VERSAO_ANTERIOR", DLSS5_VERSAO);
    SetEnvironmentVariableW(L"DLSS5_FLAG_MODULO", W.flagModulo.c_str());
    SetEnvironmentVariableW(L"DLSS5_XF", kArquivosDaPessoa);
    SetEnvironmentVariableW(L"DLSS5_XD", kPastasDaPessoa);
    SetEnvironmentVariableW(L"DLSS5_TXT_ESPERANDO", T(L"Aguardando o programa fechar...", L"Waiting for the program to close..."));
    SetEnvironmentVariableW(L"DLSS5_TXT_COPIANDO", T(L"Copiando os arquivos novos (os seus ficam)...", L"Copying the new files (yours are kept)..."));
    SetEnvironmentVariableW(L"DLSS5_TXT_PRONTO", T(L"Pronto. Reabrindo o programa...", L"Done. Reopening the program..."));
    Anotar(W.log, L"atualizar.cmd escrito em %ls; entregando ao cmd", cmd.c_str());
    P.ok = true;
    PostMessageW(P.h, WM_PROG_FIM, 0, 0);
    return 0;
}

// -------------------------------------------------------------------------------------
//  BAIXAR E APLICAR (SO NO MODO DIRETO: manifesto com zip). Mostra a janelinha, roda o trabalho na thread e, no fim, dispara o
//  atualizar.cmd. Devolve true quando o .cmd foi disparado: o launcher deve SAIR na hora,
//  porque o .cmd espera exatamente por isso (e reabre com `flagModulo`: --nvidia, --amd ou
//  --sem-dlss, a escolha que a pessoa tinha feito). false: nada foi trocado; `erro` traz o
//  motivo para a pessoa (vazio quando ela mesma cancelou) e `logPath` onde esta o detalhe.
// -------------------------------------------------------------------------------------
inline bool BaixarEAplicar(HINSTANCE hi, const std::wstring& raiz, const Manifesto& m, bool automatica,
                           const std::wstring& flagModulo, std::wstring& erro, std::wstring& logPath) {
    // v2.1: so o modo direto baixa e troca arquivos; sem zip (+ SHA-256) no manifesto nao ha o que fazer
    if (!m.Direto()) { erro = L"manifesto sem zip_url/zip_sha256"; logPath.clear(); return false; }
    using namespace ofertas::detail;
    Trabalho W;
    W.raiz = raiz; W.temp = PastaTemp(); W.m = m; W.flagModulo = flagModulo;
    W.zip = W.temp + L"\\DLSS5ForAll-" + m.versao + L".zip";
    W.preparo = W.temp + L"\\preparo";
    W.log = W.temp + L"\\atualizacao.log";
    logPath = W.log;
    erro.clear();

    Progresso P;
    P.versao = m.versao; P.automatica = automatica;
    InitializeCriticalSection(&P.cs);
    W.P = &P;

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProgressoProc; wc.hInstance = hi; wc.lpszClassName = L"DLSS5ForAllAtualizando";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    POINT centro{ (wa.left + wa.right) / 2, (wa.top + wa.bottom) / 2 };
    P.dpi = DpiEfetivoPonto(centro);
    const int Wd = Esc(PRW, P.dpi), Ht = Esc(PRH, P.dpi);
    P.h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"DLSS5 For All", WS_POPUP,
                          wa.left + (wa.right - wa.left - Wd) / 2, wa.top + (wa.bottom - wa.top - Ht) / 2, Wd, Ht,
                          nullptr, nullptr, hi, nullptr);
    if (!P.h) { DeleteCriticalSection(&P.cs); erro = L"CreateWindow"; return false; }
    auto f = [&](int hgt, int peso) { return CreateFontW(-Esc(hgt, P.dpi), 0, 0, 0, peso, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
    P.fTit = f(17, FW_BOLD); P.fTxt = f(14, FW_NORMAL); P.fBtn = f(13, FW_BOLD);
    SetWindowLongPtrW(P.h, GWLP_USERDATA, (LONG_PTR)&P);
    RoundCorners(P.h, P.dpi);
    ShowWindow(P.h, SW_SHOW); SetForegroundWindow(P.h); UpdateWindow(P.h);
    SetTimer(P.h, 1, 80, nullptr);

    HANDLE th = CreateThread(nullptr, 0, ThreadTrabalho, &W, 0, nullptr);
    if (!th) { DestroyWindow(P.h); erro = L"CreateThread"; }
    else {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }
    for (HFONT* pf : { &P.fTit, &P.fTxt, &P.fBtn }) if (*pf) DeleteObject(*pf);
    UnregisterClassW(wc.lpszClassName, hi);
    DeleteCriticalSection(&P.cs);
    if (!P.ok) { erro = P.erro; return false; }

    const std::wstring cmd = W.temp + L"\\atualizar.cmd";
    std::wstring linha = L"cmd.exe /c \"\"" + cmd + L"\"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &linha[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, W.temp.c_str(), &si, &pi)) {
        erro = TS(L"Não consegui iniciar o atualizar.cmd (erro ", L"Could not start atualizar.cmd (error ") + std::to_wstring(GetLastError()) + L")";
        Anotar(W.log, L"FALHA: %ls", erro.c_str());
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    Anotar(W.log, L"atualizar.cmd em execucao; launcher saindo");
    return true;
}

} // namespace atualizacao
