/*
    comparar_lado_a_lado.h -- o MODO COMPARATIVO da camada: metade esquerda com
    a imagem ORIGINAL, metade direita com o DLSS 5 aplicado.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).

    ------------------------------------------------------------------------
    PARA QUE SERVE
    ------------------------------------------------------------------------
    Pedido do usuario em 11/09/2026, para gravar video de comparacao: "a tela
    dividida ao meio, esquerda sem DLSS 5 e direita com". Sem isto, comparar
    exige apertar F6 e cortar o video -- e ninguem consegue mostrar as duas
    metades do MESMO quadro, que e o que convence.

    ------------------------------------------------------------------------
    ONDE ISTO ENTRA, E POR QUE E EXATAMENTE AQUI
    ------------------------------------------------------------------------
    Dois ganchos, os mesmos da mistura_guiada.h:

        reshade_begin_effects  -> o back buffer ainda e o NATIVO (nada rodou).
                                  Guardamos uma copia inteira.
        reshade_finish_effects -> o back buffer JA e a saida da cadeia.
                                  Devolvemos a METADE ESQUERDA da copia.

    O finish e o ponto certo, e isto foi conferido e nao suposto: o Neural
    Rendering do RenoDX nao e uma tecnica do ReShade -- ele intercepta o
    NVSDK_NGX_*_EvaluateFeature com replace_source=true, ou seja, roda DENTRO
    da chamada NGX do nosso DLSS5_Feed, que E uma tecnica. Logo, quando o
    finish_effects acontece, tudo ja passou: SMAA, Lumenite_Kernel, o Feed com
    o NR dentro dele, e o Sharpen.

    ------------------------------------------------------------------------
    O QUE ESTE MODO NAO MOSTRA, e esta escrito porque a pessoa VAI perguntar
    ------------------------------------------------------------------------
    No quadro SINTETIZADO (o "DLSS Frame Skipping") a cadeia inteira esta
    suprimida: nada roda entre o begin e o finish, e as duas metades ficam
    iguais -- as duas com o quadro deformado pelo campo de movimento. Nao e
    defeito e nao da para contornar aqui: naquele quadro nao existe "com DLSS"
    e "sem DLSS", existe um quadro desenhado por nos. Para gravar comparacao,
    deixe o DLSS FRAME SKIPPING em "Desligado".

    ------------------------------------------------------------------------
    CUSTO
    ------------------------------------------------------------------------
    Duas copias de GPU por quadro: uma inteira no begin e uma de meia tela no
    finish. A 1080p sao ~8 MB + ~4 MB de trafego, na casa de 0,1 a 0,3 ms --
    e so quando o modo esta LIGADO (desligado, a primeira linha de cada funcao
    devolve).
*/
#pragma once

#include <d3d11.h>
#include <cstdio>
#include <cstring>

#include "rotulos_comparar.h"

namespace dlss5_comparar
{

class Comparador
{
public:
    Comparador() = default;
    ~Comparador() { soltar(); }
    Comparador(const Comparador &) = delete;
    Comparador &operator=(const Comparador &) = delete;

    /* divisor em porcento (0..100): onde fica a linha. 50 = meio a meio.
       rotulos: desenhar as marcas "ORIGINAL" / "DLSS 5 FOR ALL" no alto de
       cada metade (as artes de <exeDir>\marca\, ver rotulos_comparar.h).
       rotuloAltura: altura das marcas em porcento da altura da tela (5 a 15).
       atraso: quantos quadros o lado ESQUERDO recua para casar com o direito
               (ver o bloco A COMPENSACAO DO ATRASO). 0 desliga.
       exeDir: a pasta do executavel, COM a barra final. */
    void configurar(bool ligado, int divisor, bool rotulos, int rotuloAltura,
                    int atraso, const wchar_t *exeDir, const wchar_t *atalho = nullptr)
    {
        /* A TECLA NO CABECALHO (14/09/2026). Trocar o texto obriga a refazer a
           arte, entao so invalida quando ele de fato muda - a pessoa pode
           remapear o atalho com o app aberto. */
        if (atalho != nullptr && wcsncmp(atalho, m_atalho, 63) != 0)
        { wcsncpy(m_atalho, atalho, 63); m_atalho[63] = L'\0'; m_rotVale = false; }
        m_quer = ligado;
        m_div = (divisor >= 5 && divisor <= 95) ? divisor : 50;
        if (rotulos != m_rotulos) { m_rotulos = rotulos; m_rotVale = false; }
        const int alt = (rotuloAltura >= 5 && rotuloAltura <= 15) ? rotuloAltura : 9;
        if (alt != m_rotPct) { m_rotPct = alt; m_rotVale = false; }
        const int atr = (atraso >= 0 && atraso <= (kGuardados - 1)) ? atraso : 0;
        /* Mudar o atraso muda QUANTAS copias precisam existir; refazer e mais
           simples (e acontece uma vez) do que manter texturas que ninguem le. */
        if (atr != m_atraso) { m_atraso = atr; soltar(); }
        if (exeDir != nullptr && wcsncmp(exeDir, m_dir, MAX_PATH - 1) != 0)
        { wcsncpy(m_dir, exeDir, MAX_PATH - 1); m_dir[MAX_PATH - 1] = L'\0'; m_rotVale = false; }
        if (!ligado) m_guardados = 0;
    }
    bool ligado() const { return m_quer; }

    /* reshade_begin_effects: guarda o quadro ANTES da cadeia. */
    void capturar_original(void *rtvHandle, void (*log)(const char *))
    {
        if (!m_quer || rtvHandle == nullptr) return;
        ID3D11Texture2D *bb = nullptr; ID3D11Device *dev = nullptr;
        if (!abrir(rtvHandle, &bb, &dev)) return;
        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        if (preparar(dev, d, log))
        {
            ID3D11DeviceContext *ctx = nullptr; dev->GetImmediateContext(&ctx);
            if (ctx)
            {
                /* Roda de (atraso + 1) posicoes: escrevemos sempre na seguinte
                   e a leitura escolhe quantas voltar. Com o padrao medido
                   (atraso 0) a roda tem uma posicao so e isto e exatamente o
                   que era antes -- nenhuma textura a mais na VRAM. */
                m_escreve = (m_escreve + 1) % nTex();
                ctx->CopyResource(m_original[m_escreve], bb);
                ctx->Release();
                if (m_guardados < nTex()) m_guardados++;
            }
        }
        bb->Release(); dev->Release();
    }

    /* reshade_finish_effects: devolve a metade esquerda do que foi guardado. */
    void aplicar(void *rtvHandle, void (*log)(const char *))
    {
        if (!m_quer || m_guardados == 0 || rtvHandle == nullptr) return;
        ID3D11Texture2D *bb = nullptr; ID3D11Device *dev = nullptr;
        if (!abrir(rtvHandle, &bb, &dev)) return;
        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        /* O back buffer mudou de tamanho entre o begin e o finish? Entao a
           copia guardada e de outro quadro e nao serve. Acontece na troca de
           resolucao; o proximo begin refaz tudo. */
        if (d.Width != m_w || d.Height != m_h) { m_guardados = 0; bb->Release(); dev->Release(); return; }

        ID3D11DeviceContext *ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (ctx == nullptr) { bb->Release(); dev->Release(); return; }

        /* ------------------------------------------------------------------
           A COMPENSACAO DO ATRASO, E POR QUE ELA E ZERO
           ------------------------------------------------------------------
           A suspeita era que a cadeia entregasse no finish deste quadro uma
           imagem construida a partir do quadro ANTERIOR, e que por isso a
           emenda saltasse com a cena em movimento.

           MEDIDO, e a suspeita estava ERRADA. Bateria de 45 fotos (alvo
           diagonal a 8 px/quadro, tres condicoes x tres valores de atraso x
           cinco fotos), metrica = degrau na emenda dividido pelo degrau tipico
           da propria imagem no mesmo passo de 3 px (1,00 = a emenda e tao
           suave quanto o resto):

               condicao          atraso 0   atraso 1   atraso 2
               parado              1,00       0,99       1,00
               movimento           0,87       1,78       2,31
               skipping 1 de 2     0,40       2,97       2,23

           Ou seja: sem compensacao JA esta alinhado, e compensar PIORA na
           medida exata de um quadro de deslocamento. O begin e o finish sao o
           mesmo Present e a cadeia nao defasa nada.

           O salto que aparecia em fotos avulsas (uma delas com razao 4,24)
           nao e defasagem do pipeline: se fosse, apareceria nas cinco fotos da
           mesma condicao, e apareceu em uma. E rasgo da CAPTURA - o
           PrintWindow pegando a janela no meio de uma troca de quadro; a foto
           campea mostrava, alem da emenda, uma segunda costura no meio da
           metade direita, que a divisao ao meio nao explica.

           A chave [comparar] atraso fica, com padrao 0, para o caso de alguem
           montar uma cadeia que de fato defase. Recuar nao quebra nada - so
           mostra a esquerda mais velha. */
        int quantos = m_atraso;
        if (quantos > m_guardados - 1) quantos = m_guardados - 1;   /* ainda enchendo a roda */
        const int slot = (m_escreve - quantos + nTex() * 2) % nTex();

        const UINT corte = (UINT)((double)m_w * (double)m_div / 100.0);
        /* LADOS INVERTIDOS EM 14/09/2026, a pedido do usuario: "nesse modo o
           preset com dlss5 deveria ser do lado esquerdo e o sem do lado
           direito". Antes era o contrario (esquerda original, direita com).
           Ele tem razao de produto: quem le da esquerda para a direita ve
           primeiro o que o programa ENTREGA, e o original entra como
           referencia depois - e nao o inverso, que faz a peca parecer um
           "antes" com um "depois" escondido no canto.
           A CONTA: o back buffer ja chega com DLSS 5 aplicado no quadro
           inteiro, e este bloco devolve o ORIGINAL por cima de uma metade.
           Para o original ficar a DIREITA, copia-se a metade [corte, m_w[ da
           textura guardada e cola-se na MESMA coordenada x = corte (recorte e
           destino andam juntos; colar em 0 espelharia a imagem de lado). */
        if (corte < m_w)
        {
            D3D11_BOX box{};
            box.left = corte; box.right = m_w;
            box.top = 0;  box.bottom = m_h;
            box.front = 0; box.back = 1;
            ctx->CopySubresourceRegion(bb, 0, corte, 0, 0, m_original[slot], 0, &box);
        }

        /* A LINHA DIVISORIA. Uma textura de 2 px de largura pela altura da
           tela, branca: sem ela, numa cena de cor parecida dos dois lados,
           nao da para ver onde uma metade acaba.

           A PRIMEIRA VERSAO COPIAVA PIXEL A PIXEL de uma textura 1x1 --
           2 x altura chamadas de CopySubresourceRegion por quadro, 2160 a
           1080p. Cada chamada custa perto de um microssegundo de CPU: a
           risca sozinha comia mais tempo do que as duas copias de quadro
           somadas. Agora e uma chamada. */
        if (m_marca && corte >= 1 && corte + 2 <= m_w)
            ctx->CopySubresourceRegion(bb, 0, corte, 0, 0, m_marca, 0, nullptr);

        /* AS MARCAS. Depois da risca de proposito: elas ficam POR CIMA de
           tudo, inclusive da metade copiada. */
        rotulos_garantir(dev, corte, log);
        rotulos_desenhar(ctx, bb, corte);

        ctx->Release();
        bb->Release(); dev->Release();

        if (!m_disse)
        {
            m_disse = true;
            char line[220];
            snprintf(line, sizeof(line),
                     "[DLSS 5 Bridge] modo comparativo ligado: esquerda %u px = imagem ORIGINAL, "
                     "direita %u px = com DLSS 5 (divisor %d%%, atraso compensado %d quadro(s)). "
                     "No quadro sintetizado do frame skipping os dois lados sao iguais - desligue "
                     "o skipping para gravar.",
                     corte, m_w - corte, m_div, m_atraso);
            if (log) log(line);
        }
    }

private:
    static bool abrir(void *rtvHandle, ID3D11Texture2D **tex, ID3D11Device **dev)
    {
        /* Em D3D, o handle de resource_view E o ponteiro da ID3D11View -- o
           mesmo atalho que a mistura_guiada.h usa e documenta. */
        ID3D11RenderTargetView *rtv = reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle);
        if (rtv == nullptr) return false;
        ID3D11Resource *res = nullptr;
        rtv->GetResource(&res);
        if (res == nullptr) return false;
        HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(tex));
        res->Release();
        if (FAILED(hr) || *tex == nullptr) return false;
        (*tex)->GetDevice(dev);
        if (*dev == nullptr) { (*tex)->Release(); *tex = nullptr; return false; }
        return true;
    }

    bool preparar(ID3D11Device *dev, const D3D11_TEXTURE2D_DESC &d, void (*log)(const char *))
    {
        if (m_original[0] && d.Width == m_w && d.Height == m_h && d.Format == m_fmt) return true;
        soltar();
        D3D11_TEXTURE2D_DESC t{};
        t.Width = d.Width; t.Height = d.Height;
        t.MipLevels = 1; t.ArraySize = 1;
        t.Format = d.Format;
        t.SampleDesc.Count = 1;
        t.Usage = D3D11_USAGE_DEFAULT;
        t.BindFlags = 0;            /* so copia: nao precisa de SRV nem de RTV */
        for (int i = 0; i < nTex(); i++)
        {
            if (FAILED(dev->CreateTexture2D(&t, nullptr, &m_original[i])) || m_original[i] == nullptr)
            {
                if (log) log("[DLSS 5 Bridge] modo comparativo: nao consegui criar a textura da copia");
                soltar();
                return false;
            }
        }
        /* A risca: 2 px de largura pela altura inteira, branca. Falhar aqui
           NAO desliga o modo -- sem ela a divisao continua valendo, so nao
           tem a linha. Em formato de 10 ou 16 bits nao ha conversao e a
           risca fica de fora, junto com as marcas. */
        dlss5_rotulos::ImagemBGRA risca;
        risca.w = 2; risca.h = d.Height;
        risca.px.assign((size_t)risca.w * risca.h * 4, 0xFF);
        m_marca = dlss5_rotulos::subir(dev, d.Format, risca);

        m_w = d.Width; m_h = d.Height; m_fmt = d.Format;
        m_rotVale = false;
        return true;
    }

    /* ---------------------------------------------------------------- */
    /* AS MARCAS "ORIGINAL" e "DLSS 5 FOR ALL"                           */
    /* ---------------------------------------------------------------- */

    /* Cria as duas texturas quando faltam, e refaz quando a tela muda de
       tamanho ou a linha muda de lugar (o tamanho das marcas depende do
       espaco de cada metade). Roda no maximo uma vez por mudanca -- nunca
       por quadro. */
    void rotulos_garantir(ID3D11Device *dev, UINT corte, void (*log)(const char *))
    {
        if (m_rotVale && corte == m_rotCorte) return;
        if (!m_rotulos) { rotulos_soltar(); m_rotVale = true; m_rotCorte = corte; return; }

        rotulos_soltar();
        m_rotVale = true;              /* tentamos uma vez; nao repetir por quadro */
        m_rotCorte = corte;

        if (!dlss5_rotulos::formato_serve(m_fmt, nullptr))
        {
            if (!m_rotDisse && log)
            {
                m_rotDisse = true;
                char l[200];
                snprintf(l, sizeof(l),
                         "[DLSS 5 Bridge] comparativo: back buffer no formato %d (HDR) - as marcas "
                         "ORIGINAL/DLSS 5 nao sao desenhadas neste formato.", (int)m_fmt);
                log(l);
            }
            return;
        }

        /* A ALTURA E CONFIGURAVEL ([comparar] rotulo_altura, 5 a 15% da altura
           da tela). Comecou fixa em 7% e o resultado ficou pequeno demais para
           video -- num quadro de 1080p o selo tinha 75 px, e num corte de
           YouTube a palavra sumia. O padrao passou a 9%. A margem e 1,5% da
           altura, igual dos dois lados. */
        UINT alturaPedida = (UINT)((double)m_h * (double)m_rotPct / 100.0 + 0.5);
        if (alturaPedida < 24) alturaPedida = 24;
        const UINT margem = (UINT)((double)m_h * 0.015 + 0.5);

        const UINT dispEsq = (corte > margem * 2) ? (corte - margem * 2) : 0;
        const UINT dispDir = (m_w - corte > margem * 2) ? (m_w - corte - margem * 2) : 0;
        if (dispEsq == 0 || dispDir == 0) return;

        dlss5_rotulos::ImagemBGRA a, b;
        bool usouReserva = false;
        for (int tentativa = 0; tentativa < 2; tentativa++)
        {
            wchar_t caminho[MAX_PATH];
            /* LADOS INVERTIDOS EM 14/09/2026 (ver o bloco da copia): agora a
               ESQUERDA e a imagem COM DLSS 5 e a DIREITA e a original. `a` e
               sempre o rotulo da esquerda e `b` o da direita, entao quem troca
               de lugar sao os arquivos carregados - e nao o desenho, que
               continua colando `a` na esquerda e `b` na direita. */
            _snwprintf(caminho, MAX_PATH, L"%smarca\\comparar-dlss5.png", m_dir);
            caminho[MAX_PATH - 1] = L'\0';
            if (!dlss5_rotulos::carregar_png(caminho, alturaPedida, a))
            { usouReserva = true; dlss5_rotulos::desenhar_texto(L"DLSS 5", alturaPedida, a); }

            _snwprintf(caminho, MAX_PATH, L"%smarca\\comparar-original.png", m_dir);
            caminho[MAX_PATH - 1] = L'\0';
            if (!dlss5_rotulos::carregar_png(caminho, alturaPedida, b))
            { usouReserva = true; dlss5_rotulos::desenhar_texto(L"ORIGINAL", alturaPedida, b); }

            if (!a.valida() || !b.valida()) return;

            /* As duas TEM de sair com a mesma altura -- e o que faz parecer um
               par e nao dois adesivos. Entao, se alguma nao couber na sua
               metade, as DUAS encolhem pelo mesmo fator e recarregamos. */
            double fator = 1.0;
            if (a.w > dispEsq) fator = (double)dispEsq / (double)a.w;
            if (b.w > dispDir) { const double f2 = (double)dispDir / (double)b.w; if (f2 < fator) fator = f2; }
            if (fator > 0.995 || tentativa == 1) break;

            UINT nova = (UINT)((double)alturaPedida * fator);
            if (nova < 16) nova = 16;
            if (nova >= alturaPedida) break;
            alturaPedida = nova;
        }

        m_rotEsq = dlss5_rotulos::subir(dev, m_fmt, a);
        m_rotDir = dlss5_rotulos::subir(dev, m_fmt, b);
        m_rotEsqW = a.w; m_rotEsqH = a.h;
        m_rotDirW = b.w; m_rotDirH = b.h;
        m_rotMargem = margem;

        /* A TECLA. Texto desenhado (nao ha arte para isso, e nem deveria haver:
           a pessoa pode remapear o atalho). Metade da altura das marcas - e
           informacao de servico, nao titulo; competir com as marcas tiraria o
           olho da imagem, que e o que se quer comparar. */
        if (m_atalho[0] != L'\0')
        {
            dlss5_rotulos::ImagemBGRA t;
            wchar_t frase[96];
            _snwprintf(frase, 96, L"%s liga/desliga", m_atalho);
            frase[95] = L'\0';
            UINT hTec = alturaPedida / 2; if (hTec < 14) hTec = 14;
            if (dlss5_rotulos::desenhar_texto(frase, hTec, t) && t.valida())
            {
                m_rotTec = dlss5_rotulos::subir(dev, m_fmt, t);
                m_rotTecW = t.w; m_rotTecH = t.h;
            }
        }

        if (!m_rotDisse && log)
        {
            m_rotDisse = true;
            char l[260];
            snprintf(l, sizeof(l),
                     "[DLSS 5 Bridge] comparativo: marcas %s, %ux%u e %ux%u px, margem %u px.",
                     usouReserva ? "de RESERVA (falta algum PNG em marca\\)" : "de marca\\*.png",
                     m_rotEsqW, m_rotEsqH, m_rotDirW, m_rotDirH, margem);
            log(l);
        }
    }

    void rotulos_desenhar(ID3D11DeviceContext *ctx, ID3D11Texture2D *bb, UINT corte)
    {
        if (!m_rotulos) return;
        const UINT y = m_rotMargem;
        if (m_rotEsq && m_rotEsqW + m_rotMargem <= corte && y + m_rotEsqH <= m_h)
            ctx->CopySubresourceRegion(bb, 0, m_rotMargem, y, 0, m_rotEsq, 0, nullptr);
        if (m_rotDir && corte + m_rotMargem + m_rotDirW <= m_w && y + m_rotDirH <= m_h)
            ctx->CopySubresourceRegion(bb, 0, corte + m_rotMargem, y, 0, m_rotDir, 0, nullptr);
        /* A TECLA, CENTRADA NO ALTO. Fica sobre a linha divisoria, que e o unico
           lugar que nao cobre nem uma metade nem a outra - e e para onde o olho
           vai quando se compara. Se nao couber, simplesmente nao desenha: melhor
           faltar a dica do que tapar a imagem que a pessoa quer avaliar. */
        if (m_rotTec && m_rotTecW > 0 && m_rotTecW + 2 * m_rotMargem <= m_w)
        {
            const UINT x = (m_w > m_rotTecW) ? (m_w - m_rotTecW) / 2 : 0;
            const UINT yt = y + (m_rotEsqH > m_rotDirH ? m_rotEsqH : m_rotDirH) + m_rotMargem;
            if (yt + m_rotTecH <= m_h)
                ctx->CopySubresourceRegion(bb, 0, x, yt, 0, m_rotTec, 0, nullptr);
        }
    }

    void rotulos_soltar()
    {
        if (m_rotEsq) { m_rotEsq->Release(); m_rotEsq = nullptr; }
        if (m_rotDir) { m_rotDir->Release(); m_rotDir = nullptr; }
        if (m_rotTec) { m_rotTec->Release(); m_rotTec = nullptr; }
        m_rotEsqW = m_rotEsqH = m_rotDirW = m_rotDirH = 0;
        m_rotTecW = m_rotTecH = 0;
    }

    void soltar()
    {
        for (int i = 0; i < kGuardados; i++)
            if (m_original[i]) { m_original[i]->Release(); m_original[i] = nullptr; }
        if (m_marca) { m_marca->Release(); m_marca = nullptr; }
        rotulos_soltar();
        m_rotVale = false;
        m_w = m_h = 0; m_guardados = 0; m_escreve = 0;
    }

    /* Teto de copias guardadas. O que existe de verdade e nTex() = atraso + 1:
       com o padrao medido (atraso 0) e UMA textura, como sempre foi. */
    enum { kGuardados = 3 };
    int nTex() const { return m_atraso + 1; }

    bool  m_quer = false;
    bool  m_rotulos = true;
    int   m_div = 50;
    int   m_rotPct = 9;        /* altura das marcas, em % da altura da tela */
    int   m_atraso = 0;        /* quantos quadros a esquerda recua (medido: 0) */
    int   m_guardados = 0;     /* quantas copias validas ja existem na roda */
    int   m_escreve = 0;       /* onde o ultimo begin escreveu */
    bool  m_disse = false;
    UINT  m_w = 0, m_h = 0;
    DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D *m_original[kGuardados] = { nullptr, nullptr, nullptr };
    ID3D11Texture2D *m_marca = nullptr;

    wchar_t m_dir[MAX_PATH] = { 0 };
    bool  m_rotVale = false;
    bool  m_rotDisse = false;
    UINT  m_rotCorte = 0;
    UINT  m_rotMargem = 0;
    UINT  m_rotEsqW = 0, m_rotEsqH = 0, m_rotDirW = 0, m_rotDirH = 0;
    ID3D11Texture2D *m_rotEsq = nullptr;
    ID3D11Texture2D *m_rotDir = nullptr;
    /* A tecla no cabecalho: texto vindo do app por [comparar] atalho. */
    wchar_t m_atalho[64] = { 0 };
    UINT  m_rotTecW = 0, m_rotTecH = 0;
    ID3D11Texture2D *m_rotTec = nullptr;
};

}   // namespace dlss5_comparar
