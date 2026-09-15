// Uma DLL que nao faz absolutamente nada, para o teste de reversao.
//
// POR QUE ELA PRECISA EXISTIR. O caso mais importante do teste e "o arquivo esta CARREGADO":
// e so nesse estado que o Windows recusa apagar e ACEITA renomear, que e o caminho da remocao
// adiada. Nao da para simular isso segurando um handle comum - um arquivo aberto sem
// FILE_SHARE_DELETE recusa as duas coisas, e um aberto com ele aceita as duas. So a imagem
// MAPEADA, que e o que LoadLibrary faz, se comporta como a peca real se comporta dentro do jogo.
//
// Carregar a peca de verdade no processo de teste seria pior: ela engancha o Present e cria
// memoria compartilhada, e o teste passaria a exercitar coisas que nao esta testando. Esta aqui
// e do tamanho exato do problema.
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
