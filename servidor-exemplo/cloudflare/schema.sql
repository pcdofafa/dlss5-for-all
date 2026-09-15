-- Esquema do D1 para o exemplo (ver wrangler.toml). Uma linha por abertura/evento.
CREATE TABLE IF NOT EXISTS aberturas (
  data TEXT NOT NULL,          -- 'YYYY-MM-DD' (UTC)
  hora TEXT NOT NULL,          -- 'HH:MM:SS'
  e    TEXT NOT NULL,          -- 'abertura' (GET versao.json) ou o nome do evento
  v    TEXT, m TEXT, l TEXT,   -- versao, modulo, idioma
  id   TEXT NOT NULL           -- id_instalacao (GUID aleatorio)
);
CREATE INDEX IF NOT EXISTS ix_aberturas_data ON aberturas(data);
CREATE INDEX IF NOT EXISTS ix_aberturas_id_data ON aberturas(id, data, e);

-- versao.json e ofertas.json publicados pelo autor (POST /publicar)
CREATE TABLE IF NOT EXISTS documentos (
  tipo TEXT PRIMARY KEY,       -- 'versao' | 'ofertas'
  json TEXT NOT NULL,
  atualizado TEXT NOT NULL
);
