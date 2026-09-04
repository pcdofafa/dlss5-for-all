// =====================================================================================
//  idioma.h - portugues / ingles em um lugar so
//
//  COMO SE USA. No ponto onde o texto aparece na tela, e nao numa tabela de IDs:
//
//      SetWindowTextW(h, T(L"Atualizar lista", L"Refresh list"));
//
//  T() devolve const wchar_t* e e inline: resolve para um ternario sobre uma variavel
//  global, sem alocacao e sem busca em tabela por chamada - pode ficar dentro do laco de
//  desenho do painel sem custo mensuravel. TS() faz o mesmo devolvendo std::wstring, para
//  quando o texto entra numa concatenacao.
//
//  A ESCOLHA DO FORMATO foi deliberada. Com 400+ textos espalhados por um main.cpp de
//  400 KB, uma tabela de IDs numericos vira um mapa que ninguem consegue revisar: para
//  saber o que aparece na tela e preciso pular para outro arquivo e voltar. Assim o
//  portugues continua legivel exatamente onde e usado, e a revisao da traducao e local.
//
//  O QUE NAO PASSA POR AQUI (e o motivo, porque errar aqui quebra o app em silencio):
//   - mensagens do dlss5forall.log: sao ferramenta de diagnostico, e ha documentacao e
//     historico que dependem do texto exato. Ficam em portugues sempre.
//   - chaves e secoes de .ini (fonte=, [ultima], [atalhos], telacheia=...): sao formato de
//     arquivo. Traduzir uma chave apaga a configuracao de quem ja usa o app.
//   - nomes de tecnica, shader e arquivo do ReShade (Lumenite_Kernel, DLSS5_Feed,
//     NRIntensity, dxgi.dll...): sao identificadores que atravessam a ponte com o add-on.
//   - nomes de classe de janela, argumentos de linha de comando (--source, --window...)
//     e o protocolo do reshade_bridge.h.
//  O teste, na duvida: "uma pessoa le isso na tela?". Se nao, nao traduza.
//
//  PRIMEIRA EXECUCAO: GetUserDefaultUILanguage(). Windows em portugues abre em portugues,
//  qualquer outro abre em ingles. Depois que a pessoa escolhe, a escolha vence a deteccao
//  para sempre - fica em [idioma] lingua=pt|en no dlss5forall.ini.
// =====================================================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

enum Lingua { LINGUA_PT = 0, LINGUA_EN = 1 };

// A variavel global do idioma. `inline` (C++17) para o header poder ser incluido de
// varios lugares sem uma unidade de traducao dona dela.
inline Lingua g_lingua = LINGUA_PT;

inline const wchar_t* T(const wchar_t* pt, const wchar_t* en) {
    return (g_lingua == LINGUA_EN) ? en : pt;
}
inline std::wstring TS(const wchar_t* pt, const wchar_t* en) {
    return std::wstring((g_lingua == LINGUA_EN) ? en : pt);
}
inline bool EmIngles() { return g_lingua == LINGUA_EN; }

// Nome do idioma na propria lingua dele - e a convencao de todo seletor de idioma que
// funciona: quem so le ingles precisa reconhecer "English" na lista, e quem so le
// portugues precisa reconhecer "Portugues". Traduzir estes dois seria o unico jeito de
// deixar a lista ilegivel justamente para quem precisa dela.
inline const wchar_t* NomeLingua(Lingua l) {
    return (l == LINGUA_EN) ? L"English" : L"Português";
}
inline const wchar_t* CodigoLingua(Lingua l) { return (l == LINGUA_EN) ? L"en" : L"pt"; }

// O que o Windows sugere. So vale na primeira execucao (sem a chave no ini).
inline Lingua IdiomaDoSistema() {
    const LANGID id = GetUserDefaultUILanguage();
    return (PRIMARYLANGID(id) == LANG_PORTUGUESE) ? LINGUA_PT : LINGUA_EN;
}

// Le [idioma] lingua= do ini. Devolve true se havia escolha gravada (o chamador loga a
// diferenca entre "a pessoa escolheu" e "deduzi pelo Windows"). Mesma API de ini do resto
// do app - GetPrivateProfileString -, nada de parser novo.
inline bool CarregarIdioma(const std::wstring& iniPath) {
    wchar_t b[16]{};
    GetPrivateProfileStringW(L"idioma", L"lingua", L"", b, 16, iniPath.c_str());
    if (_wcsicmp(b, L"en") == 0) { g_lingua = LINGUA_EN; return true; }
    if (_wcsicmp(b, L"pt") == 0) { g_lingua = LINGUA_PT; return true; }
    g_lingua = IdiomaDoSistema();
    return false;
}

inline void GravarIdioma(const std::wstring& iniPath, Lingua l) {
    g_lingua = l;
    WritePrivateProfileStringW(L"idioma", L"lingua", CodigoLingua(l), iniPath.c_str());
}
