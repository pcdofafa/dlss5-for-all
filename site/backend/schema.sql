-- =====================================================================================
--  fafa-dlss-api — esquema do D1 (banco `pcdofafa-dlss`)
--  Aplicar UMA VEZ, depois de criar o banco:
--     npx wrangler d1 execute pcdofafa-dlss --remote --file=schema.sql
--  (com --local em vez de --remote, roda no banco de testes da sua máquina)
--
--  Tudo aqui é idempotente (IF NOT EXISTS): rodar de novo não apaga nada.
--  Se este esquema JÁ tiver sido aplicado numa versão anterior, rode também o
--  `migracao-01-avaliacoes.sql`, que acrescenta as colunas e tabelas novas sem
--  recriar nada.
-- =====================================================================================

-- ---------------------------------------------------------------------- contadores
-- Rollup O(1) dos downloads. Ler o número da página é UM SELECT, não um COUNT(*)
-- na tabela inteira — é o que mantém o custo constante quando o número crescer.
--   chave 'download:total'                 -> todos os downloads contados
--   chave 'download:esp:<id>'              -> um por espelho (ex.: download:esp:drive)
--   chave 'download:dia:AAAA-MM-DD:<id>'   -> série diária, para gráfico depois
-- Os ids de espelho vêm do array ESPELHOS da página; o worker só aceita
-- [a-z0-9-], então acrescentar espelho novo não exige mexer no banco.
CREATE TABLE IF NOT EXISTS contadores (
  chave  TEXT PRIMARY KEY,
  valor  INTEGER NOT NULL DEFAULT 0
);

-- ---------------------------------------------------------------------- downloads
-- Uma linha por download CONTADO. Existe para a deduplicação (a janela de 6 h)
-- e para auditoria; os totais que a página mostra vêm de `contadores`.
-- Linhas com mais de 7 dias são podadas sozinhas (ver podarAntigos no worker) —
-- a tabela nunca vira um problema de tamanho, e os totais não dependem dela.
CREATE TABLE IF NOT EXISTS downloads (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  espelho    TEXT    NOT NULL,           -- id do espelho escolhido (ex.: 'drive')
  visitante  TEXT    NOT NULL,           -- hash salgado de IP+User-Agent (nunca o IP)
  criado_em  INTEGER NOT NULL            -- epoch em segundos
);
CREATE INDEX IF NOT EXISTS idx_dl_visitante ON downloads(visitante, criado_em);
CREATE INDEX IF NOT EXISTS idx_dl_criado    ON downloads(criado_em);

-- ---------------------------------------------------------------------- votos
-- ESTA TABELA É A FONTE ÚNICA DA MÉDIA. Uma linha por visitante, ponto.
-- É o que torna impossível contar alguém duas vezes: quem dá nota avulsa e depois
-- escreve uma avaliação com outra nota faz UPDATE nesta mesma linha — a média não
-- soma dois votos, e a avaliação publicada passa a mostrar a nota nova.
CREATE TABLE IF NOT EXISTS votos (
  visitante   TEXT    PRIMARY KEY,
  nota        INTEGER NOT NULL CHECK (nota BETWEEN 1 AND 5),
  criado_em   INTEGER NOT NULL,
  alterado_em INTEGER NOT NULL,
  alteracoes  INTEGER NOT NULL DEFAULT 0  -- teto anti-abuso (ver MAX_ALTERACOES_VOTO)
);

-- ---------------------------------------------------------------------- avaliações
-- O TEXTO da avaliação. A NOTA não mora aqui de propósito — ela vem de `votos`
-- pelo `visitante`, o que garante um voto por pessoa mesmo quem escreve.
-- Nasce sempre 'pendente'; só a rota /admin, com token, aprova.
--   curtidas   -> rollup no próprio registro: listar N avaliações é 1 consulta,
--                 não N COUNT(*) na tabela de curtidas
--   texto_norm -> texto sem acento/pontuação/caixa, para barrar repetição de
--                 conteúdo mesmo vinda de visitante (IP) diferente
CREATE TABLE IF NOT EXISTS comentarios (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  nome        TEXT    NOT NULL,
  texto       TEXT    NOT NULL,
  texto_norm  TEXT,
  visitante   TEXT    NOT NULL,
  estado      TEXT    NOT NULL DEFAULT 'pendente',   -- pendente | aprovado | recusado
  curtidas    INTEGER NOT NULL DEFAULT 0,
  criado_em   INTEGER NOT NULL,
  moderado_em INTEGER
);
CREATE INDEX IF NOT EXISTS idx_com_estado    ON comentarios(estado, criado_em DESC);
CREATE INDEX IF NOT EXISTS idx_com_visitante ON comentarios(visitante, criado_em);
CREATE INDEX IF NOT EXISTS idx_com_norm      ON comentarios(texto_norm, criado_em);

-- ---------------------------------------------------------------------- curtidas
-- Uma linha por (avaliação, visitante) — a chave primária composta é o que garante
-- "uma curtida por pessoa" e o que permite descurtir (basta apagar a linha).
-- O número exibido NÃO sai de um COUNT aqui: sai de comentarios.curtidas.
CREATE TABLE IF NOT EXISTS curtidas (
  comentario_id INTEGER NOT NULL,
  visitante     TEXT    NOT NULL,
  criado_em     INTEGER NOT NULL,
  PRIMARY KEY (comentario_id, visitante)
);
CREATE INDEX IF NOT EXISTS idx_curt_visitante ON curtidas(visitante, criado_em);
