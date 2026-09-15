# Ícone novo do DLSS5 For All — a capivara

**Data:** 11/09/2026
**Arte de origem:** `amd\assets\icone\capivara-original.png` (1254x1254, capivara meio pixel-art
meio realista, moldura azul neon arredondada)

O ícone antigo era derivado da foto do autor. O novo é derivado da arte da capivara, pelo mesmo
princípio: um script gera os nove tamanhos, ninguém redimensiona nada à mão.

---

## 1. O que mudou

| Arquivo | O que aconteceu |
|---|---|
| `amd\marca\gerar-icone-capivara.ps1` | **novo** — gerador oficial do ícone |
| `amd\marca\DLSS5ForAll.ico` | **substituído** (9 tamanhos, 251 KB) |
| `amd\marca\DLSS5ForAll-antigo-20260911.ico` | backup do ícone anterior, ao lado |
| `amd\marca\icone-png\dlss5forall-*.png` | regerados (16, 20, 24, 32, 40, 48, 64, 128, 256) |
| `amd\marca\icone-png-antigo-20260911\` | backup dos PNGs anteriores |
| `amd\marca\capivara-512.png`, `capivara-256.png` | **novos** — para o launcher e o site |
| `amd\build-mingw.sh` | o aviso de "ícone faltando" agora aponta o script novo |
| `amd\empacotar.ps1` | idem, na dica de diagnóstico |
| `amd\main.cpp` | só o comentário do `IconeApp()` apontando o script novo |
| `amd\marca\gerar-icone.ps1` | **mantido** como histórico (gera o ícone da foto antiga) |

**Nada mudou nos `.rc`, e essa é a notícia boa.** `amd\app.rc` (linha 20) e
`amd\portable-launcher\launcher.rc` (linha 10) já apontavam para o **mesmo**
`marca\DLSS5ForAll.ico` com id numérico `1`. Trocar o arquivo trocou o ícone dos dois exes.
Os `.rc` do feeder (`feeder-amd\version-amd.rc` e `src-feeder\src\version.rc`) só têm
VERSIONINFO — são DLLs/add-ons, não têm ícone e não deveriam ter.

**Bandeja e janela já vinham do mesmo recurso.** Conferido em `main.cpp`:

- `IconeApp(px)` (linha 103) chama `LoadImageW(..., MAKEINTRESOURCEW(1), IMAGE_ICON, px, px, ...)`,
  ou seja, escolhe a **imagem** do tamanho certo dentro do `.ico`, em vez de `LoadIcon`, que
  devolveria sempre a de 32 px e a deixaria borrada na bandeja.
- Bandeja: `main.cpp:3239` (`nid.hIcon = IconeApp(SM_CXSMICON)`).
- Janela: `main.cpp:11945-11946` e `seletor_hud.h:1154-1155` (`hIcon` / `hIconSm`).

Não há PNG, bitmap ou logo embutido em nenhum `.rc`, e nem `splash_ofertas.h` nem `vitrine.h`
carregam imagem de marca de arquivo — a linha do grupo de ofertas no painel desenha uma barrinha
ciano de propósito (`main.cpp:7019`), sem imagem. Logo, não havia mais nenhum lugar onde o ícone
velho ainda se apresentasse.

---

## 2. Como o `.ico` foi gerado

Tudo em `System.Drawing` pelo PowerShell, sem baixar ferramenta nenhuma.

1. **Redução:** `InterpolationMode = HighQualityBicubic`, com `PixelOffsetMode = HighQuality` e
   `WrapMode = TileFlipXY` no `ImageAttributes`. O wrap mode não é detalhe: sem ele o bicúbico
   busca pixels fora da borda do recorte e deixa uma linha fantasma clara em volta do ícone.
2. **Cantos transparentes (alfa de verdade):** a arte tem a moldura arredondada, mas fora dela
   sobra o fundo quase preto (`#040d18`). O script desenha uma máscara de retângulo arredondado
   **4x maior** e a reduz com bicúbico, depois multiplica o alfa da imagem pelo alfa da máscara.
   `Graphics.SetClip` não serviria: ele recorta com borda dura mesmo em modo AntiAlias, e a 16 px
   a escadinha aparece. Conferido nos PNGs: alfa 0 nos quatro cantos de 32 px para cima.
3. **Formato do arquivo:** até 64 px cada bloco vai em BMP de 32 bits (que todo shell do Windows
   lê); 128 e 256 vão em **PNG** comprimido. Estrutura final conferida byte a byte:

```
entradas: 9
  16x16   BMP     1128 bytes
  20x20   BMP     1720 bytes
  24x24   BMP     2440 bytes
  32x32   BMP     4264 bytes
  40x40   BMP     6760 bytes
  48x48   BMP     9640 bytes
  64x64   BMP    16936 bytes
 128x128  PNG    46156 bytes
 256x256  PNG   168152 bytes
```

Os sete tamanhos pedidos (256, 128, 64, 48, 32, 24, 16) estão todos lá. 20 e 40 entraram junto
porque o `IconeApp()` os pede de verdade: a bandeja do Windows usa 20 px a 125% de DPI e 24 px a
150%, e o ícone médio do Explorer usa 40 px.

### A geometria foi medida, não chutada

A moldura neon da arte foi localizada pixel a pixel: o centro do traço fica em x=38 e x=1212,
y=37 e y=1202, com raio de canto de ~176 px em 1254. É daí que saem os três enquadramentos que o
script conhece (`$Enquadramentos`).

### O recorte mais fechado para 16/24/32 — as duas opções, lado a lado

Comparação ampliada 8x, metade sobre fundo escuro e metade sobre fundo claro para julgar o alfa:
**`icone\comparacao-enquadramentos.png`**.

| Enquadramento | O que é | Como lê a 16 px |
|---|---|---|
| `inteira` | a arte toda, só com os cantos recortados | a moldura come a borda; o bicho fica pequeno |
| `moldura` | corta o halo escuro externo, a moldura encosta na borda | **escolhido** — ~8% mais de área útil, a silhueta arredondada azul continua reconhecível |
| `rosto` | fecha na cara (olho pixel-art + focinho realista) | o focinho lê bem a 24/32, mas a 16 vira um borrão marrom e a identidade da moldura some |

**Decisão: `moldura` para 16, 20, 24 e 32 px; `inteira` de 40 px para cima.** O `rosto` foi
descartado porque a graça do desenho é a divisão pixel-art/realista, e fechar na cara a 16 px
entrega um borrão que poderia ser qualquer bicho marrom. Trocar é uma linha:
`-Pequenos rosto` na chamada do script.

Os PNGs de 512 e 256 para o launcher e o site usam sempre o enquadramento `inteira`, onde o halo
azul externo cabe e fica bonito.

---

## 3. Compilação e conferência

Compilados com o MinGW-w64 do msys2, pelos scripts do próprio projeto:

- `amd\build-mingw.sh` (com `SKIP_ADDON=1`, porque o ícone não toca no add-on da ponte e
  recompilá-lo sobrescreveria o `.addon64` que outra pessoa pode estar testando)
- `amd\portable-launcher\build-launcher.sh`

| Binário | SHA256 |
|---|---|
| `amd\DLSS5ForAll.exe` (2.891.908 bytes) | `1CF25069F8C5B55E5D7B33B14F5F97B9BF0E7CD278CFB1C039797AC72A15C6DD` |
| `amd\portable-launcher\DLSS5ForAll.exe` (2.140.414 bytes) | `3EA11F7F4E9B2729151FD956EEF437E3B4DF7790DBA4F8D7C26049FE4961C48E` |
| `amd\marca\DLSS5ForAll.ico` (257.346 bytes) | `AD35F9DD23F83305F8911AC15C676FF3232220C934F25BA521BEF6B6BE0E9D4E` |

**Ícone extraído dos exes já compilados** (não do `.ico` solto — é o recurso que está dentro do
binário), via `PrivateExtractIconsW` em 16/24/32/48/64/128/256 e desenhado com `DrawIconEx`, que
respeita o canal alfa (`Bitmap.FromHicon` achataria):

- `icone\icone-extraido-app.png`
- `icone\icone-extraido-launcher.png`

**Prints do Explorer**, pasta `bin-teste-icone\` em ícones extra grandes (96 px), no monitor
secundário, com `SetProcessDpiAwarenessContext(-4)`:

- `icone\explorer-bin-teste-icone.png` — o `DLSS5ForAll.exe` com a capivara, e a própria pasta
  `launcher` já mostrando a miniatura do ícone novo
- `icone\explorer-launcher.png` — o exe do launcher

Nota de método: a primeira captura saiu com uma janela de outro processo por cima (um HUD de fps
do app em teste). Em vez de matar processo alheio, a captura passou a usar `PrintWindow` com
`PW_RENDERFULLCONTENT`, que manda o próprio Explorer redesenhar num DC nosso — o que estiver na
frente não entra na imagem.

---

## 4. Pasta de teste

`DLSS5ForAll-AMD\bin-teste-icone\` é cópia da `bin\` feita com `Copy-Item` (nunca link), só para
conferência visual no Explorer. Ficaram de fora os dois modelos `nvngx_*.dll` (224 MB) e as
pastas `reshade-shaders\` / `reshade-cache\`: não têm relação com o ícone e só duplicariam disco.
São 59 arquivos, 18,3 MB. O exe principal e o launcher (em `launcher\`) são os recém-compilados.

**A `bin\` de produção não foi tocada.** A `DLSS5ForAll-Portable\` também não.

---

## 5. O que falta

1. **Promoção para `bin\`** — de quem coordena. Os binários prontos estão em
   `amd\DLSS5ForAll.exe` e `amd\portable-launcher\DLSS5ForAll.exe`.
2. **Empacotamento** — `empacotar.ps1` confere se o exe tem grupo de ícone embutido, então o
   pacote novo já sai com a capivara. Nada a fazer além de rodar.
3. **Site / vitrine — nada a fazer, e isto é decisão tomada.** A capivara é o ícone **do
   programa**; a foto do autor continua sendo a marca **do canal** no site. Por isso
   `marca\gerar-icone.ps1` e `marca\logo-site-200px.jpg` ficaram intactos (conferido: nenhum dos
   dois foi escrito nesta rodada), e o cabeçalho de marca do `grupo.pcdofafa.com.br` não muda.
   `capivara-512.png` e `capivara-256.png` existem para o **launcher e o pacote**, não para
   substituir a logo do canal — quem for usá-los, use nesse escopo.
