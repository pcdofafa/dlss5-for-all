# Como contribuir com o DLSS5 For All

Contribuição é bem-vinda — correção de bug, tradução, shader, documentação, o que for.
Este arquivo diz como mandar, e o que **nunca** pode vir junto.

---

## ⚠️ Antes de tudo: o que NÃO pode entrar num pull request

Este repositório é público. **Nenhum PR pode incluir binário de terceiro**, em nenhuma
forma — solto, dentro de um `.zip`, renomeado, codificado em base64, baixado por um
script de build para dentro da árvore. Especificamente:

| Arquivo | De quem é | Por que não pode |
|---|---|---|
| `nvngx_dlssnr.dll` | NVIDIA | Build **interno** que vazou. Redistribuir é DMCA na certa — para o projeto e para quem hospeda. |
| `nvngx_dlss.dll` | NVIDIA | Proprietário. A licença da NVIDIA não nos dá direito de redistribuir. |
| `renodx-dlss5.addon64` | projeto RenoDX | Binário deles. Quem quiser pega nas releases oficiais do RenoDX. |
| `dxgi.dll` | ReShade | É o ReShade renomeado. Quem distribui ReShade é o instalador oficial. |

O `.gitignore` da raiz já bloqueia esses nomes, mas **o `.gitignore` não é a defesa, é o
lembrete**. PR que traga qualquer um deles é fechado sem análise. Se aconteceu sem
querer, não basta apagar o arquivo num commit novo: reescreva o branch
(`git rebase -i`, ou `git reset` + commit limpo) e mande de novo — o que o Git guarda no
histórico é tão público quanto o que está na ponta.

O que este projeto distribui é a **receita**: código, shaders, presets e o
`setup-dlss5.ps1`, que faz o próprio usuário baixar cada peça da fonte oficial dela.
Nunca os ingredientes de terceiros.

Junto com isso: nada de segredo no PR. Token, senha, `.env`, `.dev.vars`,
`ADMIN-TOKEN.txt` — tudo isso fica fora, e o backend lê os valores de
`npx wrangler secret put`.

---

## O fluxo

1. **Fork** deste repositório na sua conta.
2. **Branch** a partir da `main`, com nome que diga o que é:

   ```bash
   git checkout -b conserta-mv-em-borderless
   ```

3. **Commit** com mensagem que explique o *porquê*, não só o *o quê*. Português ou
   inglês, tanto faz. Commits pequenos e coerentes são bem mais fáceis de revisar do
   que um commitão de 40 arquivos.
4. **Pull request** contra a `main`, descrevendo:
   - o que mudou e por quê;
   - como você testou (qual jogo/fonte, qual GPU, qual resolução);
   - print ou vídeo, se for mudança visual — para nitidez, cor e HDR, o print vale
     mais que qualquer descrição.

**Todo PR passa por aprovação do dono do projeto antes de entrar.** Ninguém tem push
direto na `main`. Isso não é desconfiança: é o que garante que nada do bloco de cima
entre no repositório por descuido, e que o histórico público continue limpo.

Não fique chateado se demorar — isto aqui é projeto de tempo livre.

---

## Como compilar

O app é C++17 puro em cima da API do Win32, sem dependência além do próprio Windows.

**Você precisa de:**

- **Windows 10/11 x64**
- **MinGW-w64 via [msys2](https://www.msys2.org/)** — instale o msys2 e, no terminal
  dele, rode:

  ```bash
  pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils
  ```

  O `build-mingw.sh` procura o compilador em `C:\msys64\mingw64\bin\g++` e coloca essa
  pasta no `PATH` antes de chamá-lo. Se o seu msys2 estiver em outro lugar, exporte
  `CXX` apontando para o seu `g++`.
- **bash** — o do msys2, o do Git for Windows, ou o WSL.

**Compilar:**

```bash
./build-mingw.sh                 # gera DLSS5ForAll.exe + o add-on da ponte
./build-mingw.sh MeuTeste.exe    # com outro nome de saída
SKIP_ADDON=1 ./build-mingw.sh    # só o exe, para iterar rápido
```

O script também compila `app.rc` com o `windres` (ícone + VERSIONINFO). O
`marca/DLSS5ForAll.ico` está versionado; se faltar, o build avisa e segue sem ícone —
dá para regerar com `powershell -ExecutionPolicy Bypass -File .\marca\gerar-icone.ps1`.

O add-on da ponte compila sozinho por `addon/build-addon.sh` (chamado pelo
`build-mingw.sh`). Ele usa os headers do ReShade que estão em `addon/include/reshade/`.

O build sai com `-Wall -Wextra`. **Não mande PR que introduza aviso novo** — os únicos
dois avisos reais que já apareceram aqui eram defeitos de verdade.

**Rodar:** os binários gerados (`DLSS5ForAll.exe`, `dlss5-bridge.addon64`) não são
versionados. Para montar a cadeia completa numa pasta de teste, use o
`setup-dlss5.ps1`, que baixa ReShade, Feeder, LumeniteFX e RenoDX das fontes oficiais.

---

## Estilo

- Siga o que já está no arquivo que você está mexendo. O código aqui é comentado em
  português, com comentários que explicam **por que** algo é daquele jeito — mantenha
  esse hábito, especialmente quando a razão for um bug chato que custou horas.
- Texto de interface passa por `idioma.h`: use `T(L"português", L"english")` no ponto
  de uso, nunca string solta.
- Shader novo em `reshade-shaders/Shaders/`, com prefixo `DLSS5_` se for nosso.
- Nada de reformatação em massa junto com mudança de comportamento — separe em PRs
  diferentes, senão o diff fica impossível de revisar.

---

## Reportar bug

Abra uma *issue* com:

- o que você esperava e o que aconteceu;
- placa de vídeo, driver e versão do Windows;
- fonte usada (monitor / janela / câmera) e resolução;
- os logs: `dlss5forall.log`, `dlss5-feed.log`, `ReShade.log` — eles dizem onde parou.

**Não anexe DLL nenhuma na issue**, pelo mesmo motivo da tabela lá em cima.

---

## Licença

O código deste repositório é MIT (veja `LICENSE`). Mandando um PR, você concorda que a
sua contribuição entra sob a mesma licença.
