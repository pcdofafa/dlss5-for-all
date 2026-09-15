-- =====================================================================================
--  Migração 01 — avaliações com nota + curtidas
--  SÓ é necessária se o `schema.sql` ANTIGO (sem curtidas) já tiver sido aplicado.
--  Num banco novo, o `schema.sql` atual já cria tudo e esta migração não é usada.
--
--     npx wrangler d1 execute pcdofafa-dlss --remote --file=migracao-01-avaliacoes.sql
--
--  ATENÇÃO: o SQLite NÃO tem "ADD COLUMN IF NOT EXISTS". Rodar isto duas vezes dá
--  erro "duplicate column name" nas linhas de ALTER — o que é inofensivo (nada é
--  alterado), mas por isso ela é aditiva e separada: nenhuma linha aqui apaga dado.
-- =====================================================================================

ALTER TABLE comentarios ADD COLUMN curtidas   INTEGER NOT NULL DEFAULT 0;
ALTER TABLE comentarios ADD COLUMN texto_norm TEXT;

CREATE INDEX IF NOT EXISTS idx_com_norm ON comentarios(texto_norm, criado_em);

CREATE TABLE IF NOT EXISTS curtidas (
  comentario_id INTEGER NOT NULL,
  visitante     TEXT    NOT NULL,
  criado_em     INTEGER NOT NULL,
  PRIMARY KEY (comentario_id, visitante)
);
CREATE INDEX IF NOT EXISTS idx_curt_visitante ON curtidas(visitante, criado_em);
