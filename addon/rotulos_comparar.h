/*
    rotulos_comparar.h -- as duas marcas do MODO COMPARATIVO: a arte
    "ORIGINAL" na metade da esquerda e a arte "DLSS 5 FOR ALL" na da direita.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).

    ------------------------------------------------------------------------
    POR QUE ISTO EXISTE SEPARADO
    ------------------------------------------------------------------------
    O comparar_lado_a_lado.h e so geometria: duas copias de GPU e uma risca.
    Ler PNG, redimensionar e converter formato e outro assunto, com outras
    dependencias (WIC, COM, GDI), e misturar as duas coisas num arquivo so
    faria o leitor achar que a copia do quadro depende do decodificador de
    imagem -- nao depende. Se este arquivo inteiro falhar, o comparativo
    continua funcionando sem as marcas.

    ------------------------------------------------------------------------
    O QUE ELE CARREGA, E DE ONDE
    ------------------------------------------------------------------------
        <pasta do exe>\marca\comparar-original.png
        <pasta do exe>\marca\comparar-dlss5.png

    Sao as artes do usuario, ja normalizadas (mesma altura, mesmo
    enquadramento). Faltando o arquivo, entra o LETREIRO DE RESERVA: um
    cartao escuro com moldura azul e a palavra escrita com GDI. E feio de
    proposito -- e para a pessoa ver que o PNG sumiu, nao para passar por
    arte.

    ------------------------------------------------------------------------
    POR QUE OPACO, E NAO COM TRANSPARENCIA
    ------------------------------------------------------------------------
    O desenho e um CopySubresourceRegion no back buffer: copia de bloco, sem
    escala e SEM MISTURA. Nao existe canal alfa valendo aqui -- o que for
    copiado substitui o pixel. Por isso as artes vao com o fundo escuro
    delas, que e como o usuario desenhou, e por isso a escala e feita na CPU
    (WIC), no tamanho exato, uma vez por resolucao: nao da para esticar na
    hora da copia.

    Desenhar com mistura de verdade exigiria um quad com pixel shader e
    salvar/restaurar o estado do D3D11 no meio do pipeline do ReShade. Nao
    vale o risco para dois selos num modo de gravacao de video.

    ------------------------------------------------------------------------
    FORMATOS
    ------------------------------------------------------------------------
    A copia so aceita formatos da mesma familia, entao a textura da marca
    nasce com o formato exato do back buffer. Convertemos BGRA de 8 bits
    para B8G8R8A8_* ou R8G8B8A8_*. Em HDR de 10 ou 16 bits nao ha conversao
    aqui: as marcas ficam de fora e o log diz por que, uma vez so.
*/
#pragma once

#include <d3d11.h>
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <vector>
#include <cstdint>
#include <cstdio>

namespace dlss5_rotulos
{

struct ImagemBGRA
{
    UINT w = 0, h = 0;
    std::vector<uint8_t> px;      /* B,G,R,A por pixel, linha continua */
    bool valida() const { return w > 0 && h > 0 && px.size() == (size_t)w * h * 4; }
};

/* ------------------------------------------------------------------ */
/* WIC sem depender da libwindowscodecs: os GUIDs vao a mao.          */
/* Com MinGW, referenciar CLSID_WICImagingFactory obriga a linkar uma */
/* biblioteca de import a mais; declarar os tres GUIDs aqui custa 12  */
/* linhas e deixa o build com -lole32 e nada alem disso.              */
/* ------------------------------------------------------------------ */
static const GUID kCLSID_WICImagingFactory1 =
    { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID kIID_IWICImagingFactory =
    { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID kWICPixelFormat32bppBGRA =
    { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };

/* Le o PNG e ja devolve na ALTURA pedida, largura proporcional. A escala e
   do WIC (Fant, bom em reducao) e acontece uma vez por resolucao de tela --
   nao por quadro. */
inline bool carregar_png(const wchar_t *caminho, UINT alturaAlvo, ImagemBGRA &out)
{
    if (caminho == nullptr || alturaAlvo < 8) return false;
    if (GetFileAttributesW(caminho) == INVALID_FILE_ATTRIBUTES) return false;

    /* A thread de render do jogo pode nao ter COM ligado; ligamos e
       desligamos aqui dentro. RPC_E_CHANGED_MODE quer dizer que ja havia um
       apartamento de outro tipo -- serve do mesmo jeito, so nao e nosso para
       desligar. */
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool nossoCo = SUCCEEDED(hrCo);

    bool ok = false;
    IWICImagingFactory *fab = nullptr;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *quadro = nullptr;
    IWICBitmapScaler *escala = nullptr;
    IWICFormatConverter *conv = nullptr;

    if (SUCCEEDED(CoCreateInstance(kCLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER,
                                   kIID_IWICImagingFactory, reinterpret_cast<void **>(&fab))) &&
        fab != nullptr &&
        SUCCEEDED(fab->CreateDecoderFromFilename(caminho, nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &quadro)))
    {
        UINT ow = 0, oh = 0;
        if (SUCCEEDED(quadro->GetSize(&ow, &oh)) && ow > 0 && oh > 0)
        {
            const UINT nh = alturaAlvo;
            UINT nw = (UINT)(((double)ow * (double)nh / (double)oh) + 0.5);
            if (nw < 1) nw = 1;

            IWICBitmapSource *fonte = quadro;
            if (SUCCEEDED(fab->CreateBitmapScaler(&escala)) &&
                SUCCEEDED(escala->Initialize(quadro, nw, nh, WICBitmapInterpolationModeFant)))
                fonte = escala;

            /* O PNG pode vir em 24bpp (o caso das artes atuais) ou 32bpp;
               o conversor normaliza para BGRA e resolve os dois. */
            if (SUCCEEDED(fab->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(fonte, kWICPixelFormat32bppBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom)))
            {
                out.w = nw; out.h = nh;
                out.px.assign((size_t)nw * nh * 4, 0);
                if (SUCCEEDED(conv->CopyPixels(nullptr, nw * 4, (UINT)out.px.size(), out.px.data())))
                    ok = true;
            }
        }
    }

    if (conv)   conv->Release();
    if (escala) escala->Release();
    if (quadro) quadro->Release();
    if (dec)    dec->Release();
    if (fab)    fab->Release();
    if (nossoCo) CoUninitialize();

    if (!ok) { out.w = out.h = 0; out.px.clear(); }
    return ok;
}

/* LETREIRO DE RESERVA. So aparece quando o PNG nao esta la. GDI numa DIB, sem
   tocar em nada de D3D: cartao escuro, moldura azul de 3 px e a palavra. */
inline bool desenhar_texto(const wchar_t *texto, UINT alturaAlvo, ImagemBGRA &out)
{
    if (texto == nullptr || alturaAlvo < 8) return false;

    const int h = (int)alturaAlvo;
    const int recuo = (h * 22) / 100;
    const int corpo = (h * 52) / 100;

    HDC tela = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(tela);
    if (tela) ReleaseDC(nullptr, tela);
    if (dc == nullptr) return false;

    HFONT fonte = CreateFontW(-corpo, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ velha = fonte ? SelectObject(dc, fonte) : nullptr;

    RECT med{ 0, 0, 0, 0 };
    DrawTextW(dc, texto, -1, &med, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    int w = (med.right - med.left) + recuo * 2;
    if (w < h) w = h;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          /* topo primeiro, como a ImagemBGRA */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib != nullptr && bits != nullptr)
    {
        HGDIOBJ velhoBmp = SelectObject(dc, dib);

        RECT tudo{ 0, 0, w, h };
        HBRUSH fundo = CreateSolidBrush(RGB(8, 12, 22));
        FillRect(dc, &tudo, fundo);
        DeleteObject(fundo);

        HBRUSH borda = CreateSolidBrush(RGB(60, 180, 255));
        FrameRect(dc, &tudo, borda);
        RECT b2{ 1, 1, w - 1, h - 1 };  FrameRect(dc, &b2, borda);
        RECT b3{ 2, 2, w - 2, h - 2 };  FrameRect(dc, &b3, borda);
        DeleteObject(borda);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 250, 255));
        DrawTextW(dc, texto, -1, &tudo, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();

        out.w = (UINT)w; out.h = (UINT)h;
        out.px.assign((size_t)w * h * 4, 0);
        memcpy(out.px.data(), bits, out.px.size());
        /* GDI escreve zero no canal alfa; a copia e opaca, mas um alfa zerado
           num back buffer que alguem leia depois viraria buraco. */
        for (size_t i = 3; i < out.px.size(); i += 4) out.px[i] = 0xFF;
        ok = true;

        SelectObject(dc, velhoBmp);
    }

    if (dib) DeleteObject(dib);
    if (velha) SelectObject(dc, velha);
    if (fonte) DeleteObject(fonte);
    DeleteDC(dc);
    return ok;
}

/* O CopySubresourceRegion exige formatos da mesma familia, entao a textura
   nasce com o formato do back buffer -- e so precisamos saber a ordem dos
   canais. Fora destes dois grupos (HDR de 10 ou 16 bits), nao ha conversao e
   as marcas nao sao desenhadas. */
inline bool formato_serve(DXGI_FORMAT f, bool *trocarRB)
{
    switch (f)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        if (trocarRB) *trocarRB = false;
        return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        if (trocarRB) *trocarRB = true;
        return true;
    default:
        return false;
    }
}

inline ID3D11Texture2D *subir(ID3D11Device *dev, DXGI_FORMAT fmt, const ImagemBGRA &img)
{
    bool trocarRB = false;
    if (dev == nullptr || !img.valida() || !formato_serve(fmt, &trocarRB)) return nullptr;

    std::vector<uint8_t> dados(img.px);
    if (trocarRB)
        for (size_t i = 0; i + 3 < dados.size(); i += 4)
        { const uint8_t t = dados[i]; dados[i] = dados[i + 2]; dados[i + 2] = t; }

    D3D11_TEXTURE2D_DESC t{};
    t.Width = img.w; t.Height = img.h;
    t.MipLevels = 1; t.ArraySize = 1;
    t.Format = fmt;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT;
    t.BindFlags = 0;                 /* so serve de fonte de copia */

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = dados.data();
    sd.SysMemPitch = img.w * 4;

    ID3D11Texture2D *tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&t, &sd, &tex))) return nullptr;
    return tex;
}

}   // namespace dlss5_rotulos
