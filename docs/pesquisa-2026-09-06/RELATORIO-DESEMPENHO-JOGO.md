# Quando o jogo come a placa toda: o que dá para fazer pela película

**Data:** 2026-09-06 · **Máquina:** Windows 11 Pro 26200, RTX 4080 (driver com NvAPI ativo),
iGPU AMD, dois monitores · **Compilador:** MinGW g++ (msys64), header-only, sem `.lib` da NVIDIA.

---

## 1. O problema que o usuário trouxe

> "Testei Resident Evil com fps destravado e a película rodava a 19 fps; quando travei o fps
> do jogo, joguei a 60 com a película."

A queixa é exata e a causa é simples. O jogo com fps livre enfileira trabalho na GPU até
saturá-la. A nossa camada (a "película") é outro processo, disputando a mesma placa em pé de
igualdade, e o agendador da GPU dá a vez a quem pediu primeiro. Quem pede sem parar ganha
quase tudo. Travar o fps do jogo devolve as sobras, e a película volta a andar.

Este documento mede as saídas possíveis, escolhe uma e diz por que as outras ficaram fora.

---

## 2. Como foi medido

Um jogo de teste em D3D11 (`jogo-teste.cpp`), sem V-Sync, com carga de fragment shader
regulável, desenhando numa janela no monitor 2. Por cima dele, a nossa camada de verdade
(`DLSS5ForAll.exe --outra-instancia --overlay`). Um terceiro processo (`medidor.cpp`) abre os
dois, aplica cada estratégia, espera a camada aquecer e mede 30 s por cenário:

| coluna | o que é |
|---|---|
| **película** | quadros por segundo que a nossa camada realmente apresenta |
| **jogo** | quadros por segundo do jogo de teste (ele mesmo reporta) |
| **chegada** | com que frequência a captura entrega quadros novos à camada |
| **present → tela** | latência do nosso `Present` até o quadro aparecer, em ms |

Fontes: `scratchpad\otimizar-desempenho\medidor-stdout3.txt` (rodada 3, jogo livre) e
`medidor-stdout4.txt` (rodada 4, com teto na camada, o cenário do 3x).

**Ressalva honesta sobre os números.** Esta máquina rodou vários agentes ao mesmo tempo
durante os testes, e outros deles usavam a GPU. Duas rodadas do mesmo cenário deram números
bem diferentes (a rodada 2 mediu a película abaixo de 1 fps na base; a rodada 3 mediu 9,3 fps).
**A ordem entre as estratégias se manteve nas duas rodadas**, e é ela que sustenta as decisões
aqui. Os valores absolutos, não.

---

## 3. A tabela

Rodada 3, jogo de teste com fps livre saturando a placa. Base = a camada sem nenhuma medida.

| estratégia | película | jogo | present→tela | aplica ao vivo | risco |
|---|---|---|---|---|---|
| jogo sozinho, sem camada | — | 164,5 | — | — | referência |
| **base** (jogo livre, camada normal) | **9,3** | 153,7 | 328 ms | — | é o problema |
| 1a. nossa prioridade de GPU = HIGH | 52,7 | 148,2 | 144 ms | **sim** | nenhum |
| 1b. nossa prioridade de GPU = REALTIME | **99,2** | 148,2 | **17 ms** | **sim** | ver §4 |
| 1c. prioridade de CPU do nosso processo = HIGH | 73,3 | 151,6 | 47 ms | **sim** | nenhum |
| 1d. prioridade de GPU do JOGO = BELOW_NORMAL | 74,1 | 160,2 | 35 ms | **sim** | mexe no processo do jogo |
| 2a. limite 60 no driver, jogo **já aberto** | 75,5 | **152,0** | 47 ms | **não** | — |
| 2b. limite 30 no driver, jogo **já aberto** | 76,7 | **146,2** | 45 ms | **não** | — |
| 2c. limite 30, jogo **reaberto** | — | **30,0** | — | (na reabertura) | — |
| 2d. limite retirado, jogo **ainda aberto** | — | **30,0** | — | **não** | fica preso até reabrir |
| 2e. limite retirado, jogo **reaberto** | — | 140,1 | — | (na reabertura) | — |
| 3a. `work_resolution` 100→67 com a camada aberta | (Feeder reconstruiu) | — | — | **sim** | perde nitidez |

O ponto mais importante da tabela está nas linhas 2a/2b contra 2c: **o limite de fps do driver
não pega num jogo que já está rodando.** Com o jogo aberto, gravar 60 ou 30 no perfil não mudou
nada (152 e 146 fps). Fechado e reaberto, o mesmo perfil travou o jogo em exatos 30,0 fps.
O driver lê o perfil quando o jogo cria o device D3D, e não olha mais.

A linha 2d fecha o assunto pelo outro lado: **retirar o limite também só vale na reabertura.**
Um jogo que ficou preso em 30 continua em 30 até ser fechado. Quem aplica o limite tem a
obrigação de avisar isso, e é o que o painel faz.

### 3.1 O cenário do 3x, que é o que a pessoa vai viver

Rodada 4, com a camada já no teto de 30 fps (o que o 3x liga sozinho):

| cenário | película | jogo | present→tela |
|---|---|---|---|
| 5a. camada com teto 30, nada mais | 25,0 | 108,2 | 34 ms |
| 5b. + nossa prioridade de GPU HIGH | 25,0 | 108,3 | 33 ms |
| 5c. + nossa prioridade de GPU REALTIME | 25,0 | 117,7 | **20 ms** |
| 5e. + jogo travado em 30 (reaberto) | **19,9** | 30,0 | 21 ms |
| 6a. camada com teto 60, nada mais | 50,0 | 91,0 | 21 ms |
| 6c. + jogo travado em 60 (reaberto) | 49,9 | 60,0 | 20 ms |

Duas leituras que mudaram o desenho do módulo:

**A prioridade de GPU compra latência, não fps, quando a película já bate o teto.** De 5a para
5c a película ficou em 25 fps, mas o atraso do quadro caiu de 34 para 20 ms. Faz sentido: ela
não pode passar do próprio teto; o que sobra de melhora vai para o tempo de entrega.

**Travar o jogo no mesmo teto da película não aumenta o fps dela — e pode baixar.** Em 5e a
película caiu de 25,0 para 19,9. A razão está na coluna "chegada": com o jogo livre chegavam
91 quadros novos por segundo à captura; com o jogo em 30, chegam 30. A película nunca apresenta
mais do que captura, e quando as duas taxas ficam iguais elas batem cabeça no tempo. O ganho de
"igual à película" é liberar a placa e a latência, não subir o número.

---

## 4. Por que a prioridade de GPU é a alavanca principal

`D3DKMTSetProcessSchedulingPriorityClass` (em `gdi32.dll`, carregada em tempo de execução) muda
a classe de agendamento de GPU de **um processo só: o nosso**. É reversível, morre com o
processo, e não toca em nada do sistema nem do jogo.

O que foi medido sobre ela:

- **REALTIME (classe 5) foi aceito sem elevação.** `prio-probe.exe` rodou com token de usuário
  comum (`elevado=0`) e as classes 3, 4 e 5 retornaram `STATUS_SUCCESS`. A exigência de
  `SeIncreaseBasePriorityPrivilege` que a documentação antiga cita não se confirmou neste
  Windows. O módulo tenta REALTIME e cai para HIGH sozinho se algum sistema recusar.
- **HIGH é inconstante.** Na rodada 3 levou a película de 9,3 a 52,7 fps; na rodada 2, com a
  placa mais disputada, não mudou nada (película abaixo de 1 fps nas duas). REALTIME resolveu
  nas duas rodadas.
- **O jogo não trava.** Em todos os cenários o jogo perdeu no máximo o que a película usa
  (148 contra 153 fps na rodada 3). Com o teto de 30 da película essa fatia é pequena, e em 5c
  o jogo até subiu (117,7 contra 108,2), porque a película deixou de bloquear a fila.
- É a mesma classe em que o compositor do Windows (DWM) roda. A película **é** o caminho da
  imagem até a tela, então ela pedir a vez do compositor é coerente com o que ela faz.

**O que não usamos e por quê:** baixar a prioridade de GPU **do jogo** (linha 1d) funciona
(74,1 fps) mas exige abrir o processo do jogo com direito de escrita. Isso é exatamente o que um
anticheat vigia, e mexer no processo alheio para ganhar o que já ganhamos mexendo no nosso é
trocar risco por nada.

**Agendamento de GPU por hardware (HAGS):** está **ligado** nesta máquina (lido via
`D3DKMTQueryAdapterInfo`, `WDDM_2_7_CAPS`). Todos os números acima são com ele ligado. O módulo
**só lê** esse estado, para o log; mudá-lo é configuração do sistema da pessoa e exige reiniciar.

---

## 5. O limite de fps pelo driver (NvAPI DRS)

`nvapi64.dll` é carregada em tempo de execução e as funções saem de `nvapi_QueryInterface` pelos
IDs públicos, os mesmos do `nvapi-open-source-sdk`. Sem SDK, sem `.lib`, sem DLL redistribuída.
Se a biblioteca não existir (máquina AMD, por exemplo), o módulo escreve uma linha no log e a
alavanca some da interface. Sem exceção, sem caixa de erro.

O que o módulo faz: acha (ou cria) o perfil do exe do jogo e grava `FRL_FPS_ID` (`0x10835002`),
que é o "Max Frame Rate" do painel da NVIDIA.

**A restauração é a parte séria.** Antes de tocar no driver, o módulo grava no
`dlss5forall.ini`, seção `[desempenho]`: qual exe, qual era o valor original, se o perfil já
existia ou foi criado por nós, e o que aplicamos. Na abertura seguinte, se essas chaves estiverem
lá, ele desfaz sozinho — **inclusive depois de o app ter caído**. Se o perfil for nosso, ele é
apagado inteiro; se era da pessoa, só a chave volta ao valor de antes.

Isso está provado no teste, não só afirmado. Ver §7.

### Alcança um navegador?

O casamento do DRS é **por nome de arquivo do exe**, e isso está provado: o perfil criado para
`jogo-teste.exe` travou o jogo de teste em 30,0 fps na reabertura (cenário 2c). Aplicado a
`msedge.exe`, o mesmo mecanismo alcançaria o Edge — o Chromium apresenta por DXGI no processo
de GPU, que também se chama `msedge.exe`.

**Mesmo assim o módulo recusa navegadores, por decisão de produto.** O processo de GPU do
navegador é um só para todas as abas: limitá-lo a 30 fps por causa de um vídeo travaria a
rolagem, a interface e todas as outras abas. E, como o limite só vale na abertura, valeria
apenas depois de fechar o navegador inteiro. Um efeito colateral desses, por um ganho que a
pessoa não pediu, não passa. Quando o alvo é um navegador, o painel diz
"vídeo no navegador: sem limite disponível" e nada é gravado no perfil dele.

**Não medido:** o efeito real do FRL sobre um vídeo tocando no Edge. Medir exigiria fechar o
navegador do usuário. Como a decisão de produto não depende do resultado, o teste não se pagava.

### AMD

Não implementado. O equivalente é o FRTC/Chill via ADLX (ou o ADL antigo), e o módulo já tem o
ponto de entrada separado (`LimiteJogoDisponivelAmd()`, hoje `false`) para quando alguém
escrever. Numa máquina AMD os chips de limite aparecem apagados, com "não disponível ainda".

---

## 6. Por que não seguimos o caminho do RTSS

O RivaTuner Statistics Server é a ferramenta que as pessoas usam para travar fps de qualquer
jogo, e ela consegue o que o driver não consegue: aplicar **ao vivo**. Três razões para não ir
por ali:

1. **Ele injeta uma DLL no processo do jogo e engancha o `Present`.** É a técnica que os
   anticheats modernos tratam como ataque. Um usuário nosso poderia ser expulso de uma partida
   por causa de um botão do nosso painel. Nada do que ganharíamos paga isso.
2. **Licença.** O RTSS é freeware proprietário da Unwinder/Guru3D, sem permissão de
   redistribuição em outro produto. Não podemos empacotá-lo, e depender de o usuário instalar
   por fora quebra a promessa de portátil que o app faz.
3. **Não precisamos.** A alavanca que resolve o caso do usuário (§4) é a nossa prioridade de
   GPU, que pega ao vivo, é reversível e não toca em processo alheio.

---

## 7. O teste que prova o módulo

`scratchpad\otimizar-desempenho\teste-desempenho.cpp`, compilado contra o `desempenho.h` real:

```
g++ -std=c++17 -O2 -municode -mconsole -Wall -Wextra \
    -I "C:/Users/PCDOFAFA/Desktop/dlss5 for all portable/amd" \
    teste-desempenho.cpp -o teste-desempenho.exe -lgdi32 \
    -static -static-libgcc -static-libstdc++
```

**56 verificações, 56 passaram** (saída completa em `teste-desempenho-saida.txt`). O que elas
cobrem, do mais importante para o menos:

- **Aplicar e restaurar de verdade.** Depois de `LimiteJogoAplicar(60)`, uma **sessão nova** do
  driver lê 60 no perfil do alvo; depois de restaurar, lê "nenhum". Sessão nova é o que
  importa: prova que a gravação saiu do nosso processo e chegou ao driver.
- **Recuperação depois de uma queda.** O teste escreve à mão no ini o estado "o app morreu com
  um limite pendurado" e chama `Iniciar()`. O perfil é desfeito e o ini fica limpo.
- **O perfil criado por nós é apagado inteiro**, não só a chave, para não deixar lixo no painel
  da NVIDIA da pessoa.
- **Trocar 60 → 30 não perde o valor original** guardado.
- **Navegador não recebe limite:** com o Edge como alvo, o painel explica e nada é gravado no
  perfil dele.
- **A escada** em dois roteiros: um em que a prioridade de GPU já resolve (para no primeiro
  degrau, não mexe na resolução nem no jogo) e outro em que nada resolve ao vivo (desce
  100 → 80 → 67 e termina no limite do jogo, com o resumo pedindo para reabrir).
- **Um degrau que piora é desfeito** e o anterior fica de pé: quando 67% derruba a película, a
  resolução volta para 80% — não para 100% — e o resumo cita o fps que ficou valendo, não o do
  degrau ruim.
- **`Encerrar()` devolve o driver como estava**, e ao fim nenhum perfil nosso sobrou.

O teste não abre jogo nem camada: o DRS trabalha por nome de exe, então a prova de
aplicação/restauração não precisa de nada rodando, e a escada é alimentada com fps sintético,
o que a torna determinística. Os números de desempenho da §3 vieram do medidor, que abre tudo.

---

## 8. Decisão

**Automático onde funciona ao vivo; manual, com aviso, onde só vale na reabertura.**

| alavanca | como entra |
|---|---|
| prioridade de GPU da película | **automática**, sem perguntar nada. Pega ao vivo, é reversível, resolve o caso do usuário |
| jogo igual à película | **padrão com o 3x ligado**, porque o 3x já trava a película em 30. Grava para a próxima abertura e diz isso |
| jogo em 60 / 30 / livre | **chips manuais** na coluna "Mais opções", para quem quiser mandar |
| resolução do DLSS 80 / 67 | só dentro do botão "Otimizar desempenho", nunca sozinha: custa nitidez |

O botão **"OTIMIZAR DESEMPENHO"** existe como plano B explícito: mede 5 s por degrau e só desce
se a película continuar abaixo do alvo. Ele não é o caminho principal, porque o degrau mais
forte que sobra depois da prioridade de GPU (o limite do jogo) não pode mostrar resultado na
hora — e um botão automático que termina em "reabra o jogo para ver" é uma promessa pela metade.

### O que fica limitado

- **O limite do jogo nunca vale na hora.** Nem aplicar, nem tirar. Toda mensagem sobre ele diz
  isso, com essas palavras.
- **Em AMD não há limite de jogo nenhum** até alguém escrever o caminho ADLX.
- **Vídeo em navegador não tem limite**, por escolha.
- **Os números da §3 são de um jogo de teste**, não do Resident Evil, e numa máquina disputada
  por vários processos. A ordem entre as estratégias é sólida; os valores absolutos não são
  promessa.
- **"Igual à película" não sobe o fps da película** (§3.1). Ele libera a placa e corta latência.
  O painel não promete fps por causa dele.

---

## 9. Fontes

- Medições próprias: `scratchpad\otimizar-desempenho\medidor.cpp`, saídas
  `medidor-stdout3.txt` (rodada 3) e `medidor-stdout4.txt` (rodada 4); `prio-probe.cpp` e
  `prio-probe-baixo.txt` para a questão do privilégio.
- Teste do módulo: `scratchpad\otimizar-desempenho\teste-desempenho.cpp`, saída
  `teste-desempenho-saida.txt`.
- IDs das funções NvAPI e layout dos structs DRS: `nvapi-open-source-sdk` (a forma pública do
  `nvapi.h`), conferida contra o comportamento do driver desta máquina — todas as 17 funções
  resolveram e `GetSetting`/`SetSetting` responderam `NVAPI_OK`, o que só acontece se o
  `sizeof` dos structs bater com o que o driver espera.
- `D3DKMTSetProcessSchedulingPriorityClass` e `D3DKMTQueryAdapterInfo` (`KMTQAITYPE_WDDM_2_7_CAPS`,
  valor 70): documentação de kernel do WDDM; comportamento confirmado por medição aqui.
- Integração no app: `INTEGRACAO-DESEMPENHO.md`, nesta mesma pasta.
