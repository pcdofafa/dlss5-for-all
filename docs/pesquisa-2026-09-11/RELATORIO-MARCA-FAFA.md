# Relatório — marca "PC do Fafa" / "FAFA" sem acento

Varredura de 11/09/2026. A marca do canal é **PC do Fafa** / **FAFA**, sempre sem acento.
Este documento registra a correção das grafias acentuadas nas pastas de escopo, o que ficou
de fora e por quê.

Observação sobre as contagens deste arquivo: o próprio relatório cita a grafia errada dezenas
de vezes, por necessidade. Um grep de verificação sobre a pasta `pesquisa-2026-09-11` vai
acusá-lo. Ele está excluído de todos os números abaixo.

## Números

| | Arquivos | Ocorrências |
|---|---|---|
| Antes | 31 | 118 |
| Corrigidas por esta varredura | 21 | 87 |
| Corrigidas pelos donos durante a janela | 3 | 16 |
| Restantes | 7 | 15 |

Backup dos 21 arquivos, com a árvore relativa preservada, em
`C:\Users\PCDOFAFA\AppData\Local\Temp\claude\marca-fafa-backup-054124\`.

Todos os arquivos tocados eram UTF-8 sem BOM com quebra de linha LF. A regravação usou
`[IO.File]::WriteAllText` com `UTF8Encoding($false)`. A conferência byte a byte contra o
backup confirmou que os 21 arquivos mantiveram a contagem de CR, a de LF e a ausência de BOM.
Nenhuma URL, chave, e-mail, nome de arquivo ou identificador foi tocado: as linhas com
`@pcdofafa`, `pcdofafa.com.br`, `grupo.pcdofafa.com.br` e `fafa-dlss5-app` já eram sem acento
e continuam idênticas.

### Armadilha encontrada no caminho

A primeira passada usou o operador `-replace` do PowerShell, que é **case-insensitive por
padrão**. A classe `[á]` casou também com `Á`, então `marca=PC DO FAFÁ` virou
`marca=PC DO Fafa` em vez de `PC DO FAFA`, e de forma não uniforme entre os arquivos. O erro
foi pego pelo diff byte a byte contra o backup, não pelo grep, que já dava zero.

A correção restaurou os arquivos do backup e refez a troca com
`[Regex]::Replace` e `RegexOptions::None`, usando um `MatchEvaluator` que decide entre `A` e
`a` pela caixa do caractere acentuado que estava ali. Quem repetir este trabalho deve usar
`[Regex]` explícito, nunca `-replace`. O detalhe soma-se ao que já se sabia sobre `sed -i` do
MSYS converter CRLF em LF.

## Arquivos alterados

### `ofertas.ini` — 16 cópias, 75 ocorrências

Em cada cópia completa, as cinco ocorrências são as mesmas: o comentário do cabeçalho
(linha 2), `marca=PC DO FAFÁ` (30), `nome_grupo=PC do Fafá Promoções` (45), o
`nome_grupo_en` comentado (60) e a `mensagem` comentada (68).

| Arquivo | Ocorrências |
|---|---|
| `DLSS5ForAll-AMD\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-amd\ofertas.ini` | 3 |
| `DLSS5ForAll-AMD\bin-camada2\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-camada3\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-botoes\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-fpsavancado\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-icone\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-passes\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-passes-fix\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-requiem\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-residual\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-robustez\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-robustez\camada2\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\bin-teste-sintetizar\ofertas.ini` | 5 |
| `DLSS5ForAll-AMD\launcher-teste\ofertas.ini` | 2 |

`bin\` estava livre. O único `DLSS5ForAll.exe` em execução durante a varredura, PID 27756,
roda a partir de `bin-teste-requiem\`, não de `bin\`. Nenhum processo foi encerrado.

### Documentação e memória — 12 ocorrências

| Arquivo | Ocorrências | Natureza |
|---|---|---|
| `amd\docs\polimento-produto.md` | 4 | item 28 reescrito, ver abaixo |
| `amd\docs\pesquisa-2026-09-11\RELATORIO-LAUNCHER-PROFISSIONAL.md` | 5 | rodapé e tabela de tradução |
| `amd\docs\pesquisa-2026-09-11\RELATORIO-BOTOES.md` | 1 | nome do botão na tabela de verificação |
| `amd\build-mingw.sh` | 1 | comentário do `--codepage=65001`, ver abaixo |
| `...-bin-teste-passes\memory\feeder-nao-derruba-ngx-alheio.md` | 1 | menção corrente à marca |

Duas dessas alterações não foram substituição mecânica, porque a troca literal deixaria o
texto falso.

**`build-mingw.sh`, linha 38.** O comentário justificava o `--codepage=65001` dizendo que o
`app.rc` "tem acento no CompanyName (\"PC do Fafá\")". Trocar só o nome produziria uma frase
que afirma haver acento e exibe um exemplo sem acento. O comentário passou a falar de campos
de texto acentuados em geral, sem citar a marca. A justificativa técnica do flag continua
válida. Vale registrar que o `CompanyName` era o **único** acento do `app.rc`: quando ele for
corrigido, o flag deixa de ter efeito prático, embora continue correto mantê-lo.

**`polimento-produto.md`, item 28, linha 46 e seção dos BAIXOS.** O item registrava como
defeito o fato de o menu da bandeja escrever "PC do Fafa" sem acento e prescrevia
`→ L"Grupo de ofertas — PC do Fafá"`. Isso está invertido em relação à regra da marca: era a
recomendação que reintroduzia o acento. O item foi reescrito para dizer que a bandeja já está
certa e que o acento deve sair da splash, do painel e dos arquivos de recurso. A parte
legítima do item, a unificação do travessão, foi preservada.

## Ocorrências NÃO alteradas

### Arquivos de outros agentes — 8 ocorrências, repassar aos donos

Nenhum arquivo desta lista foi tocado.

| Arquivo:linha | Conteúdo |
|---|---|
| `amd\main.cpp:3211` | comentário sobre as duas grafias no menu |
| `amd\main.cpp:3213` (x2) | `T(L"Grupo de ofertas — PC do Fafá", L"Deals group — PC do Fafá")` |
| `amd\main.cpp:6972` (x2) | o mesmo par de strings, no painel |
| `amd\app.rc:7` | comentário de codificação |
| `amd\app.rc:45` | `VALUE "CompanyName", "PC do Fafá"` |
| `amd\vitrine.h:222` | `function acento(t) { return t.replace(/Fafa(?![0-9A-Za-z_])/g, 'Fafá'); }` |

**Prioridade: `vitrine.h:222`.** Essa função reinsere o acento por código, em qualquer texto
que passe por ela. Enquanto existir, corrigir os dados de origem não adianta para a vitrine
embutida no app: ela volta a exibir "PC do Fafá" a cada render. Deve ser removida, não
ajustada. É a única ocorrência do escopo que reintroduz o erro ativamente.

O worker do site, em `fafa-attribution\workers\bridge-page\src\`, já está do lado certo: o
`index.js:1131` traz a regra escrita como comentário e o `app-ofertas.test.js:105` tem uma
asserção `!/Faf[áÁ]/.test(html)` que reprova a página se o acento voltar. A observação de
"PC do Fafá acentuado" registrada no `RELATORIO-DEPLOY-CLOUDFLARE.md` é anterior a essa
correção. Vale conferir uma captura nova da vitrine para fechar o assunto.

### Corrigidos pelos próprios donos durante a varredura — 16 ocorrências

Estavam acentuados no levantamento inicial e já apareceram limpos na verificação final.
Nada a fazer, registrados só para fechar a contagem.

| Arquivo | Ocorrências que tinha |
|---|---|
| `amd\splash_ofertas.h` | 10 |
| `amd\ofertas.ini` | 5 |
| `amd\portable-launcher\launcher.rc` | 1 |

Atenção: o `amd\ofertas.ini` é a fonte de onde descem as 16 cópias. Agora que ele e as cópias
estão alinhados, um novo empacotamento não reintroduz o acento.

### `fafa-attribution` — 365 ocorrências, somente listadas

Sem nenhuma edição, conforme combinado. As ocorrências não estão em lógica de aplicação.

| Arquivo | Ocorrências |
|---|---|
| `data\audit.jsonl` | 47 |
| `data\video_reply\cxjIGtErLq0.json` | 9 |
| `data\video_reply\cxjIGtErLq0_ontem.json` | 9 |
| `data\video_reply\sem_resposta.txt` | 5 |
| `data\video_reply\pares_respondidos.txt` | 3 |
| `data\video_reply\novos_e_replies.txt` | 1 |
| `data\attribution.db` | 3 |
| `data\comment_agent.db` | 3 |
| `data\backups\*.db` | 278 em 94 snapshots |
| `workers\bridge-page\src\index.js` | 1 |
| `workers\bridge-page\src\app-ofertas.test.js` | 2 |
| `workers\bridge-page\.wrangler*\**\index.js` | 4 em 4 arquivos de build |

Os arquivos de `data\` são registro do que já foi publicado: descrições e comentários reais
de vídeos do YouTube, com hashtags como `#PCdoFafá`, `#FafáOfertas`, `#FafáPromo` e
`#pcdofafá`, além de briefings de criativos. Corrigi-los falsificaria o histórico. O que vale
corrigir é a origem, ou seja, as hashtags usadas em publicações novas. Os `.db` em
`data\backups\` são 94 snapshots do mesmo banco e repetem o mesmo conteúdo.

As 7 ocorrências em `workers\` são a regra e o teste que a defendem, descritos na seção
anterior, mais quatro cópias geradas pelo build do Wrangler. Não há grafia acentuada errada
em código nessa pasta.

### Citações deliberadas da grafia errada — 7 ocorrências, mantidas de propósito

Trocar qualquer uma destas destruiria a informação que elas carregam.

| Arquivo:linha | Por que fica |
|---|---|
| `memory\feedback-fafa-sem-acento.md:14` | cita `"Fafá"` como a forma a evitar; é o conteúdo da regra |
| `memory\feedback-fafa-sem-acento.md:19` (x2) | é o padrão de busca `Fafá\|FAFÁ` que a própria regra manda usar |
| `memory\MEMORY.md:9` | `a marca é "PC do Fafa"/"FAFA", nunca "Fafá"` |
| `memory\powershell-51-le-ps1-sem-bom-como-ansi.md:13` | exemplo de mojibake, `Fafá` vira `FafÃ¡` |
| `RELATORIO-DEPLOY-CLOUDFLARE.md:121` | registro do título servido pelo site no ar naquela data |
| `RELATORIO-DEPLOY-CLOUDFLARE.md:122` | registro do cabeçalho servido, descrito como "acentuado" |

As duas linhas do relatório de deploy são observação verificada do que o servidor devolveu
em 11/09/2026. Não é a marca sendo escrita, é a evidência de um estado a corrigir.

## Verificação final

Padrão `Fafá|FAFÁ|fafá`, mais as variantes `à` e `â`, nas pastas de escopo, depois das
correções e sem contar este relatório:

```
C:\...\memory\feedback-fafa-sem-acento.md                       n=3   citação da regra
C:\...\memory\MEMORY.md                                         n=1   citação da regra
C:\...\memory\powershell-51-le-ps1-sem-bom-como-ansi.md         n=1   exemplo de mojibake
C:\...\amd\docs\pesquisa-2026-09-11\RELATORIO-DEPLOY-CLOUDFLARE.md  n=2   registro factual
C:\...\amd\app.rc                                               n=2   outro agente
C:\...\amd\main.cpp                                             n=5   outro agente
C:\...\amd\vitrine.h                                            n=1   outro agente, reinjeta

7 arquivos, 15 ocorrências
```

Fecha a conta: 21 arquivos corrigidos aqui, 3 corrigidos pelos donos e 7 restantes somam os
31 arquivos do levantamento inicial.

Nenhuma ocorrência restante está em arquivo que esta varredura podia editar e deixou passar.

## Fora do escopo, não varrido

- `DLSS5ForAll-Portable\` — proibido.
- `fafa-bot-core` — somente leitura, não listado.
- Binários (`.exe`, `.dll`, `.addon64`, `.ico`, `.png`, `.zip`, `.dmp`) em qualquer pasta.
