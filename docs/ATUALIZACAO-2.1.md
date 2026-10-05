# Atualização do launcher na 2.1: aviso para o site ou troca direta

O launcher consulta `{url_base}/versao.json` (ou `url_manifesto` do `ofertas.ini`). Se a `versao`
do manifesto for maior que a instalada, a tela mostra a pílula "atualização disponível" e, ao clicar
em INICIAR, aparece a caixa "Nova versão disponível" (uma vez por abertura; volta nas próximas
aberturas enquanto houver versão nova). **O programa instalado abre em seguida, sempre**, exceto
quando a troca direta foi aplicada (o launcher sai e o `atualizar.cmd` o reabre).

Quem escolhe o modo é o manifesto, a cada lançamento.

## Campos do manifesto

| campo | uso |
| --- | --- |
| `versao` | **obrigatório**. Compara por partes numéricas ("1.10" > "1.9"). |
| `pagina` | opcional. Só `https` de `pcdofafa.com.br` (ou subdomínio); qualquer outra coisa é ignorada. Padrão embutido: `https://www.pcdofafa.com.br/dlss`. |
| `notas`, `notas_en` | opcionais, até 400 caracteres, aparecem no corpo da caixa. |
| `data`, `tamanho_bytes` | opcionais, vão para o rodapé da caixa (o tamanho só no modo direto). |
| `zip_url` + `zip_sha256` | modo direto, só 2.1+. Os dois juntos (https; 64 hex). |
| `url` + `sha256` | o formato da 1.2. A 1.2 só atualiza com eles. A 2.1+ os usa como zip se `zip_*` faltarem. |
| `minima_para_auto` | opcional; menor versão instalada que atualiza sem perguntar (caixa "automaticamente"). |

Zip incompleto (só a URL, ou hash que não é hexadecimal) não invalida o aviso: cai no modo site e o
`launcher.log` diz o motivo.

## Por que a 1.2 ignora

O parser da 1.2 (`atualizacao.h`, `LerManifesto`, linhas 461-462 no commit `37db60b`) devolve
"manifesto inválido" quando `url` está vazia ou `sha256` não tem 64 hexadecimais, e então não mostra
nada. `pagina`, `zip_url` e `zip_sha256` ela não lê. Só preenchendo `url` + `sha256` a 1.2 enxerga a
atualização (e então baixa o zip e troca, como sempre).

## As três combinações

**1. Só site** (a 1.2 ignora; a 2.1+ abre a página):

```json
{"versao":"2.2","notas":"Corrige o travamento ao abrir.","notas_en":"Fixes the freeze on launch."}
```

**2. Direto só para a 2.1+** (a 1.2 ignora, pois não há `url`/`sha256`):

```json
{"versao":"2.2","zip_url":"https://.../DLSS5ForAll-2.2.zip","zip_sha256":"<64 hex>","tamanho_bytes":48300000,"notas":"..."}
```

`publicar-versao.ps1 -SoDireto21` gera esse formato.

**3. Direto também para a 1.2** (a 1.2 e a 2.1+ baixam e trocam):

```json
{"versao":"2.2","url":"https://.../DLSS5ForAll-2.2.zip","sha256":"<64 hex>","tamanho_bytes":48300000,"notas":"..."}
```

`publicar-versao.ps1` sem `-SoDireto21` gera esse formato (o de sempre). Cuidado: a 1.2 troca os
arquivos com o `atualizar.cmd` dela; só use esta combinação
se o zip for seguro para uma 1.2 instalada (a 2.1 muda a estrutura de pastas: `bin-amd` sai, o mod AMD vai para `binmod-amd`).

## O que o dono faz no servidor (worker `fafa-dlss5-app`)

O manifesto é o mesmo endereço `GET /versao.json`, lido do D1 (`app_config`, `tipo='versao'`). Hoje o
`POST /publicar` recusa (400) qualquer manifesto sem `url` `.zip` e `sha256`, então as combinações 1
e 2 pedem **uma** destas duas saídas:

**A. Ajustar o validador** (`src/index.js`, `validarManifestoVersao`) e publicar o worker de novo.
Proposta: `{}` é aceito (retira o aviso); senão `versao` obrigatória; `url`+`sha256` e
`zip_url`+`zip_sha256`, quando presentes, precisam vir em par e válidos (https `.zip`, 64 hex);
`pagina`, se presente, `https` de `pcdofafa.com.br`. Depois, publicar com `publicar.ps1 -Tipo versao`
(ou `-Dados @{...}`) como hoje.

**B. Sem mexer no código**, direto no D1 (ignora o validador; o worker serve o JSON como está):

```
npx wrangler d1 execute pcdofafa-dlss --remote --command "INSERT INTO app_config (tipo,dados,atualizado_em) VALUES ('versao','{\"versao\":\"2.2\",\"notas\":\"...\"}',strftime('%s','now')) ON CONFLICT(tipo) DO UPDATE SET dados=excluded.dados, atualizado_em=excluded.atualizado_em"
```

**Retirar um aviso:** publicar `{}` (estado de hoje: ninguém vê aviso). Por A, `publicar.ps1`; por B,
o mesmo comando com `'{}'`.

Contagem: o clique que abre a página reutiliza o evento `clique_atualizar` (o servidor só conhece os
nomes de `src/resumo.js` EVENTOS). Para separar "abriu a página" de "atualizar agora", acrescente um
nome novo à lista do worker e troque `kEventoPaginaDownload` em `atualizacao.h`.

## O que o atualizador nunca toca

`kArquivosDaPessoa` (`*.ini`, `*.cfg`, logs, add-ons e DLLs da pessoa...) e `kPastasDaPessoa`
(`reshade-cache`, `bin-camada2`, `camada2`, `backup-versao-anterior`, `cache`, `mod-amd`). O
`/XD mod-amd` casa o nome da pasta em qualquer nível (`bin\mod-amd` incluída). O `atualizar.cmd` usa
`robocopy /E`, nunca `/PURGE` nem `/MIR`: nada é apagado.
