// Redireciona para o stub: existe so para `-Itests/ponte_stub` resolver o
// #include "ponte_feeder_host.h" do fonte_ponte.h enquanto o header do A1 nao esta em src\.
// Quando src\ponte_feeder_host.h existir, ele ganha (mesma pasta do includente) e este
// arquivo nunca e lido.
#pragma once
#include "../ponte_stub.h"
