# Rodar o Neural Rendering em resolução menor e reconstruir: quanto custa, quanto rende

Data: 2026-09-06. Máquina: RTX 4080 (driver 596.49), Windows 11 26200, monitor ultrawide
3440×1440 a 150 %. Código medido: `amd\main.cpp` compilado em 06/09 às 02:44, com o
**DLSS5-Feeder 0.12.0** (`scratchpad\feeder-012\src-nv\build\dlss5-feed-nvidia-0.12.0.addon64`),
que é a versão que tem as chaves `work_upscale` e `work_sharpness`.
Tudo rodou numa cópia isolada em `scratchpad\upscale-nr\app`; o pacote real nunca foi tocado.

> RASCUNHO EM ANDAMENTO — as tabelas de fps com jogo saturando a GPU ainda estão sendo
> medidas. Este arquivo é substituído pela versão final ao término.

---

## 1. A pergunta, traduzida

O usuário perguntou se dá para deixar o DLSS 5 mais leve aplicando-o "em cima do FSR" ou "em
cima da camada que lê o DLSS dentro do jogo", e se isso viraria uma caixinha de marcar que dá
desempenho de verdade.

Tecnicamente há **duas ideias diferentes** dentro dessa pergunta, e elas têm respostas
opostas:

1. **Rodar a rede em resolução menor e reconstruir com um upscaler barato na volta.**
   Isto é o `work_resolution` + `work_upscale` do Feeder. **Funciona, e o ganho é grande.**
2. **Empilhar a nossa camada em cima do DLSS que o jogo já roda.**
   Isto **não economiza nada** — o custo da nossa camada depende só da resolução, não do
   conteúdo — e **piora a imagem**, porque são dois filtros temporais em série.

O resto do relatório mede as duas.

---

## 2. Como o custo da nossa camada se comporta

A camada processa **área**. Numa janela de 3277×1368 (4,5 megapixels, um ultrawide realista)
a rede vê 4,5 milhões de pixels por quadro; a 58 % de cada eixo ela vê 1,5 milhão — 34 % da
conta. O caminho, no Feeder 0.12.0, é:

```
quadro nativo  ->  reamostra para o tamanho de trabalho  ->  DLAA + Neural Rendering
               ->  volta ao tamanho nativo  ->  backbuffer
```

e a chave `work_upscale` escolhe **como** é a volta:

| valor | o que faz na volta | custo |
|---|---|---|
| 0 | esticão bilinear | quase zero |
| 1 | **FSR 1 da AMD (EASU + RCAS)**, um filtro espacial adaptativo a bordas | uma passada de tela cheia |
| 2 | DLSS Super Resolution de verdade, com jitter sintético (experimental) | ver §5 |

`work_sharpness` é a força do RCAS (0 a 1), que só roda com `work_upscale` 1 ou 2.

---

## 3. O instrumento (e por que o fps sozinho não serve)

Medir isto com uma fonte de vídeo a 60 fps **não funciona**: a fonte é o teto e a RTX 4080
não satura. Na primeira rodada, com a janela de 3277×1368, o app entregou 58,2 fps com
`work_resolution=100` e 56,6 fps com 58 % — nenhuma diferença, porque em nenhum dos dois casos
a placa era o gargalo (uso da GPU em 35 %).

O que discrimina é **quanto tempo de GPU o nosso processo gasta por quadro**, medido pelo
contador `\GPU Engine(pid_<nosso pid>*)\Utilization Percentage` do Windows, que é por
processo e por isso não se confunde com os outros agentes que estavam usando a máquina.
Multiplicado pelo período do quadro, dá milissegundos de GPU por quadro.

Depois, para transformar milissegundos em fps, a segunda bateria roda um **jogo que satura a
GPU sem vsync** (`jogo-carga.exe`, cópia do `jogo-teste.exe` do agente otimizar-desempenho com
outro título de janela) e lê o **fps do jogo**: cada milissegundo que a nossa camada gasta é
um milissegundo que o jogo não tem.

Métricas de imagem, em PNGs do backbuffer gravados pelo `DLSS5_DUMP` (que é pós-ReShade):

- **detalhe** — laplaciano horizontal médio, `|2L(x) − L(x−1) − L(x+1)|`. Responde a
  frequência alta, ou seja, à textura fina. É exatamente o que a resolução de trabalho menor
  joga fora e o que o upscaler da volta tenta devolver.
- **nitidez** — `|dL/dx|` médio. Sobe com realce de borda (RCAS) mesmo quando o detalhe real
  não voltou, por isso as duas andam juntas na tabela.
- **efeito** — diferença média contra a saída **sem** a nossa camada, depois de borrar as duas
  (raio 4). O borrão apaga a perda de alta frequência e deixa a mudança de tom e contraste
  local, que é o que o Neural Rendering faz e o que o usuário mandou nunca enfraquecer.

---

*(seções 4 a 9 em preenchimento: matriz de imagem, matriz de fps com jogo, alternativas de
upscaler, a ideia de empilhar sobre o DLSS do jogo, proposta de interface e passo a passo de
integração)*
