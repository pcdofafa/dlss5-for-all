/* =====================================================================
   shared_source.h -- le a saida da camada de BAIXO por TEXTURA COMPARTILHADA,
   no lugar de captura-la pelo compositor (lado CONSUMIDOR, dentro do app).

   O outro lado e addon\shared_frame.h, dentro do add-on da ponte; o cabecalho
   dele explica o protocolo e o porque. Aqui fica so o que o app precisa saber:

     - o produtor mantem um mapeamento de memoria nomeado com um cabecalho e
       uma textura compartilhada nomeada, por geracao;
     - "geracao" muda quando a resolucao muda: reabrir a textura e obrigatorio;
     - a sincronizacao e por keyed mutex de chave unica (0). O produtor NUNCA
       espera; nos esperamos um pouco, e so copiamos quando o numero do quadro
       mudou -- assim cada quadro dele vira exatamente um nosso, e quando nao
       da, sabemos exatamente quantos perdemos.

   O QUE ESTE MODULO NAO FAZ, de proposito: ele nao decide nada. Se o produtor
   sumir, se a versao nao bater ou se o formato nao servir, ele apenas devolve
   "sem imagem" e conta o tempo; quem decide voltar para a captura por
   compositor e o main.cpp, com o motivo escrito no log. Um transporte que
   escolhe sozinho o proprio substituto e um transporte que esconde falha.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <string>

namespace SharedSource {

/* O MESMO cabecalho do addon\shared_frame.h. Duplicado de proposito: o add-on
   e compilado sozinho, com os headers do ReShade, e o app nao inclui nada dele.
   Se um dos dois mudar sem o outro, a checagem de versao abaixo recusa o
   transporte e a camada volta para o compositor -- em vez de ler lixo. */
constexpr uint32_t kMagic   = 0x44355346; /* 'D5SF' */
constexpr uint32_t kVersion = 2;   /* 2 acrescentou modo/slots/slot no fim */

struct Header
{
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t generation;
    uint64_t frame;
    uint64_t qpc;
    uint32_t producerPid;
    uint32_t publishing;
    wchar_t  texName[96];
    /* versao 2: o modo do transporte e o anel. Um produtor versao 1 nao tem
       estes campos; o consumidor trata ausencia como modo mutex. */
    uint32_t modo;
    uint32_t slots;
    uint32_t slot;
    uint32_t reservado;
};

/* Os mesmos do lado do add-on. Duplicados de proposito, como o resto. */
constexpr uint32_t kModoMutex   = 1;
constexpr uint32_t kModoRotacao = 2;
constexpr uint32_t kMaxSlots    = 4;

/* Quanto tempo sem quadro novo antes de desistir do transporte. 500 ms e o que
   o pedido manda, e e folgado de proposito: a camada de baixo pode ficar sem
   apresentar quando a fonte dela para (video pausado, jogo em menu) sem que
   nada esteja errado. */
constexpr DWORD kTimeoutMs = 500;

class Reader
{
public:
    /* Abre esperando: a camada de baixo pede a publicacao ANTES de nos criar, mas
       quem cria de fato a textura e o add-on dela, no primeiro finish_effects.
       Medido: 1,0 s entre um e outro (a camada de cima chegava 280 ms cedo e
       desistia). Esperar aqui e barato -- a camada de cima leva 4 s so para a
       rede dela subir -- e e a diferenca entre usar o transporte e nunca usar. */
    bool AbrirEsperando(const std::wstring &nome, ID3D11Device *dev, DWORD esperaMs, std::wstring *erro)
    {
        const ULONGLONG ate = GetTickCount64() + esperaMs;
        for (;;) {
            if (Abrir(nome, dev, erro)) {
                /* aberto nao basta: a geracao so aparece quando a textura existe */
                for (;;) {
                    if (m_head != nullptr && m_head->magic == kMagic && m_head->generation != 0) return true;
                    if (GetTickCount64() >= ate) { if (erro) *erro = L"a camada de baixo nao publicou a tempo"; Fechar(); return false; }
                    Sleep(30);
                }
            }
            if (GetTickCount64() >= ate) return false;
            Sleep(30);
        }
    }

    /* Abre o mapeamento. Nao abre a textura ainda -- isso so da para fazer
       quando houver geracao publicada. Devolve false com o motivo em `erro`. */
    bool Abrir(const std::wstring &nome, ID3D11Device *dev, std::wstring *erro)
    {
        Fechar();
        m_nome = nome;
        m_dev = dev;
        if (m_dev == nullptr) { if (erro) *erro = L"sem device D3D11"; return false; }
        m_dev->AddRef();
        if (FAILED(m_dev->QueryInterface(__uuidof(ID3D11Device1), (void **)&m_dev1)) || m_dev1 == nullptr)
        { if (erro) *erro = L"o device nao tem ID3D11Device1"; Fechar(); return false; }
        m_dev->GetImmediateContext(&m_ctx);
        if (m_ctx == nullptr) { if (erro) *erro = L"sem contexto imediato"; Fechar(); return false; }

        m_map = OpenFileMappingW(FILE_MAP_READ, FALSE, m_nome.c_str());
        if (m_map == nullptr)
        { if (erro) *erro = L"a camada de baixo ainda nao publicou (mapeamento inexistente)"; Fechar(); return false; }
        m_head = (const Header *)MapViewOfFile(m_map, FILE_MAP_READ, 0, 0, sizeof(Header));
        if (m_head == nullptr) { if (erro) *erro = L"MapViewOfFile falhou"; Fechar(); return false; }
        m_ultimoOk = GetTickCount64();
        return true;
    }

    void Fechar()
    {
        SoltarTextura();
        if (m_head != nullptr) { UnmapViewOfFile((LPCVOID)m_head); m_head = nullptr; }
        if (m_map  != nullptr) { CloseHandle(m_map); m_map = nullptr; }
        if (m_ctx  != nullptr) { m_ctx->Release();  m_ctx = nullptr; }
        if (m_dev1 != nullptr) { m_dev1->Release(); m_dev1 = nullptr; }
        if (m_dev  != nullptr) { m_dev->Release();  m_dev = nullptr; }
        m_geracao = 0; m_ultimoFrame = 0; m_w = m_h = 0;
    }

    bool Aberto() const { return m_head != nullptr; }
    UINT Largura() const { return m_w; }
    UINT Altura()  const { return m_h; }
    uint64_t QuadrosLidos()  const { return m_lidos; }
    uint64_t QuadrosPulados() const { return m_pulados; }
    /* copias descartadas por leitura suja (so no anel). Zero = o anel bastou. */
    uint64_t QuadrosSujos()  const { return m_sujos; }
    /* qual dos caminhos esta em uso, para o log dizer e a rodada poder ser
       conferida: rotulo que discorda do processo ja custou quatro tabelas. */
    const char *ModoTexto() const { return (m_modo == kModoRotacao) ? "ROTACAO" : "MUTEX"; }
    uint32_t Slots() const { return m_slots; }
    uint64_t UltimoQuadro()  const { return m_ultimoFrame; }
    /* quanto tempo faz que nao chega quadro novo */
    ULONGLONG ParadoMs() const { return GetTickCount64() - m_ultimoOk; }

    /* Copia o quadro novo, se houver, para `destino` (mesma resolucao e formato).
       Devolve:
          1  copiou um quadro novo
          0  nao ha quadro novo agora (normal quando a fonte de baixo esta parada)
         -1  o transporte caiu: versao errada, produtor fechou, ou o tempo
             estourou. `motivo` diz qual. Quem decide o que fazer e o chamador. */
    int Ler(ID3D11Texture2D *destino, std::wstring *motivo)
    {
        if (m_head == nullptr) { if (motivo) *motivo = L"nao aberto"; return -1; }

        const uint32_t magic = m_head->magic;
        const uint32_t ver   = m_head->version;
        if (magic != kMagic || ver < 1 || ver > kVersion)
        {
            /* Ainda pode ser o instante entre CreateFileMapping (zerado) e a
               primeira gravacao do cabecalho: so vira erro quando o tempo
               estoura, la embaixo. */
            if (ParadoMs() > kTimeoutMs)
            {
                if (motivo) *motivo = (magic == 0)
                    ? L"a camada de baixo nao chegou a publicar nada"
                    : L"versao do protocolo diferente entre o app e o add-on";
                return -1;
            }
            return 0;
        }

        if (m_head->publishing == 0 && ParadoMs() > kTimeoutMs)
        { if (motivo) *motivo = L"a camada de baixo parou de publicar"; return -1; }

        const uint32_t ger = m_head->generation;
        if (ger == 0) { if (ParadoMs() > kTimeoutMs) { if (motivo) *motivo = L"sem geracao publicada"; return -1; } return 0; }

        if (ger != m_geracao)
        {
            if (!AbrirTextura(ger, motivo)) return (ParadoMs() > kTimeoutMs) ? -1 : 0;
        }

        const uint64_t f = m_head->frame;
        if (f == m_ultimoFrame)
        {
            if (ParadoMs() > kTimeoutMs) { if (motivo) *motivo = L"sem quadro novo por mais de meio segundo"; return -1; }
            return 0;
        }

        /* ---- MODO ROTACAO: sem cadeado, com deteccao de leitura suja ----
           Le o slot anunciado e confere o numero do quadro DEPOIS da copia. Se
           o produtor avancou o bastante para dar a volta no anel enquanto
           copiavamos, o slot pode ter sido reescrito no meio: a copia e
           DESCARTADA e contada em m_sujos. Isso e o tratamento explicito do
           risco de o anel nao bastar - e a contagem e que diz se ele bastou.
           Um quadro rasgado num video passa despercebido; aqui ele alimenta uma
           rede temporal e vira artefato que se acumula. */
        if (m_modo == kModoRotacao)
        {
            const uint32_t s = m_head->slot;
            if (s >= m_slots || m_slotTex[s] == nullptr) { ++m_pulados; return 0; }
            m_ctx->CopyResource(destino, m_slotTex[s]);
            MemoryBarrier();
            const uint64_t depois = m_head->frame;
            if (depois - f >= m_slots)   /* deu a volta: o slot pode ter sido reescrito */
            { ++m_sujos; return 0; }
            if (m_ultimoFrame != 0 && f > m_ultimoFrame + 1) m_pulados += (f - m_ultimoFrame - 1);
            m_ultimoFrame = f;
            ++m_lidos;
            m_ultimoOk = GetTickCount64();
            return 1;
        }

        if (m_mutex == nullptr || m_tex == nullptr) return 0;
        /* Esperamos NOS, e nao o produtor: 8 ms e pouco mais de meio quadro a
           60 fps. Se nao der, o proximo laco tenta de novo e o contador de
           pulados registra. */
        const HRESULT hr = m_mutex->AcquireSync(0, 8);
        if (hr != WAIT_OBJECT_0) { ++m_pulados; return 0; }

        m_ctx->CopyResource(destino, m_tex);
        m_mutex->ReleaseSync(0);

        /* O salto entre o quadro anterior e este e o que diz se o transporte
           esta 1 para 1. Salto de 1 = perfeito; maior = perdemos os do meio. */
        if (m_ultimoFrame != 0 && f > m_ultimoFrame + 1) m_pulados += (f - m_ultimoFrame - 1);
        m_ultimoFrame = f;
        ++m_lidos;
        m_ultimoOk = GetTickCount64();
        return 1;
    }

    /* Cria a textura de destino do tamanho e formato que o produtor publicou.
       O chamador e quem decide quando (a resolucao pode mudar em voo). */
    bool CriarDestino(ID3D11Texture2D **out, std::wstring *motivo)
    {
        if (out == nullptr || m_head == nullptr) return false;
        *out = nullptr;
        const uint32_t w = m_head->width, h = m_head->height, fmt = m_head->format;
        if (w == 0 || h == 0) { if (motivo) *motivo = L"o produtor ainda nao disse a resolucao"; return false; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = (DXGI_FORMAT)fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const HRESULT hr = m_dev->CreateTexture2D(&td, nullptr, out);
        if (FAILED(hr)) { if (motivo) *motivo = L"CreateTexture2D do destino falhou"; return false; }
        m_w = w; m_h = h;
        return true;
    }

    uint32_t Geracao() const { return m_head ? m_head->generation : 0; }
    uint32_t Formato() const { return m_head ? m_head->format : 0; }

private:
    void SoltarTextura()
    {
        if (m_mutex != nullptr) { m_mutex->Release(); m_mutex = nullptr; }
        /* solta o anel inteiro. m_tex aponta para m_slotTex[0], entao NAO se
           solta os dois - seria um Release a mais. */
        for (uint32_t i = 0; i < kMaxSlots; ++i)
            if (m_slotTex[i] != nullptr) { m_slotTex[i]->Release(); m_slotTex[i] = nullptr; }
        m_tex = nullptr;
    }

    bool AbrirTextura(uint32_t ger, std::wstring *motivo)
    {
        SoltarTextura();
        /* O MODO VEM DO PRODUTOR, sempre. O consumidor nao escolhe e nao tem
           configuracao propria: um produtor versao 1 nao tem o campo, e ai e
           mutex, que era o unico que existia. */
        m_modo  = (m_head->version >= 2 && m_head->modo == kModoRotacao) ? kModoRotacao : kModoMutex;
        m_slots = 1;
        if (m_modo == kModoRotacao)
        {
            m_slots = m_head->slots;
            if (m_slots < 2 || m_slots > kMaxSlots)
            { if (motivo) *motivo = L"o produtor anunciou um anel de tamanho invalido"; return false; }
            for (uint32_t i = 0; i < m_slots; ++i)
            {
                wchar_t nm[128];
                _snwprintf(nm, 128, L"%ls_tex_%u_%u", m_head->texName, (unsigned)ger, (unsigned)i);
                nm[127] = L'\0';
                ID3D11Texture2D *ti = nullptr;
                if (FAILED(m_dev1->OpenSharedResourceByName(nm, DXGI_SHARED_RESOURCE_READ,
                                                            __uuidof(ID3D11Texture2D), (void **)&ti)) || ti == nullptr)
                { if (motivo) *motivo = L"OpenSharedResourceByName falhou num slot do anel"; SoltarTextura(); return false; }
                m_slotTex[i] = ti;
            }
            m_tex   = m_slotTex[0];
            m_mutex = nullptr;          /* nao existe no anel, e e exatamente o ponto */
            m_geracao = ger;
            m_ultimoFrame = 0;
            return true;
        }
        wchar_t nome[96];
        wcsncpy(nome, m_head->texName, 95); nome[95] = L'\0';
        if (nome[0] == L'\0') { if (motivo) *motivo = L"o produtor nao nomeou a textura"; return false; }
        ID3D11Texture2D *t = nullptr;
        const HRESULT hr = m_dev1->OpenSharedResourceByName(nome, DXGI_SHARED_RESOURCE_READ,
                                                            __uuidof(ID3D11Texture2D), (void **)&t);
        if (FAILED(hr) || t == nullptr)
        {
            if (motivo) *motivo = L"OpenSharedResourceByName falhou";
            return false;
        }
        t->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&m_mutex);
        m_tex = t;
        m_slotTex[0] = t;
        m_geracao = ger;
        m_ultimoFrame = 0;   /* geracao nova: o primeiro quadro dela e sempre novo */
        return true;
    }

    std::wstring m_nome;
    ID3D11Device        *m_dev  = nullptr;
    ID3D11Device1       *m_dev1 = nullptr;
    ID3D11DeviceContext *m_ctx  = nullptr;
    HANDLE        m_map  = nullptr;
    const Header *m_head = nullptr;
    ID3D11Texture2D *m_tex = nullptr;
    IDXGIKeyedMutex *m_mutex = nullptr;
    ID3D11Texture2D *m_slotTex[kMaxSlots] = { nullptr, nullptr, nullptr, nullptr };
    uint32_t  m_modo = kModoMutex, m_slots = 1;
    uint64_t  m_sujos = 0;      /* copias descartadas por o produtor ter dado a volta */
    uint32_t  m_geracao = 0;
    uint64_t  m_ultimoFrame = 0, m_lidos = 0, m_pulados = 0;
    ULONGLONG m_ultimoOk = 0;
    UINT      m_w = 0, m_h = 0;
};

} // namespace SharedSource
