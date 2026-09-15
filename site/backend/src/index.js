// =====================================================================================
//  fafa-dlss-api — backend da página pcdofafa.com.br/dlss (DLSS5 For All)
// =====================================================================================
//
//  Worker SEPARADO do `ecurtadorpcdofafa`, de propósito: o encurtador serve /p/*, /h/*
//  e os agendamentos de posts, e a regra do repo fafa-attribution é que ele não recebe
//  feature nova de terceiros. Este worker pega a rota MAIS ESPECÍFICA
//  `pcdofafa.com.br/dlss-api/*`, que vence o `/*` do principal SEM TOCAR NELE —
//  mesmo desenho já usado pelo `fafa-format-web` em /format/*. Nenhum deploy do
//  worker de produção é necessário para nada aqui.
//
//  O que ele guarda (D1, banco `pcdofafa-dlss`):
//    - contador REAL de downloads, deduplicado por visitante, com o espelho escolhido
//    - avaliações: nota de 1 a 5 (obrigatória) + texto opcional, no formato de review
//    - curtidas nas avaliações, uma por visitante, descurtíveis
//
//  A REGRA CENTRAL: a média sai SEMPRE da tabela `votos`, que tem uma linha por
//  visitante. Escrever uma avaliação faz UPSERT nessa mesma linha. Por isso quem
//  vota avulso e depois escreve com outra nota NÃO conta duas vezes — não é uma
//  verificação que pode falhar, é a forma da tabela que impede.
//
//  Endpoints públicos
//    GET  /dlss-api/estado      -> tudo que a página mostra
//    POST /dlss-api/download    -> conta um download   {espelho}
//    POST /dlss-api/voto        -> nota avulsa         {nota:1..5}
//    POST /dlss-api/avaliacao   -> nota + texto        {nota, nome, texto, website, t}
//    POST /dlss-api/curtida     -> curte/descurte      {id}
//  Endpoints de moderação (token)
//    GET  /dlss-api/admin?token=…       -> página de aprovação
//    POST /dlss-api/admin/moderar       -> {token, id, acao:"aprovar"|"recusar"|"apagar"}
//
//  Secrets (npx wrangler secret put …):
//    IP_SALT      obrigatório — sal do hash de visitante. Sem ele o hash seria
//                 reversível por força bruta sobre a faixa de IPs.
//    ADMIN_TOKEN  obrigatório — a rota /admin só existe se este secret existir.
//    TG_BOT_TOKEN + TG_ADMIN_CHAT  opcionais — avisam no Telegram que chegou
//                 avaliação para aprovar. Sem eles, o worker roda igual.
// =====================================================================================

// ------------------------------------------------------------------ ajustes
const JANELA_DEDUPE_S   = 6 * 3600;  // mesmo visitante só conta 1 download a cada 6 h
const RETENCAO_DL_DIAS  = 7;         // poda das linhas de download já fora da janela
const MAX_NOME          = 40;
const MAX_TEXTO         = 800;
const MAX_ALTERACOES_VOTO = 20;
const AVAL_NA_PAGINA    = 50;

// Antiflood das avaliações. Os números erram para o lado PERMISSIVO no que depende
// de identificar o visitante, porque sem cadastro a identidade é aproximada: num
// provedor com CGNAT ou numa casa com várias pessoas, todo mundo parece um só, e
// apertar aqui silencia inocente sem aviso. O que segura flood de verdade são as
// duas defesas de baixo, que NÃO dependem de saber quem é quem.
const AVAL_POR_DIA      = 2;         // por visitante — 2, não 1, justamente por causa do IP compartilhado
const AVAL_INTERVALO_S  = 300;       // 5 min entre um envio e o próximo do mesmo visitante
const AVAL_GLOBAL_HORA  = 40;        // teto global; acima disso, descarte silencioso
const DUPLICATA_DIAS    = 7;         // janela em que texto repetido é considerado duplicata
const TEMPO_MINIMO_MS   = 3000;      // formulário preenchido rápido demais = robô

const CURTIDAS_POR_HORA = 60;        // por visitante. Curtir muita avaliação é uso
                                     // normal, não flood — o teto só barra laço automatizado.

// Ordenação das avaliações: mais curtidas em cima, MAS com peso do tempo.
// Sem o divisor, quem chegou primeiro fica no topo para sempre (acumula curtida por
// estar visível) e avaliação nova nasce com zero no fim da lista, onde ninguém a vê
// para curtir — a página congela nos mesmos cinco cards em poucas semanas.
// score = (curtidas + 1) / (idade_em_DIAS + 2)
//
// A escala é em DIAS, não em horas, e isso foi medido: com horas, uma avaliação de
// 10 dias com 20 curtidas caía para score 0,086 e perdia até para uma recém-chegada
// sem curtida nenhuma — ou seja, a lista virava cronológica na prática e o "mais
// curtidos em cima" que o dono pediu deixava de existir. Em dias, a mesma avaliação
// faz 21/12 = 1,75 e continua no topo, enquanto uma nova nasce em 1/2 = 0,5 e sobe
// de verdade se as pessoas curtirem. Meses depois ela cede sozinha.
//
// O "+1" dá score inicial a quem ainda não tem curtida; o "+2" evita divisão por
// ~zero no primeiro dia. Divisão simples de propósito: POWER() não é garantido no
// SQLite do D1, e com este volume não faz falta.
// PARA CALIBRAR: aumentar o "+2" achata a curva (favorece mais as curtidas);
// voltar para 3600.0 favorece novidade de forma agressiva.
const ORDEM_AVALIACOES =
  "ORDER BY (c.curtidas + 1.0) / ((? - c.criado_em) / 86400.0 + 2.0) DESC, c.criado_em DESC";

// Qual espelho foi usado. O id vem do array ESPELHOS da página, então não dá para
// ter uma lista fechada aqui — o que dá é limitar o FORMATO, e é o que importa:
// nada além de letras minúsculas, números e hífen entra no banco.
const RE_ESPELHO = /^[a-z0-9][a-z0-9-]{0,23}$/;

const SITE = "https://pcdofafa.com.br";

// ------------------------------------------------------------------ utilitários
function json(status, corpo, extra = {}) {
  return new Response(JSON.stringify(corpo), {
    status,
    headers: {
      "Content-Type": "application/json;charset=utf-8",
      // Números ao vivo nunca podem ficar em cache de borda nem de navegador.
      "Cache-Control": "no-store",
      "Access-Control-Allow-Origin": SITE,
      "Vary": "Origin",
      ...extra,
    },
  });
}

/** Escapa para HTML. Usado SÓ na página de moderação, que é o único lugar deste
 *  worker onde texto de visitante entra num documento. A API pública devolve JSON,
 *  e a página renderiza com textContent. */
function esc(s) {
  return String(s ?? "").replace(/[&<>"']/g, (c) => (
    { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]
  ));
}

/** Tira caracteres de controle (menos \n e \t), normaliza quebras e corta no
 *  tamanho máximo. Não tenta "limpar HTML": o texto é guardado como texto puro
 *  e nunca é interpretado como marcação em lugar nenhum. */
function limpar(txt, max) {
  return String(txt ?? "")
    .replace(/\r\n?/g, "\n")
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, "")
    .replace(/\n{3,}/g, "\n\n")
    .trim()
    .slice(0, max);
}

/** Forma canônica do texto, para detectar reenvio do mesmo conteúdo: sem caixa,
 *  sem acento, sem pontuação, espaços colapsados. "Muito bom!!!" e "muito bom"
 *  viram a mesma coisa — que é como o flooder tenta escapar. */
function normalizarTexto(txt) {
  return String(txt ?? "")
    .toLowerCase()
    .normalize("NFD").replace(/[\u0300-\u036f]/g, "")
    .replace(/[^a-z0-9]+/g, " ")
    .trim();
}

const agora = () => Math.floor(Date.now() / 1000);
// Data em horário de Brasília (UTC-3). Sem o ajuste, a série diária viraria o dia
// às 21h daqui, e o "downloads de hoje" não bateria com o dia de quem olha.
const hoje  = () => new Date(Date.now() - 3 * 3600 * 1000).toISOString().slice(0, 10);

/** Identidade do visitante: hash salgado de IP + User-Agent, 32 hex.
 *  O IP em si nunca é gravado — o que vai para o banco é este hash, que sem o
 *  IP_SALT não volta a ser um endereço.
 *  Limite conhecido e aceito: sem cadastro isto é uma APROXIMAÇÃO. Erra para os
 *  dois lados (IP dinâmico divide uma pessoa em várias; CGNAT junta várias numa
 *  só), e é por isso que os tetos por visitante são folgados e a moderação é a
 *  rede de segurança de verdade. */
async function idVisitante(request, env) {
  const ip = request.headers.get("CF-Connecting-IP") || "0.0.0.0";
  const ua = request.headers.get("User-Agent") || "";
  const bytes = new TextEncoder().encode(`${env.IP_SALT}|${ip}|${ua}`);
  const dig = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return Array.from(dig.slice(0, 16), (b) => b.toString(16).padStart(2, "0")).join("");
}

async function corpoJson(request) {
  try {
    const c = await request.json();
    return c && typeof c === "object" ? c : {};
  } catch {
    return {};
  }
}

/** Comparação de token em tempo constante — evita vazar o segredo por timing. */
function tokenBate(a, b) {
  const x = String(a ?? ""), y = String(b ?? "");
  if (x.length !== y.length) return false;
  let dif = 0;
  for (let i = 0; i < x.length; i++) dif |= x.charCodeAt(i) ^ y.charCodeAt(i);
  return dif === 0;
}

// ------------------------------------------------------------------ leituras
async function lerContadores(env) {
  const { results } = await env.DB.prepare(
    "SELECT chave, valor FROM contadores WHERE chave = 'download:total' OR chave LIKE 'download:esp:%'"
  ).all();
  const por_espelho = {};
  let total = 0;
  for (const r of results || []) {
    if (r.chave === "download:total") total = r.valor;
    else por_espelho[r.chave.slice("download:esp:".length)] = r.valor;
  }
  // A página só mostra o total. O detalhe por espelho vai junto para o dono
  // conseguir ver por onde as pessoas realmente baixam.
  return { total, por_espelho };
}

async function lerAvaliacaoAgregada(env, visitante) {
  const ag = await env.DB.prepare(
    "SELECT COUNT(*) AS n, COALESCE(SUM(nota),0) AS s FROM votos"
  ).first();
  const meu = await env.DB.prepare(
    "SELECT nota FROM votos WHERE visitante = ?"
  ).bind(visitante).first();
  const n = ag?.n || 0;
  return {
    votos: n,
    media: n ? Math.round((ag.s / n) * 100) / 100 : null,
    meu_voto: meu?.nota ?? null,
  };
}

/** Lista as avaliações aprovadas já ORDENADAS E PAGINADAS PELO BANCO — nunca um
 *  sort no navegador sobre tudo, que com o tempo vira resposta grande demais.
 *  A nota vem de `votos` por LEFT JOIN: é a nota ATUAL de quem escreveu. */
async function lerAvaliacoes(env, visitante) {
  const t = agora();
  const { results } = await env.DB.prepare(
    "SELECT c.id, c.nome, c.texto, c.criado_em, c.curtidas, v.nota " +
    "FROM comentarios c LEFT JOIN votos v ON v.visitante = c.visitante " +
    "WHERE c.estado = 'aprovado' " + ORDEM_AVALIACOES + " LIMIT ?"
  ).bind(t, AVAL_NA_PAGINA).all();

  // Uma consulta só resolve o "eu curti" de todos os cards.
  const minhas = await env.DB.prepare(
    "SELECT comentario_id FROM curtidas WHERE visitante = ?"
  ).bind(visitante).all();
  const curtidos = new Set((minhas.results || []).map((r) => r.comentario_id));

  const pend = await env.DB.prepare(
    "SELECT 1 AS x FROM comentarios WHERE visitante = ? AND estado = 'pendente' LIMIT 1"
  ).bind(visitante).first();

  return {
    lista: (results || []).map((r) => ({
      id: r.id,
      nome: r.nome,
      texto: r.texto,
      nota: r.nota ?? null,
      curtidas: r.curtidas || 0,
      eu_curti: curtidos.has(r.id),
      em: r.criado_em,
    })),
    minha_pendente: !!pend,
  };
}

// ------------------------------------------------------------------ GET /estado
async function rotaEstado(request, env) {
  const visitante = await idVisitante(request, env);
  const [downloads, avaliacao, avaliacoes] = await Promise.all([
    lerContadores(env),
    lerAvaliacaoAgregada(env, visitante),
    lerAvaliacoes(env, visitante),
  ]);
  return json(200, {
    ok: true,
    downloads,
    avaliacao,
    avaliacoes: avaliacoes.lista,
    minha_avaliacao_pendente: avaliacoes.minha_pendente,
  });
}

// ------------------------------------------------------------------ POST /download
async function rotaDownload(request, env, ctx) {
  const corpo = await corpoJson(request);
  // String() antes do teste, e não depois: `RE.test(undefined)` compara contra a
  // string "undefined", que passa no formato — e aí iria um `undefined` cru para o
  // bind do D1. Coagir primeiro faz o campo ausente virar "" e cair em "outro".
  const bruto = String(corpo.espelho == null ? "" : corpo.espelho);
  const espelho = RE_ESPELHO.test(bruto) ? bruto : "outro";
  const visitante = await idVisitante(request, env);
  const t = agora();

  // Deduplicação: o mesmo visitante só conta uma vez a cada JANELA_DEDUPE_S,
  // INDEPENDENTE do espelho. Quem tenta um espelho, falha e vai no outro baixou
  // o programa UMA vez — contar dois seria inflar o número.
  const jaContou = await env.DB.prepare(
    "SELECT 1 AS x FROM downloads WHERE visitante = ? AND criado_em > ? LIMIT 1"
  ).bind(visitante, t - JANELA_DEDUPE_S).first();

  if (jaContou) {
    return json(200, { ok: true, contado: false, downloads: await lerContadores(env) });
  }

  const bump = "INSERT INTO contadores (chave, valor) VALUES (?, 1) " +
               "ON CONFLICT(chave) DO UPDATE SET valor = valor + 1";
  await env.DB.batch([
    env.DB.prepare("INSERT INTO downloads (espelho, visitante, criado_em) VALUES (?, ?, ?)")
      .bind(espelho, visitante, t),
    env.DB.prepare(bump).bind("download:total"),
    env.DB.prepare(bump).bind(`download:esp:${espelho}`),
    env.DB.prepare(bump).bind(`download:dia:${hoje()}:${espelho}`),
  ]);

  ctx.waitUntil(podarAntigos(env));
  return json(200, { ok: true, contado: true, downloads: await lerContadores(env) });
}

/** Poda oportunista (1 chamada em 25) das linhas de download fora da janela.
 *  Os totais moram em `contadores` e não dependem destas linhas. */
async function podarAntigos(env) {
  if (Math.random() > 0.04) return;
  try {
    await env.DB.prepare("DELETE FROM downloads WHERE criado_em < ?")
      .bind(agora() - RETENCAO_DL_DIAS * 86400).run();
  } catch { /* poda é higiene, nunca pode derrubar a resposta */ }
}

// ------------------------------------------------------------------ voto (nota)
/** Grava a nota do visitante. É a MESMA função usada pelo voto avulso e pela
 *  avaliação com texto — é isso que garante um voto por pessoa nos dois caminhos. */
async function gravarNota(env, visitante, nota) {
  const t = agora();
  const atual = await env.DB.prepare(
    "SELECT nota, alteracoes FROM votos WHERE visitante = ?"
  ).bind(visitante).first();

  if (!atual) {
    await env.DB.prepare(
      "INSERT INTO votos (visitante, nota, criado_em, alterado_em, alteracoes) VALUES (?, ?, ?, ?, 0)"
    ).bind(visitante, nota, t, t).run();
    return { ok: true };
  }
  if (atual.nota === nota) return { ok: true };
  if (atual.alteracoes >= MAX_ALTERACOES_VOTO) {
    return { ok: false, erro: "você já trocou sua nota vezes demais" };
  }
  await env.DB.prepare(
    "UPDATE votos SET nota = ?, alterado_em = ?, alteracoes = alteracoes + 1 WHERE visitante = ?"
  ).bind(nota, t, visitante).run();
  return { ok: true };
}

function notaValida(v) {
  const n = Number(v);
  return Number.isInteger(n) && n >= 1 && n <= 5 ? n : null;
}

async function rotaVoto(request, env) {
  const corpo = await corpoJson(request);
  const nota = notaValida(corpo.nota);
  if (nota === null) return json(400, { ok: false, erro: "dê uma nota de 1 a 5" });

  const visitante = await idVisitante(request, env);
  const r = await gravarNota(env, visitante, nota);
  if (!r.ok) return json(429, { ok: false, erro: r.erro });
  return json(200, { ok: true, avaliacao: await lerAvaliacaoAgregada(env, visitante) });
}

// ------------------------------------------------------------------ POST /avaliacao
async function rotaAvaliacao(request, env, ctx) {
  const corpo = await corpoJson(request);

  const nota  = notaValida(corpo.nota);
  const nome  = limpar(corpo.nome, MAX_NOME);
  const texto = limpar(corpo.texto, MAX_TEXTO);

  // Erros de PREENCHIMENTO são reportados: quem está de boa-fé precisa saber
  // o que corrigir, e nada disso ajuda um robô.
  if (nota === null)    return json(400, { ok: false, erro: "dê uma nota de 1 a 5 antes de enviar" });
  if (nome.length < 2)  return json(400, { ok: false, erro: "escreva um nome (2 caracteres ou mais)" });
  if (texto.length < 4) return json(400, { ok: false, erro: "escreva sua avaliação" });

  const visitante = await idVisitante(request, env);
  const t = agora();

  // A nota entra SEMPRE, mesmo que o texto acabe descartado adiante: a pessoa
  // escolheu uma nota, e ela é dela. Como é upsert na linha única de `votos`,
  // não há risco de contar duas vezes.
  await gravarNota(env, visitante, nota);

  const respostaPadrao = json(200, { ok: true, estado: "pendente" });

  // --------- descarte silencioso: daqui para baixo, a resposta é sempre a mesma.
  // Dizer "você está rápido demais" ou "isso é duplicado" ensina o robô a ajustar
  // o ritmo e o texto. Como TUDO passa por moderação e nada aparece na hora, a
  // mesma mensagem serve para o visitante honesto sem confundi-lo.

  // 1. Honeypot: campo escondido no formulário, humano nunca vê.
  if (String(corpo.website ?? "").trim() !== "") return respostaPadrao;

  // 2. Tempo de página: formulário respondido em menos de 3 s não é gente.
  const ms = Number(corpo.t);
  if (Number.isFinite(ms) && ms >= 0 && ms < TEMPO_MINIMO_MS) return respostaPadrao;

  // 3. Conteúdo repetido, INDEPENDENTE de quem mandou. É esta checagem — e não o
  //    teto por visitante — que pega o flooder que troca de IP/rede a cada envio.
  const norm = normalizarTexto(texto);
  if (norm.length >= 8) {
    const igual = await env.DB.prepare(
      "SELECT 1 AS x FROM comentarios WHERE texto_norm = ? AND criado_em > ? LIMIT 1"
    ).bind(norm, t - DUPLICATA_DIAS * 86400).first();
    if (igual) return respostaPadrao;
  }

  // 4. Teto global por hora: último anteparo, não depende de identificar ninguém.
  //    Protege de ataque distribuído sem silenciar quem usa em condição normal.
  const naHora = await env.DB.prepare(
    "SELECT COUNT(*) AS n FROM comentarios WHERE criado_em > ?"
  ).bind(t - 3600).first();
  if ((naHora?.n || 0) >= AVAL_GLOBAL_HORA) return respostaPadrao;

  // 5. Tetos por visitante — folgados de propósito (ver comentário em AVAL_POR_DIA).
  const recente = await env.DB.prepare(
    "SELECT 1 AS x FROM comentarios WHERE visitante = ? AND criado_em > ? LIMIT 1"
  ).bind(visitante, t - AVAL_INTERVALO_S).first();
  if (recente) return respostaPadrao;

  const doDia = await env.DB.prepare(
    "SELECT COUNT(*) AS n FROM comentarios WHERE visitante = ? AND criado_em > ?"
  ).bind(visitante, t - 86400).first();
  if ((doDia?.n || 0) >= AVAL_POR_DIA) return respostaPadrao;

  const r = await env.DB.prepare(
    "INSERT INTO comentarios (nome, texto, texto_norm, visitante, estado, curtidas, criado_em) " +
    "VALUES (?, ?, ?, ?, 'pendente', 0, ?)"
  ).bind(nome, texto, norm, visitante, t).run();

  ctx.waitUntil(avisarTelegram(env, nome, nota, texto, r?.meta?.last_row_id));
  return respostaPadrao;
}

/** Aviso opcional no Telegram. Só roda se os dois secrets existirem, e uma falha
 *  aqui nunca chega ao visitante — a avaliação já está salva. */
async function avisarTelegram(env, nome, nota, texto, id) {
  if (!env.TG_BOT_TOKEN || !env.TG_ADMIN_CHAT) return;
  const msg = `⭐ Avaliação nova na página /dlss (id ${id ?? "?"})\n\n` +
              `De: ${nome} — nota ${nota}/5\n\n${texto.slice(0, 400)}\n\n` +
              `Aprovar em ${SITE}/dlss-api/admin?token=…`;
  try {
    await fetch(`https://api.telegram.org/bot${env.TG_BOT_TOKEN}/sendMessage`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ chat_id: env.TG_ADMIN_CHAT, text: msg, disable_web_page_preview: true }),
    });
  } catch { /* silêncio: aviso é conveniência, não parte do fluxo */ }
}

// ------------------------------------------------------------------ POST /curtida
async function rotaCurtida(request, env) {
  const corpo = await corpoJson(request);
  const id = Number(corpo.id);
  if (!Number.isInteger(id) || id <= 0) return json(400, { ok: false, erro: "id inválido" });

  const visitante = await idVisitante(request, env);
  const t = agora();

  // Só avaliação APROVADA aceita curtida: aceitar numa pendente confirmaria a
  // existência dela para quem só teria de saber depois de publicada.
  const alvo = await env.DB.prepare(
    "SELECT id, curtidas FROM comentarios WHERE id = ? AND estado = 'aprovado'"
  ).bind(id).first();
  if (!alvo) return json(404, { ok: false, erro: "avaliação não encontrada" });

  const jaCurtiu = await env.DB.prepare(
    "SELECT 1 AS x FROM curtidas WHERE comentario_id = ? AND visitante = ?"
  ).bind(id, visitante).first();

  if (jaCurtiu) {
    // Descurtir. GREATEST não existe no SQLite; o MAX(a,b) escalar existe e evita
    // que um clique duplo em corrida deixe o rollup negativo.
    await env.DB.batch([
      env.DB.prepare("DELETE FROM curtidas WHERE comentario_id = ? AND visitante = ?").bind(id, visitante),
      env.DB.prepare("UPDATE comentarios SET curtidas = MAX(curtidas - 1, 0) WHERE id = ?").bind(id),
    ]);
    const n = await env.DB.prepare("SELECT curtidas FROM comentarios WHERE id = ?").bind(id).first();
    return json(200, { ok: true, curtiu: false, curtidas: n?.curtidas || 0 });
  }

  // Teto só contra laço automatizado — curtir várias avaliações é uso normal.
  const naHora = await env.DB.prepare(
    "SELECT COUNT(*) AS n FROM curtidas WHERE visitante = ? AND criado_em > ?"
  ).bind(visitante, t - 3600).first();
  if ((naHora?.n || 0) >= CURTIDAS_POR_HORA) {
    return json(200, { ok: true, curtiu: false, curtidas: alvo.curtidas || 0 });
  }

  await env.DB.batch([
    env.DB.prepare("INSERT INTO curtidas (comentario_id, visitante, criado_em) VALUES (?, ?, ?)")
      .bind(id, visitante, t),
    env.DB.prepare("UPDATE comentarios SET curtidas = curtidas + 1 WHERE id = ?").bind(id),
  ]);
  const n = await env.DB.prepare("SELECT curtidas FROM comentarios WHERE id = ?").bind(id).first();
  return json(200, { ok: true, curtiu: true, curtidas: n?.curtidas || 0 });
}

// ------------------------------------------------------------------ moderação
function paginaAdmin(pendentes, aprovados, token) {
  const estrelas = (n) => (n ? "★".repeat(n) + "☆".repeat(5 - n) : "sem nota");
  const linha = (c, aprovado) => `
    <article class="c">
      <header>
        <span><b>${esc(c.nome)}</b> <i class="st">${esc(estrelas(c.nota))}</i></span>
        <time>${esc(new Date(c.criado_em * 1000).toLocaleString("pt-BR"))}</time>
      </header>
      <p>${esc(c.texto).replace(/\n/g, "<br>")}</p>
      <div class="acoes">
        ${aprovado
          ? `<span class="cur">${c.curtidas || 0} curtida(s)</span>
             <button data-id="${c.id}" data-acao="recusar">Tirar do ar</button>`
          : `<button class="ok" data-id="${c.id}" data-acao="aprovar">Aprovar</button>
             <button data-id="${c.id}" data-acao="recusar">Recusar</button>`}
        <button class="del" data-id="${c.id}" data-acao="apagar">Apagar</button>
      </div>
    </article>`;

  return `<!DOCTYPE html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex,nofollow">
<title>Moderação — /dlss</title><style>
 body{background:#0b1017;color:#d1d5db;font:15px/1.6 system-ui,Segoe UI,sans-serif;margin:0;padding:26px}
 .w{max-width:760px;margin:0 auto}
 h1{font-size:19px;color:#fff;margin:0 0 4px}h2{font-size:14px;color:#06b6d4;text-transform:uppercase;
   letter-spacing:.12em;margin:28px 0 12px}
 .c{background:#111827;border:1px solid #243244;border-radius:12px;padding:14px 16px;margin-bottom:11px}
 .c header{display:flex;justify-content:space-between;gap:12px;font-size:13px;margin-bottom:7px}
 .c b{color:#fff}.c time{color:#6b7280}.c .st{color:#facc15;font-style:normal;letter-spacing:2px}
 .c p{margin:0 0 11px;white-space:pre-wrap;overflow-wrap:anywhere}
 .cur{font-size:12.5px;color:#6b7280;margin-right:10px}
 button{font:inherit;font-size:13px;background:#1f2937;color:#d1d5db;border:1px solid #33445c;
   border-radius:8px;padding:6px 13px;cursor:pointer;margin-right:7px}
 button:hover{border-color:#06b6d4;color:#fff}
 button.ok{background:linear-gradient(135deg,#0891b2,#2563eb);color:#fff;border-color:transparent}
 button.del{color:#ef4444}
 .vazio{color:#6b7280;font-style:italic}
</style></head><body><div class="w">
<h1>Moderação da página /dlss</h1>
<p style="color:#6b7280;font-size:13px;margin:0">Avaliação só aparece no site depois de aprovada aqui.</p>
<h2>Esperando aprovação (${pendentes.length})</h2>
${pendentes.length ? pendentes.map((c) => linha(c, false)).join("") : '<p class="vazio">Nada na fila.</p>'}
<h2>No ar (${aprovados.length})</h2>
${aprovados.length ? aprovados.map((c) => linha(c, true)).join("") : '<p class="vazio">Nenhuma avaliação publicada ainda.</p>'}
</div><script>
 const TOKEN = ${JSON.stringify(token)};
 document.addEventListener("click", async (e) => {
   const b = e.target.closest("button[data-acao]"); if (!b) return;
   if (b.dataset.acao === "apagar" && !confirm("Apagar de vez esta avaliação?")) return;
   b.disabled = true;
   const r = await fetch("/dlss-api/admin/moderar", {
     method: "POST", headers: { "Content-Type": "application/json" },
     body: JSON.stringify({ token: TOKEN, id: Number(b.dataset.id), acao: b.dataset.acao })
   });
   if (r.ok) location.reload(); else { b.disabled = false; alert("Falhou: " + r.status); }
 });
</script></body></html>`;
}

async function rotaAdmin(request, env, url) {
  if (!env.ADMIN_TOKEN) return new Response("Moderação não configurada", { status: 503 });
  if (!tokenBate(url.searchParams.get("token"), env.ADMIN_TOKEN)) {
    return new Response("Não autorizado", { status: 401 });
  }
  const campos = "SELECT c.id, c.nome, c.texto, c.criado_em, c.curtidas, v.nota " +
                 "FROM comentarios c LEFT JOIN votos v ON v.visitante = c.visitante ";
  const pend = await env.DB.prepare(
    campos + "WHERE c.estado = 'pendente' ORDER BY c.criado_em ASC"
  ).all();
  const apro = await env.DB.prepare(
    campos + "WHERE c.estado = 'aprovado' ORDER BY c.criado_em DESC LIMIT 100"
  ).all();
  return new Response(paginaAdmin(pend.results || [], apro.results || [], env.ADMIN_TOKEN), {
    status: 200,
    headers: {
      "Content-Type": "text/html;charset=utf-8",
      "Cache-Control": "no-store",
      "X-Robots-Tag": "noindex, nofollow",
    },
  });
}

async function rotaModerar(request, env) {
  if (!env.ADMIN_TOKEN) return json(503, { ok: false, erro: "moderação não configurada" });
  const corpo = await corpoJson(request);
  if (!tokenBate(corpo.token, env.ADMIN_TOKEN)) return json(401, { ok: false, erro: "não autorizado" });

  const id = Number(corpo.id);
  if (!Number.isInteger(id) || id <= 0) return json(400, { ok: false, erro: "id inválido" });

  if (corpo.acao === "apagar") {
    await env.DB.batch([
      env.DB.prepare("DELETE FROM curtidas WHERE comentario_id = ?").bind(id),
      env.DB.prepare("DELETE FROM comentarios WHERE id = ?").bind(id),
    ]);
    return json(200, { ok: true });
  }
  const estado = corpo.acao === "aprovar" ? "aprovado" : corpo.acao === "recusar" ? "recusado" : null;
  if (!estado) return json(400, { ok: false, erro: "ação inválida" });

  await env.DB.prepare("UPDATE comentarios SET estado = ?, moderado_em = ? WHERE id = ?")
    .bind(estado, agora(), id).run();
  return json(200, { ok: true, estado });
}

// ------------------------------------------------------------------ roteador
export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const p = url.pathname;
    const m = request.method;

    if (m === "OPTIONS") {
      return new Response(null, {
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": SITE,
          "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type",
          "Access-Control-Max-Age": "86400",
          "Vary": "Origin",
        },
      });
    }

    // A rota /admin não depende do D1 estar são para responder 401 a quem não tem token.
    if (m === "GET"  && p === "/dlss-api/admin")         return rotaAdmin(request, env, url);
    if (m === "POST" && p === "/dlss-api/admin/moderar") return rotaModerar(request, env);

    if (!env.DB) return json(503, { ok: false, erro: "banco indisponível" });

    try {
      if (m === "GET"  && p === "/dlss-api/estado")    return await rotaEstado(request, env);
      if (m === "POST" && p === "/dlss-api/download")  return await rotaDownload(request, env, ctx);
      if (m === "POST" && p === "/dlss-api/voto")      return await rotaVoto(request, env);
      if (m === "POST" && p === "/dlss-api/avaliacao") return await rotaAvaliacao(request, env, ctx);
      if (m === "POST" && p === "/dlss-api/curtida")   return await rotaCurtida(request, env);
    } catch (e) {
      // Erro do banco vira 503 com corpo previsível: a página trata isso escondendo
      // os números em vez de mostrar zero, que seria mentira.
      console.log("erro:", e && e.message);
      return json(503, { ok: false, erro: "indisponível" });
    }

    return json(404, { ok: false, erro: "rota desconhecida" });
  },
};
