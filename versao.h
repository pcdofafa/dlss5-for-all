// =====================================================================================
//  versao.h - A VERSAO DO APP, num lugar so.
//
//  Quem le: o launcher da versao portable (User-Agent da verificacao de atualizacao e a
//  comparacao "ha versao mais nova?"), o publicar-versao.ps1 (nome do zip e versao.json)
//  e o montar-portable.ps1 (versao.txt na raiz do pacote). O main.cpp deve incluir este
//  arquivo e definir APP_VERSION a partir de DLSS5_VERSAO, para as duas nunca divergirem.
//
//  FORMATO: numeros separados por ponto ("1.1", "1.2.3"). A comparacao e numerica por
//  parte (ver atualizacao::CompararVersao): "1.10" e MAIS NOVA que "1.9". Nao use letras.
//  Ao subir a versao, suba tambem o VERSIONINFO do launcher.rc e do app.rc (1.1.0.0).
// =====================================================================================
#pragma once
#define DLSS5_VERSAO L"2.2.4"
