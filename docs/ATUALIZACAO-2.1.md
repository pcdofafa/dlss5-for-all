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
| `url` + `sha256` | o formato da 1.2/2.0 (elas só atualizam com eles); a 2.1+ os usa como zip se `zip_*` faltarem. |
| `modo` | opcional. `"site"` força o modo site na 2.1+ mesmo com zip (1.2/2.0 ignoram o campo). |
| `minima_para_auto` | opcional; menor versão instalada que atualiza sem perguntar (caixa "automaticamente"). |

Zip incompleto (só a URL, ou hash que não é hexadecimal) não invalida o aviso: cai no modo site e o
`launcher.log` diz o motivo.

## Compatibilidade com a 1.2 e a 2.0 (que já atualizam direto)

A 1.2 e a 2.0 têm o mecanismo direto e o dono quer usá-lo no lançamento da 2.1: o manifesto no
formato ANTIGO (`versao` + `url` + `sha256`) continua funcionando para elas exatamente como hoje, e a
2.1+ o entende como modo direto. O parser delas (`atualizacao.h`, `LerManifesto`, linhas 461-462 no
commit `37db60b`; o da 2.0.1 é idêntico) devolve "manifesto inválido", sem aviso, quando `url` está
vazia ou `sha256` não tem 64 hexadecimais, e ignora campos que não conhece (`pagina`, `zip_*`, `modo`).
É isso que faz um manifesto "só site" ou "zip_*" ser invisível para elas.

## As combinações

**1. Direto para todos** (1.2, 2.0 e 2.1+ baixam, conferem e trocam). É o formato de sempre e o que
`publicar-versao.ps1` gera sem `-SoDireto21`. O worker atual já o serve e aceita:

```json
{"versao":"2.1","url":"https://.../DLSS5ForAll-2.1.zip","sha256":"<64 hex>","tamanho_bytes":48300000,"notas":"...","notas_en":"..."}
```

**2. Só aviso de site** (só a 2.1+ vê; 1.2/2.0 não oferecem nada):

```json
{"versao":"2.2","notas":"Corrige o travamento ao abrir.","notas_en":"Fixes the freeze on launch."}
```

**3. Direto só para a 2.1+** (1.2/2.0 não oferecem nada):

```json
{"versao":"2.2","zip_url":"https://.../DLSS5ForAll-2.2.zip","zip_sha256":"<64 hex>","tamanho_bytes":48300000,"notas":"..."}
```

(`publicar-versao.ps1 -SoDireto21` gera esse formato.)

**4. Direto para os antigos, site para a 2.1+**: o formato 1 mais `"modo":"site"`. A 1.2/2.0 ignoram o
campo e atualizam direto; a 2.1+ ignora o zip e abre a página.

```json
{"versao":"2.2","url":"https://.../DLSS5ForAll-2.2.zip","sha256":"<64 hex>","modo":"site"}
```

## Aplicar o zip da 2.1 por cima de uma 1.2 ou 2.0

O `atualizar.cmd` da 1.2 e o da 2.0.1 são idênticos e usam `robocopy /E` (sem `/PURGE`, sem `/MIR`):
só copiam o que existe no zip, nunca apagam. A lista de pastas protegidas deles não tem `mod-amd`
(só `reshade-cache bin-camada2 camada2 backup-versao-anterior cache`), e os `*.ini`, `*.cfg`,
`dlssnr_on_amd*`, `nvngx_*.dll`, `renodx-dlss*.addon64` e logs ficam como estão.

- **Nada se perde nem se sobrescreve de importante.** O mod e os arquivos da pessoa estavam em
  `bin-amd\`, que o zip da 2.1 não tem: a pasta inteira fica para trás intacta (lixo inofensivo, e é
  dela que o `preparar-mod-amd` copia o mod para `bin\mod-amd`). `bin\mod-amd` não existe nessas
  máquinas, então o robocopy só o cria (README e script de conferência). O zip público NÃO pode
  levar binários nem `.ini` do mod dentro de `bin\mod-amd`: a lista antiga só protegeria
  `dlssnr_on_amd*` e `*.ini`.
- **O `bin-amd\DLSS5ForAll.exe` antigo fica para trás** (a espera por exe em uso, o backup e a
  restauração em falha usam "if exist", não quebram quando a pasta some do zip).
- **O que pode dar errado é a reabertura.** O `.cmd` antigo reabre `DLSS5ForAll.exe --atualizado=<v>
  --nvidia|--amd|--sem-dlss`, agora o launcher 2.1. Se o launcher 2.1 tratar `--amd` abrindo
  `bin-amd\DLSS5ForAll.exe`, abre o app ANTIGO (se a pasta sobrou) ou mostra "Não encontrei o
  programa" (se não). Neste repositório (`launcher.cpp` da base) `--amd` ainda faz isso. Cuidado
  necessário no launcher 2.1 final: `--amd` e `modulo=amd` salvo devem abrir `bin\DLSS5ForAll.exe
  --amd` (mod em `bin\mod-amd`) e nunca `bin-amd`; e, se existir `bin-amd\` sem os arquivos do mod
  em `bin\mod-amd`, oferecer rodar `bin\mod-amd\preparar-mod-amd.cmd` (que copia da pasta antiga, só
  depois de o usuário responder S). Isso é trabalho da série do launcher/AMD, não desta.
- Falha do robocopy (código >= 8): o `.cmd` devolve os exes do backup; nesse caso a pessoa fica na
  versão antiga, e na 2.1+ a mensagem de erro oferece a página do site.
- Ficam sem verificação real: nada disto foi executado (só lido e um teste de `robocopy /XD` em
  pasta de rascunho).

## O que o dono faz no servidor (worker `fafa-dlss5-app`)

O manifesto é o mesmo endereço `GET /versao.json`, lido do D1 (`app_config`, `tipo='versao'`). Hoje o
`POST /publicar` recusa (400) manifesto sem `url` `.zip` e `sha256`. As combinações 1 e 4 (têm `url` + `sha256`) passam como estão: nada a mudar no servidor. As combinações 2 e 3
pedem **uma** destas duas saídas:

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
