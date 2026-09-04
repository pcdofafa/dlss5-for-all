// =====================================================================================
//  qr.h - gerador de QR Code para o DLSS5 For All
//
//  Implementacao propria, escrita a partir da especificacao ISO/IEC 18004 (QR Code).
//  Sem dependencia externa e sem codigo de terceiros: nao ha licenca de outro projeto
//  embutida aqui, o arquivo segue a mesma licenca do resto do app.
//
//  O que faz: modo byte (8 bits), niveis de correcao L/M/Q/H, versao 1..40 escolhida
//  automaticamente pelo tamanho do dado, mascara escolhida pelas 4 regras de penalidade
//  da norma. Sai uma matriz de modulos que da pra desenhar com GDI (1 = escuro).
//
//  Uso tipico:
//      qr::Code c;
//      if (qr::EncodeText(L"https://exemplo.com", qr::ECC_M, c)) { ... c.at(x,y) ... }
//
//  Header-only, C++17, sem nenhum #include de Windows.
// =====================================================================================
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

namespace qr {

enum Ecc { ECC_L = 0, ECC_M = 1, ECC_Q = 2, ECC_H = 3 };

// Matriz do simbolo, sem quiet zone (quem desenha e que reserva as 4 bordas claras).
struct Code {
    int version = 0;
    int mask = 0;                 // mascara escolhida (0..7)
    int size = 0;                 // 21 + 4*(version-1)
    std::vector<uint8_t> mod;     // size*size, 1 = modulo escuro

    bool at(int x, int y) const {
        return x >= 0 && y >= 0 && x < size && y < size && mod[(size_t)y * size + x] != 0;
    }
};

namespace detail {

// --------------------------------------------------------------- tabelas da norma
// Codewords de correcao por bloco e numero de blocos, por [nivel][versao].
// Indice 0 das versoes nao existe (fica -1 so pra alinhar a tabela com a versao).
static const int8_t kEccPerBlock[4][41] = {
    // 0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
    { -1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 }, // L
    { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 }, // M
    { -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 }, // Q
    { -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 }, // H
};
static const int8_t kNumBlocks[4][41] = {
    // 0  1  2  3  4  5  6  7  8  9 10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
    { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 }, // L
    { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 }, // M
    { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 }, // Q
    { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 }, // H
};
// Os 2 bits de nivel como aparecem na informacao de formato (nao e a ordem L,M,Q,H).
static const int kFormatEcc[4] = { 1, 0, 3, 2 };

// Total de modulos disponiveis pra dado+correcao numa versao (ja descontando
// padroes de posicao, alinhamento, temporizacao, formato e versao).
static int rawDataModules(int ver) {
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}
static int dataCodewords(int ver, int ecl) {
    return rawDataModules(ver) / 8 - kEccPerBlock[ecl][ver] * kNumBlocks[ecl][ver];
}

// --------------------------------------------------------------- GF(256), poly 0x11D
static uint8_t gfMul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}
// Polinomio gerador de Reed-Solomon de grau `deg`, coeficientes do maior pro menor
// expoente, sem o termo lider (que e sempre 1).
static std::vector<uint8_t> rsGenerator(int deg) {
    std::vector<uint8_t> g((size_t)deg, 0);
    g[(size_t)deg - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < deg; i++) {
        for (size_t j = 0; j < g.size(); j++) {
            g[j] = gfMul(g[j], root);
            if (j + 1 < g.size()) g[j] ^= g[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return g;
}
static std::vector<uint8_t> rsRemainder(const uint8_t* data, size_t n, const std::vector<uint8_t>& gen) {
    std::vector<uint8_t> res(gen.size(), 0);
    for (size_t i = 0; i < n; i++) {
        uint8_t factor = (uint8_t)(data[i] ^ res[0]);
        res.erase(res.begin());
        res.push_back(0);
        for (size_t j = 0; j < res.size(); j++) res[j] ^= gfMul(gen[j], factor);
    }
    return res;
}

// --------------------------------------------------------------- posicoes de alinhamento
static std::vector<int> alignPositions(int ver) {
    if (ver == 1) return {};
    int na = ver / 7 + 2;
    int step = (ver == 32) ? 26 : ((ver * 4 + na * 2 + 1) / (na * 2 - 2) * 2);
    std::vector<int> p;
    for (int pos = ver * 4 + 10; p.size() < (size_t)(na - 1); pos -= step) p.push_back(pos);
    p.push_back(6);
    std::reverse(p.begin(), p.end());
    return p;
}

// --------------------------------------------------------------- desenho
struct Canvas {
    int size = 0;
    std::vector<uint8_t> mod, fun;   // fun = 1 quando o modulo e padrao fixo (nao recebe mascara)

    void init(int ver) {
        size = ver * 4 + 17;
        mod.assign((size_t)size * size, 0);
        fun.assign((size_t)size * size, 0);
    }
    void setFun(int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        mod[(size_t)y * size + x] = dark ? 1 : 0;
        fun[(size_t)y * size + x] = 1;
    }
    bool get(int x, int y) const { return mod[(size_t)y * size + x] != 0; }
    bool isFun(int x, int y) const { return fun[(size_t)y * size + x] != 0; }
};

static void drawFinder(Canvas& c, int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int d = std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && y >= 0 && x < c.size && y < c.size) c.setFun(x, y, d != 2 && d != 4);
        }
}
static void drawAlign(Canvas& c, int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
            c.setFun(cx + dx, cy + dy, d != 1);
        }
}
static void drawFormat(Canvas& c, int ecl, int mask) {
    int data = (kFormatEcc[ecl] << 3) | mask;      // 5 bits
    int rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;      // 15 bits

    auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; i++) c.setFun(8, i, bit(i));
    c.setFun(8, 7, bit(6));
    c.setFun(8, 8, bit(7));
    c.setFun(7, 8, bit(8));
    for (int i = 9; i < 15; i++) c.setFun(14 - i, 8, bit(i));

    for (int i = 0; i < 8; i++) c.setFun(c.size - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; i++) c.setFun(8, c.size - 15 + i, bit(i));
    c.setFun(8, c.size - 8, true);                 // modulo escuro obrigatorio
}
static void drawVersion(Canvas& c, int ver) {
    if (ver < 7) return;
    int rem = ver;
    for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    long bits = ((long)ver << 12) | rem;           // 18 bits
    for (int i = 0; i < 18; i++) {
        bool b = ((bits >> i) & 1) != 0;
        int a = c.size - 11 + i % 3, bpos = i / 3;
        c.setFun(a, bpos, b);
        c.setFun(bpos, a, b);
    }
}
static void drawFunctionPatterns(Canvas& c, int ver, int ecl) {
    for (int i = 0; i < c.size; i++) {             // temporizacao
        c.setFun(6, i, i % 2 == 0);
        c.setFun(i, 6, i % 2 == 0);
    }
    drawFinder(c, 3, 3);
    drawFinder(c, c.size - 4, 3);
    drawFinder(c, 3, c.size - 4);

    std::vector<int> ap = alignPositions(ver);
    for (size_t i = 0; i < ap.size(); i++)
        for (size_t j = 0; j < ap.size(); j++) {
            bool corner = (i == 0 && j == 0) || (i == 0 && j == ap.size() - 1) || (i == ap.size() - 1 && j == 0);
            if (!corner) drawAlign(c, ap[j], ap[i]);
        }
    drawFormat(c, ecl, 0);                          // so pra reservar; e redesenhado no fim
    drawVersion(c, ver);
}

// Espalha os codewords no zigue-zague de duas colunas, de baixo pra cima / cima pra baixo.
static void drawCodewords(Canvas& c, const std::vector<uint8_t>& data) {
    size_t i = 0;
    for (int right = c.size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;                  // a coluna 6 e temporizacao
        for (int vert = 0; vert < c.size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? c.size - 1 - vert : vert;
                if (!c.isFun(x, y) && i < data.size() * 8) {
                    c.mod[(size_t)y * c.size + x] = (uint8_t)((data[i >> 3] >> (7 - (i & 7))) & 1);
                    i++;
                }
            }
        }
    }
}
static bool maskBit(int m, int x, int y) {
    switch (m) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (y / 2 + x / 3) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    default:return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
}
static void applyMask(Canvas& c, int m) {
    for (int y = 0; y < c.size; y++)
        for (int x = 0; x < c.size; x++)
            if (!c.isFun(x, y) && maskBit(m, x, y))
                c.mod[(size_t)y * c.size + x] ^= 1;
}

// As 4 regras de penalidade da norma. So decidem qual mascara fica - qualquer uma
// gera um simbolo valido, entao aqui um empate nao quebra leitura.
static long penalty(const Canvas& c) {
    const int N1 = 3, N2 = 3, N3 = 40, N4 = 10;
    long p = 0;
    int n = c.size;

    // regra 1: sequencias de 5+ modulos iguais, em linha e em coluna
    for (int y = 0; y < n; y++) {
        int run = 1;
        for (int x = 1; x < n; x++) {
            if (c.get(x, y) == c.get(x - 1, y)) { run++; if (run == 5) p += N1; else if (run > 5) p++; }
            else run = 1;
        }
    }
    for (int x = 0; x < n; x++) {
        int run = 1;
        for (int y = 1; y < n; y++) {
            if (c.get(x, y) == c.get(x, y - 1)) { run++; if (run == 5) p += N1; else if (run > 5) p++; }
            else run = 1;
        }
    }
    // regra 2: blocos 2x2 da mesma cor
    for (int y = 0; y + 1 < n; y++)
        for (int x = 0; x + 1 < n; x++) {
            bool v = c.get(x, y);
            if (v == c.get(x + 1, y) && v == c.get(x, y + 1) && v == c.get(x + 1, y + 1)) p += N2;
        }
    // regra 3: padrao 1:1:3:1:1 com 4 modulos claros de um dos lados, ou seja as
    // janelas de 11 modulos 10111010000 e 00001011101, dentro do simbolo.
    auto scan = [&](bool horiz) {
        for (int line = 0; line < n; line++) {
            int win = 0;
            for (int i = 0; i < n; i++) {
                win = ((win << 1) & 0x7FF) | ((horiz ? c.get(i, line) : c.get(line, i)) ? 1 : 0);
                if (i >= 10 && (win == 0x5D0 || win == 0x05D)) p += N3;
            }
        }
    };
    scan(true);
    scan(false);
    // regra 4: desvio da proporcao de modulos escuros em relacao a 50%
    long dark = 0;
    for (int i = 0; i < n * n; i++) dark += c.mod[(size_t)i];
    int total = n * n;
    int k = (int)((std::abs(dark * 20L - total * 10L) + total - 1) / total) - 1;   // passos de 5%
    if (k > 0) p += (long)k * N4;
    return p;
}

// --------------------------------------------------------------- bits e blocos
struct BitBuf {
    std::vector<uint8_t> bytes;
    size_t bits = 0;
    void put(uint32_t val, int len) {
        for (int i = len - 1; i >= 0; i--) {
            if ((bits & 7) == 0) bytes.push_back(0);
            if ((val >> i) & 1) bytes.back() |= (uint8_t)(1 << (7 - (bits & 7)));
            bits++;
        }
    }
};

// Divide em blocos, calcula a correcao de cada um e intercala na ordem da norma.
static std::vector<uint8_t> addEccAndInterleave(const std::vector<uint8_t>& data, int ver, int ecl) {
    int numBlocks = kNumBlocks[ecl][ver];
    int eccLen = kEccPerBlock[ecl][ver];
    int rawCw = rawDataModules(ver) / 8;
    int shortBlocks = numBlocks - rawCw % numBlocks;
    int shortLen = rawCw / numBlocks;

    std::vector<std::vector<uint8_t>> blocks;
    std::vector<uint8_t> gen = rsGenerator(eccLen);
    size_t k = 0;
    for (int i = 0; i < numBlocks; i++) {
        int dlen = shortLen - eccLen + (i < shortBlocks ? 0 : 1);
        std::vector<uint8_t> b(data.begin() + k, data.begin() + k + dlen);
        k += (size_t)dlen;
        std::vector<uint8_t> ecc = rsRemainder(b.data(), b.size(), gen);
        if (i < shortBlocks) b.push_back(0);              // furo pra alinhar a intercalacao
        b.insert(b.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(b));
    }
    std::vector<uint8_t> out;
    out.reserve((size_t)rawCw);
    for (size_t i = 0; i < blocks[0].size(); i++)
        for (size_t j = 0; j < blocks.size(); j++)
            if (i != (size_t)(shortLen - eccLen) || j >= (size_t)shortBlocks)
                out.push_back(blocks[j][i]);
    return out;
}

} // namespace detail

// -------------------------------------------------------------------------------------
//  Codifica `data` (bytes crus) em modo byte. Devolve false se nao couber nem na
//  versao 40 do nivel pedido. `forceMask` (0..7) so serve pra teste - em -1 a
//  mascara sai das regras de penalidade da norma.
// -------------------------------------------------------------------------------------
inline bool EncodeBytes(const std::string& data, Ecc ecl, Code& out, int forceMask = -1) {
    using namespace detail;

    int ver = 0, ccBits = 0;
    for (int v = 1; v <= 40; v++) {
        int cc = (v <= 9) ? 8 : 16;                        // bits do contador em modo byte
        long need = 4 + cc + 8L * (long)data.size();
        if (need <= (long)dataCodewords(v, (int)ecl) * 8) { ver = v; ccBits = cc; break; }
    }
    if (!ver) return false;

    int capBits = dataCodewords(ver, (int)ecl) * 8;
    BitBuf bb;
    bb.put(0x4, 4);                                        // modo byte
    bb.put((uint32_t)data.size(), ccBits);
    for (unsigned char ch : data) bb.put(ch, 8);
    bb.put(0, std::min<int>(4, capBits - (int)bb.bits));   // terminador
    bb.put(0, (int)((8 - bb.bits % 8) % 8));               // completa o byte
    for (uint8_t pad = 0xEC; (int)bb.bits < capBits; pad = (uint8_t)(pad ^ 0xEC ^ 0x11))
        bb.put(pad, 8);                                    // enchimento 11101100 / 00010001

    std::vector<uint8_t> codewords = addEccAndInterleave(bb.bytes, ver, (int)ecl);

    Canvas c;
    c.init(ver);
    drawFunctionPatterns(c, ver, (int)ecl);
    drawCodewords(c, codewords);

    // escolhe a mascara de menor penalidade
    int best = 0; long bestP = -1;
    Canvas keep = c;
    if (forceMask >= 0 && forceMask <= 7) {
        best = forceMask;
    } else {
        for (int m = 0; m < 8; m++) {
            c = keep;
            applyMask(c, m);
            drawFormat(c, (int)ecl, m);
            long p = penalty(c);
            if (bestP < 0 || p < bestP) { bestP = p; best = m; }
        }
    }
    c = keep;
    applyMask(c, best);
    drawFormat(c, (int)ecl, best);

    out.version = ver;
    out.mask = best;
    out.size = c.size;
    out.mod = c.mod;
    return true;
}

// UTF-8 na mao pra este header nao depender de Windows nem de locale.
inline std::string ToUtf8(const wchar_t* s) {
    std::string o;
    if (!s) return o;
    for (const wchar_t* p = s; *p; p++) {
        uint32_t cp = (uint32_t)*p;
        if (cp >= 0xD800 && cp <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {   // par substituto (wchar_t 16 bits)
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)p[1] - 0xDC00);
            p++;
        }
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    return o;
}

inline bool EncodeText(const wchar_t* text, Ecc ecl, Code& out) {
    return EncodeBytes(ToUtf8(text), ecl, out);
}

} // namespace qr
