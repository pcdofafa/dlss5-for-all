# Servidor de exemplo (Cloudflare Worker + D1) para o launcher do DLSS5 For All

**Só documentação do contrato.** O servidor de verdade é o painel `fafa-attribution`
do usuário (Cloudflare Workers + D1), que outro agente está estendendo com estas mesmas
rotas. Este exemplo existe para quem precisar ver o contrato inteiro funcionando de ponta
a ponta num Worker isolado. Não foi implantado nem testado contra a conta do usuário
(nenhum token foi usado).

## O contrato que o launcher espera

Uma única chave no `ofertas.ini` da raiz do pacote liga tudo: `[atualizacao] url_base=`.

| Rota | Quem chama | O que faz |
|---|---|---|
| `GET {base}/versao.json?v=&m=&l=&id=` | launcher, ao abrir | devolve o manifesto de versão; **conta uma abertura** quando `id` vem junto |
| `GET {base}/ofertas.json?v=&m=&l=&id=` | launcher, ao abrir | devolve a publi da tela (não conta de novo) |
| `GET {base}/evento?e=<nome>&id=&v=&m=&l=` | launcher, num clique | registra o clique; resposta ignorada (2 s) |
| `GET {base}/resumo?token=…` | launcher do **autor** | resumo em JSON para o cartão "Relatório de uso" |
| `POST {base}/publicar?token=…` | `publicar-versao.ps1` / `publicar-oferta.ps1` | corpo `{"tipo":"versao"\|"ofertas","dados":{…}}` |

Parâmetros: `v` versão do app, `m` módulo da última abertura (`nvidia`/`amd`/`sem`, pode
faltar), `l` idioma (`pt`/`en`), `id` GUID aleatório da instalação. Com `contar=0` o
launcher não manda **nenhum** deles e não manda eventos; o servidor então só serve os JSON.

Eventos: `clique_nvidia`, `clique_amd`, `clique_sem_dlss`, `clique_grupo`, `clique_canal`,
`clique_cupom`, `clique_atualizar`, `fechou_sem_escolher`, `atualizacao_ok`, `atualizacao_falhou`.

Resumo (`/resumo`):
```json
{"gerado":"2026-09-10T12:00:00Z",
 "hoje":{"aberturas":N,"usuarios":N},"dias7":{...},"dias30":{...},
 "por_dia":[{"data":"2026-09-10","aberturas":N,"usuarios":N}, ...últimos 14],
 "por_versao":{"1.1":N},"por_modulo":{"nvidia":N,"amd":N,"sem":N},
 "por_botao_hoje":{"clique_nvidia":N,...},"por_botao_7":{...}}
```
`usuarios` = ids distintos; `aberturas` = GETs de `versao.json` com `id`. O exemplo aplica
um limite de 1 registro por `id` e evento por hora (o launcher só abre uma vez por
execução, então isso só corta repetições anormais).

## Passo a passo (se for usar este exemplo sozinho)
1. `npm i -g wrangler` (ou `npx wrangler`), `npx wrangler login`.
2. `npx wrangler d1 create dlss5` → cole o `database_id` no `wrangler.toml`.
3. `npx wrangler d1 execute dlss5 --remote --file=schema.sql`
4. `npx wrangler secret put TOKEN_ADMIN` (um segredo longo; é o `?token=` do `/resumo` e do `/publicar`).
5. `npx wrangler deploy` → anote a URL (`https://dlss5-atualizacao.<conta>.workers.dev`).
6. No `ofertas.ini` distribuído: `url_base=https://dlss5-atualizacao.<conta>.workers.dev`.
   Na cópia do autor, além disso: `relatorio_token=<o mesmo TOKEN_ADMIN>`.
7. Publicar: `$env:DLSS5_TOKEN_ADMIN="..."`, depois
   `publicar-versao.ps1 -UrlZip https://... -Notas "..." -UrlBase https://... -Publicar` e
   `publicar-oferta.ps1 -Titulo "..." -Cupom X -Validade 2026-09-14 -UrlBase https://... -Publicar`.

`npx wrangler dev` exige login na conta; por isso este exemplo não foi executado aqui. A
lógica do cliente foi testada contra um servidor local simples que responde as mesmas
rotas (ver o relatório do atualizador).

## Alternativa em PHP (resumida)
Um `index.php` que: lê `$_GET`, grava `date, hora, e, v, m, l, id` numa linha de
`registros.csv` quando vier `id` (rotas `versao.json`/`evento`), serve `versao.json`/`ofertas.json`
do disco, e em `resumo` (com `token` conferido contra uma constante fora do webroot) agrupa
o CSV por dia/versão/módulo/evento e devolve o JSON acima. O launcher não distingue: só
importa a forma das rotas e do JSON.
