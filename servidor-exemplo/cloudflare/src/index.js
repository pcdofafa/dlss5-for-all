// Exemplo de servidor (Cloudflare Worker + D1) que cumpre o CONTRATO do launcher do
// DLSS5 For All. So documentacao: o servidor de verdade e o painel fafa-attribution.
//
//   GET  /versao.json?v=&m=&l=&id=    manifesto publicado; com `id` conta uma abertura
//   GET  /ofertas.json?v=&m=&l=&id=   publi publicada (mesmos parametros; nao conta de novo)
//   GET  /evento?e=<nome>&id=&v=&m=&l=  registra um clique (resposta ignorada pelo launcher)
//   GET  /resumo?token=...            resumo em JSON para o cartao "Relatorio de uso"
//   POST /publicar?token=...          {"tipo":"versao"|"ofertas","dados":{...}}
//
// Sem cookies, CORS fechado (o launcher nao e navegador), limite de 1 registro por id/evento
// por hora (rate limit por id, que e o que o launcher manda; IP nao e guardado).
const TIPOS = new Set(["versao", "ofertas"]);
const EVENTOS = new Set(["clique_nvidia", "clique_amd", "clique_sem_dlss", "clique_grupo", "clique_canal",
                         "clique_cupom", "clique_atualizar", "fechou_sem_escolher", "atualizacao_ok", "atualizacao_falhou"]);
const MODULOS = new Set(["nvidia", "amd", "sem"]);

const json = (obj, status = 200) => new Response(JSON.stringify(obj), {
  status, headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" } });
const texto = (s, status = 200) => new Response(s, { status, headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });

function limpo(v, max, re) { if (typeof v !== "string") return null; v = v.slice(0, max); return re.test(v) ? v : null; }
function identidade(u) {
  const id = limpo(u.searchParams.get("id"), 36, /^[0-9a-f-]{36}$/i);
  if (!id) return null;   // sem id = contar=0: nada e registrado
  const v = limpo(u.searchParams.get("v"), 32, /^[0-9.]+$/) || "";
  const m = limpo(u.searchParams.get("m"), 8, /^[a-z]+$/);
  const l = limpo(u.searchParams.get("l"), 2, /^(pt|en)$/) || "";
  return { id: id.toLowerCase(), v, m: MODULOS.has(m) ? m : "", l };
}
async function registrar(env, e, ident) {
  if (!ident) return;
  const agora = new Date();
  const data = agora.toISOString().slice(0, 10), hora = agora.toISOString().slice(11, 19);
  // 1 registro por id/evento por hora
  const limite = new Date(agora.getTime() - 3600_000).toISOString();
  const ultimo = await env.DB.prepare("SELECT data||'T'||hora AS t FROM aberturas WHERE id=? AND e=? ORDER BY data DESC, hora DESC LIMIT 1")
    .bind(ident.id, e).first();
  if (ultimo && ultimo.t > limite.slice(0, 19)) return;
  await env.DB.prepare("INSERT INTO aberturas(data,hora,e,v,m,l,id) VALUES (?,?,?,?,?,?,?)")
    .bind(data, hora, e, ident.v, ident.m, ident.l, ident.id).run();
}
async function documento(env, tipo) {
  const r = await env.DB.prepare("SELECT json FROM documentos WHERE tipo=?").bind(tipo).first();
  return r ? r.json : null;
}
function tokenOk(env, u) {
  const t = u.searchParams.get("token") || "";
  return env.TOKEN_ADMIN && t.length > 0 && t === env.TOKEN_ADMIN;
}
async function resumo(env) {
  const hoje = new Date().toISOString().slice(0, 10);
  const dia = (n) => new Date(Date.now() - n * 86400_000).toISOString().slice(0, 10);
  const conta = async (desde) => {
    const r = await env.DB.prepare("SELECT COUNT(*) AS ab, COUNT(DISTINCT id) AS us FROM aberturas WHERE e='abertura' AND data>=?").bind(desde).first();
    return { aberturas: r.ab, usuarios: r.us };
  };
  const porDia = (await env.DB.prepare("SELECT data, COUNT(*) AS ab, COUNT(DISTINCT id) AS us FROM aberturas WHERE e='abertura' AND data>=? GROUP BY data ORDER BY data").bind(dia(13)).all()).results
    .map(r => ({ data: r.data, aberturas: r.ab, usuarios: r.us }));
  const mapa = async (sql, ...b) => { const o = {}; for (const r of (await env.DB.prepare(sql).bind(...b).all()).results) o[r.k] = r.n; return o; };
  return {
    gerado: new Date().toISOString(),
    hoje: await conta(hoje), dias7: await conta(dia(6)), dias30: await conta(dia(29)),
    por_dia: porDia,
    por_versao: await mapa("SELECT v AS k, COUNT(DISTINCT id) AS n FROM aberturas WHERE e='abertura' AND data>=? GROUP BY v", dia(29)),
    por_modulo: await mapa("SELECT CASE WHEN m='' THEN 'sem' ELSE m END AS k, COUNT(DISTINCT id) AS n FROM aberturas WHERE e='abertura' AND data>=? GROUP BY k", dia(29)),
    por_botao_hoje: await mapa("SELECT e AS k, COUNT(*) AS n FROM aberturas WHERE e<>'abertura' AND data=? GROUP BY e", hoje),
    por_botao_7: await mapa("SELECT e AS k, COUNT(*) AS n FROM aberturas WHERE e<>'abertura' AND data>=? GROUP BY e", dia(6)),
  };
}

export default {
  async fetch(req, env) {
    const u = new URL(req.url);
    const p = u.pathname.replace(/\/+$/, "") || "/";
    try {
      if (req.method === "GET" && (p === "/versao.json" || p === "/ofertas.json")) {
        const doc = await documento(env, p === "/versao.json" ? "versao" : "ofertas");
        if (p === "/versao.json") await registrar(env, "abertura", identidade(u));
        return doc ? new Response(doc, { headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" } })
                   : texto("nada publicado", 404);
      }
      if (req.method === "GET" && p === "/evento") {
        const e = u.searchParams.get("e") || "";
        if (EVENTOS.has(e)) await registrar(env, e, identidade(u));
        return texto("ok");
      }
      if (req.method === "GET" && p === "/resumo") {
        if (!tokenOk(env, u)) return texto("token", 403);
        return json(await resumo(env));
      }
      if (req.method === "POST" && p === "/publicar") {
        if (!tokenOk(env, u)) return texto("token", 403);
        const corpo = await req.json();
        if (!TIPOS.has(corpo.tipo) || typeof corpo.dados !== "object") return texto("corpo: {tipo, dados}", 400);
        const s = JSON.stringify(corpo.dados);
        if (s.length > 16384) return texto("grande demais", 413);
        await env.DB.prepare("INSERT INTO documentos(tipo,json,atualizado) VALUES (?,?,?) ON CONFLICT(tipo) DO UPDATE SET json=excluded.json, atualizado=excluded.atualizado")
          .bind(corpo.tipo, s, new Date().toISOString()).run();
        return json({ ok: true, tipo: corpo.tipo, bytes: s.length });
      }
      return texto("nao encontrado", 404);
    } catch (e) {
      return texto("erro: " + (e && e.message ? e.message : e), 500);
    }
  }
};
