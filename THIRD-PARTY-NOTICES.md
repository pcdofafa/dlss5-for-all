# THIRD-PARTY NOTICES — DLSS5 For All

Este arquivo lista código de terceiros **copiado ou adaptado** para dentro do nosso fonte e o
aviso de licença que a licença exige manter. Créditos gerais (Feeder, ReShade, FidelityFX, etc.)
continuam em `CREDITOS.md` / `CREDITS.md`.

---

## NeuralScreen — snapshot MIT (commit `5256f2b`, 16/09/2026)

- Origem: `github.com/perseval-BLR/NeuralScreen`, **commit `5256f2b`** (o último sob MIT; da v1.13.0,
  commit `503618e`, em diante o projeto é PolyForm Strict 1.0.0 e **nada** dessa fase entra aqui).
- O que foi adaptado:
  - **Exposição adaptativa** — `native/dlss5-feed-host64.cpp`, função `UpdateAdaptiveExposure`
    (v1.5.6): o mapeamento `smoothstep(dark=0,10; lit=0,40)` da luminância média para
    `DLSS.Exposure.Scale` entre 1,10 e 1,00, a EMA por constante de tempo (τ = 0,5 s) e o par de
    parâmetros NGX `DLSS.Pre.Exposure` / `DLSS.Exposure.Scale`.
  - Onde está: `src\feeder-amd\src-feeder\src\dlss5-feed.cpp`, bloco `NS-2309 item 3`
    (`ExpoQuadro`, `ExpoEnsure`, `ExpoNoMapa`), atrás da chave `exposicao_adaptativa` (padrão 0).
    Cada trecho leva o comentário "derivado de NeuralScreen <commit 5256f2b>, MIT".
- Ideias observadas na fase **PolyForm** (v1.13.0+) foram **reimplementadas do zero**, sem copiar
  texto de código, e estão marcadas no fonte com "ideia observada no NeuralScreen (PolyForm), código
  próprio": filtro de janelas `cloaked`/minúsculas/fora da imagem, uma instância da rede por passe,
  sonda da rede num processo descartável e as conferências de §5.6 (ver
  `relatorios\OCLUSAO-2309.md` §25).

Texto da licença (de `git show 5256f2b:LICENSE`):

```
MIT License

Copyright (c) 2026 perseval-BLR

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Nota do próprio snapshot: o `nvngx_dlssnr.dll` que eles distribuíam **não** é coberto por essa
licença (é da NVIDIA). Nós não distribuímos essa DLL.

---

## Rodada MGR-2309 (24/09/2026) — só ideias, nenhum código copiado

Nesta rodada **nenhum texto de código** de terceiros entrou no fonte. Tudo foi escrito do zero; o
comentário no fonte diz de onde veio cada ideia (`relatorios\OCLUSAO-2309.md` §26). Registro para
rastreio, não como obrigação de licença:

| origem | licença | o que veio (como ideia ou fato técnico) |
|---|---|---|
| DLSS5 Autopilot | MIT, "Copyright (c) 2026 DLSS 5 Autopilot contributors" | casamento de anti-cheat por palavra inteira (#187); vírgula decimal no cfg (#69); lançar sem herdar o diretório de DLLs (#287) |
| dlss5-anywhere (Won.Cafe) | MIT, "© 2026 Won.Cafe" | nomes das chaves `DirectNeuralRenderingHookMethod`/`ProcessingScale`; `NGXCore\EnableOTA`; aviso de ASI loader; marcador `DLSS-NR direct: attached snippet`; ordem de encerramento |
| NeuralScreen (`dll_trust.h`) | MIT (fase até `5256f2b`) | checagem Authenticode só como log (WinVerifyTrust escrito do zero) |
| DLSS 5 Manager (NODIX) | todos os direitos reservados | só ideias: histerese + fração coberta; captura da imagem processada; tom local 0 nos passes 2+. Nenhum arquivo deles foi aberto na hora de escrever |
