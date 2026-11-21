// VISN texture decoder. Ported from the original hand-written x86 routine
// (XA_3DFX.EXE, object 3, 0xE6D38..0xE8350). Bit-exact with the original.
#include "visn.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace xa {
namespace {

// ---- constant tables taken from the executable's data segment ----
const uint8_t kZigzag[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
const uint8_t kNaturalToZz[64] = {0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63};
const uint8_t kQuantY[64] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
const uint8_t kQuantC[64] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
const int32_t kAan[64] = {16384,22725,21407,19266,16384,12873,8867,4520,22725,31521,29692,26722,22725,17855,12299,6270,21407,29692,27969,25172,21407,16819,11585,5906,19266,26722,25172,22654,19266,15137,10426,5315,16384,22725,21407,19266,16384,12873,8867,4520,12873,17855,16819,15137,12873,10114,6967,3552,8867,12299,11585,10426,8867,6967,4799,2446,4520,6270,5906,5315,4520,3552,2446,124};
const uint8_t kMetaLens[17] = {2,2,3,4,8,8,9,9,8,8,8,8,8,5,4,3,4};

// ---- bit reader: 16-bit little-endian words, consumed MSB first ----
struct Bits {
    const uint8_t* p; const uint8_t* end;
    uint32_t buf = 0; int cnt = 0;
    uint16_t word() {
        if (p + 2 <= end) { uint16_t w = uint16_t(p[0] | (p[1] << 8)); p += 2; return w; }
        p += 2; return 0;
    }
    void need(int n) { if (cnt < n) { buf |= (uint32_t(word()) << 16) >> cnt; cnt += 16; } }
    uint32_t take(int n) { uint32_t v = buf >> (32 - n); buf <<= n; cnt -= n; return v; }
    int32_t takeSigned(int n) { int32_t v = int32_t(buf) >> (32 - n); buf <<= n; cnt -= n; return v; }
    void skip(int n) { if (n) { buf <<= n; cnt -= n; } }
    bool bit() { bool b = (buf & 0x80000000u) != 0; buf <<= 1; cnt--; return b; }
};

// ---- canonical Huffman with 8-bit fast table and binary tree for long codes ----
struct Huff {
    int n = 0;                 // number of symbols; tree nodes are numbered >= n
    uint8_t fast[256];
    uint8_t lens[512];         // code lengths (index >= n reads 0, like the original's padding)
    uint8_t tree0[512], tree1[512];
    bool build(const uint8_t* l, int count) {
        n = count; std::memset(fast, 0, sizeof fast); std::memset(lens, 0, sizeof lens);
        std::memset(tree0, 0, sizeof tree0); std::memset(tree1, 0, sizeof tree1);
        std::memcpy(lens, l, size_t(count));
        uint32_t counts[16] = {0}, next[17] = {0};
        for (int i = 0; i < count; i++) { if (l[i] > 15) return false; if (l[i]) counts[l[i]]++; }
        uint32_t c = 0;
        for (int i = 1; i <= 15; i++) { c = (c + counts[i - 1]) * 2; next[i] = c; }
        int nextNode = count;
        for (int sym = 0; sym < count; sym++) {
            int len = l[sym]; if (!len) continue;
            uint32_t code = next[len]++;
            if (len <= 8) {
                uint32_t start = code << (8 - len), num = 1u << (8 - len);
                for (uint32_t k = 0; k < num && start + k < 256; k++) fast[start + k] = uint8_t(sym);
            } else {
                int extra = len - 8;
                uint8_t* slot = &fast[(code >> extra) & 0xff];
                for (int b = extra - 1; b >= 0; b--) {
                    if (*slot == 0) { tree0[nextNode & 511] = 0; tree1[nextNode & 511] = 0; *slot = uint8_t(nextNode); nextNode++; }
                    int node = *slot;
                    slot = ((code >> b) & 1) ? &tree1[node] : &tree0[node];
                }
                *slot = uint8_t(sym);
            }
        }
        return true;
    }
    // Generic decode (fast path + tree walk). 'consumeLen' mirrors the original:
    // fast hits consume lens[sym] bits; tree walks consume 8 + walk bits.
    int decode(Bits& b) const {
        b.need(8);
        int s = fast[b.buf >> 24];
        if (s < n) { b.skip(lens[s]); return s; }
        b.skip(8); b.need(8);
        while (s >= n) s = b.bit() ? tree1[s] : tree0[s];
        return s;
    }
};

// meta decoder for code lengths (original quirk: on a tree hit it first skips lens[node]=0 bits)
bool readLengths(Bits& b, const Huff& meta, uint8_t* out, int& count) {
    b.need(12); count = int(b.take(12));
    uint8_t dl = uint8_t(count), dh = 0; int pos = 0;
    while (true) {
        int s = meta.decode(b);
        if (s < 16) {
            dh = uint8_t((dh + s) & 0xf);
            if (pos < 512) out[pos] = dh;
            pos++;
            if (--dl == 0) break;
        } else {
            b.need(5); uint8_t run = uint8_t(b.take(5));
            dl = uint8_t(dl - run);
            do { if (pos < 512) out[pos] = 0; pos++; } while (--run != 0);
            if (dl == 0) break;
        }
        if (pos > 600) return false;
    }
    return count <= 256;
}

struct Coef { int32_t q; int pos; };

void buildQuant(Coef* t, const uint8_t* base, int quality) {
    int scale = (quality > 50) ? (100 - quality) * 2 : 5000 / std::max(quality, 1);
    for (int i = 0; i < 64; i++) {
        int v = (base[i] * scale + 50) / 100;
        v = std::clamp(v, 1, 255);
        t[kNaturalToZz[i]].q = v * kAan[i];
    }
    for (int k = 0; k < 64; k++) t[k].pos = kZigzag[k];
}

void readBlock(Bits& b, const Huff& h, const Coef* q, int32_t* blk) {
    std::memset(blk, 0, 64 * sizeof(int32_t));
    b.need(6); int ncoef = int(b.take(6));
    b.need(12); blk[0] = int32_t(b.take(12)) - 0x800;
    for (int k = 1; k <= ncoef; k++) {
        int s = h.decode(b);
        int32_t v;
        if (s <= 0x74) {
            v = s - 0x3a;
            if (v) v = (v * q[k].q) >> 14;
        } else {
            int nb = s - 0x74;
            b.need(nb);
            v = (b.takeSigned(nb) * q[k].q) >> 14;
        }
        if (k < 64) blk[q[k].pos] = v;
    }
}

inline int32_t m(int32_t a, int32_t c) { return (a * c) >> 8; }

void idct(const int32_t* in, int32_t* out) {
    int32_t t[64];
    for (int c = 0; c < 8; c++) {
        const int32_t* s = in + c;
        int32_t a0 = s[0], a2 = s[16], a4 = s[32], a6 = s[48];
        int32_t e0 = a0 + a4, e1 = a0 - a4, e3 = a2 + a6;
        int32_t e2 = m(a2 - a6, 0x16a) - e3;
        int32_t t0 = e0 + e3, t3 = e0 - e3, t1 = e1 + e2, t2 = e1 - e2;
        int32_t b1 = s[8], b3 = s[24], b5 = s[40], b7 = s[56];
        int32_t z13 = b5 + b3, z10 = b5 - b3, z11 = b1 + b7, z12 = b1 - b7;
        int32_t o7 = z11 + z13;
        int32_t o11 = m(z11 - z13, 0x16a);
        int32_t z5 = m(z10 + z12, 0x1d9);
        int32_t o10 = m(z12, 0x115) - z5;
        int32_t o12 = m(z10, -669) + z5;
        int32_t o6 = o12 - o7, o5 = o11 - o6, o4 = o10 + o5;
        int32_t* d = t + c;
        d[0] = t0 + o7; d[56] = t0 - o7; d[8] = t1 + o6; d[48] = t1 - o6;
        d[16] = t2 + o5; d[40] = t2 - o5; d[32] = t3 + o4; d[24] = t3 - o4;
    }
    for (int r = 0; r < 8; r++) {
        const int32_t* s = t + r * 8; int32_t* d = out + r * 8;
        int32_t e0 = s[0] + s[4], e1 = s[0] - s[4], e3 = s[2] + s[6];
        int32_t e2 = m(s[2] - s[6], 0x16a) - e3;
        int32_t t0 = e0 + e3, t3 = e0 - e3, t1 = e1 + e2, t2 = e1 - e2;
        int32_t z13 = s[5] + s[3], z10 = s[5] - s[3], z11 = s[1] + s[7], z12 = s[1] - s[7];
        int32_t o7 = z11 + z13;
        int32_t o11 = m(z11 - z13, 0x16a);
        int32_t z5 = m(z10 + z12, 0x1d9);
        int32_t o10 = m(z12, 0x115) - z5;
        int32_t o12 = m(z10, -669) + z5;
        int32_t o6 = o12 - o7, o5 = o11 - o6, o4 = o10 + o5;
        d[0] = (t0 + o7) >> 3; d[7] = (t0 - o7) >> 3; d[1] = (t1 + o6) >> 3; d[6] = (t1 - o6) >> 3;
        d[2] = (t2 + o5) >> 3; d[5] = (t2 - o5) >> 3; d[4] = (t3 + o4) >> 3; d[3] = (t3 - o4) >> 3;
    }
}

// ---- YCbCr -> RGB565 lookup tables (same construction as the original) ----
struct ColorTables {
    int32_t comp[3][512];       // R,G,B component bits for index -128..383
    int32_t y[512], crR[512], cbG[512], crG[512], cbB[512];
    static int x87round(double v) { return int(std::nearbyint(v)); }
    static void fill(int32_t* t, double k, int start) {
        // t[0..127] = clamp low, t[128..383] values, t[384..511] clamp high
        double v = k * start;
        for (int i = 0; i < 128; i++) t[i] = x87round(v);
        for (int i = 0; i < 256; i++) { t[128 + i] = x87round(v); v += k; }
        for (int i = 0; i < 128; i++) t[384 + i] = x87round(v);
    }
    ColorTables() {
        const int bits[3] = {5, 6, 5}, shift[3] = {11, 5, 0};
        for (int c = 0; c < 3; c++) {
            int maxv = (1 << bits[c]) - 1;
            for (int i = 0; i < 128; i++) comp[c][i] = 0;
            int last = 0;
            for (int i = 0; i < 256; i++) { last = (i * maxv / 255) << shift[c]; comp[c][128 + i] = last; }
            for (int i = 0; i < 128; i++) comp[c][384 + i] = last;
        }
        fill(y, 1.0, 0);
        fill(crR, 1.402, -128);
        fill(cbG, -0.34414, -128);
        fill(crG, -0.71414, -128);
        fill(cbB, 1.772, -128);
    }
    static int idx(int v) { return std::clamp(v, -128, 383) + 128; }
    uint16_t px(int cb, int cr, int yy) const {
        int Y = y[idx(yy)];
        int r = comp[0][std::clamp(Y + crR[idx(cr)], -128, 383) + 128];
        int g = comp[1][std::clamp(Y + cbG[idx(cb)] + crG[idx(cr)], -128, 383) + 128];
        int b = comp[2][std::clamp(Y + cbB[idx(cb)], -128, 383) + 128];
        return uint16_t(r | g | b);
    }
};

const ColorTables& colors() { static ColorTables t; return t; }

} // namespace

bool visnDecode(const uint8_t* data, size_t size, std::vector<uint16_t>& out, int& width, int& height) {
    if (!isVisn(data, size)) return false;
    width = (data[4] | (data[5] << 8));
    height = (data[6] | (data[7] << 8));
    int quality = data[8];
    int format = data[9];
    if (format != 2 || width <= 0 || height <= 0 || (width & 7) || (height & 7) || width > 4096 || height > 4096)
        return false;

    Coef qY[64], qC[64];
    buildQuant(qY, kQuantY, quality);
    buildQuant(qC, kQuantC, quality);

    Huff meta; meta.build(kMetaLens, 17);
    Bits b{data + 10, data + size};
    b.buf = (uint32_t(b.word()) << 16); b.buf |= b.word(); b.cnt = 32;

    uint8_t lens[512]; int count;
    Huff hC, hY;
    if (!readLengths(b, meta, lens, count) || !hC.build(lens, count)) return false;
    if (!readLengths(b, meta, lens, count) || !hY.build(lens, count)) return false;

    out.assign(size_t(width) * height, 0);
    const ColorTables& ct = colors();
    int32_t blk[64], pl[3][64];
    for (int by = 0; by < height / 8; by++)
        for (int bx = 0; bx < width / 8; bx++) {
            readBlock(b, hC, qC, blk); idct(blk, pl[0]);   // Cb
            readBlock(b, hC, qC, blk); idct(blk, pl[1]);   // Cr
            readBlock(b, hY, qY, blk); idct(blk, pl[2]);   // Y
            if (b.p > data + size + 8) return false;
            for (int r = 0; r < 8; r++) {
                uint16_t* d = &out[size_t(by * 8 + r) * width + bx * 8];
                for (int c = 0; c < 8; c++) d[c] = ct.px(pl[0][r * 8 + c], pl[1][r * 8 + c], pl[2][r * 8 + c]);
            }
        }
    return true;
}

} // namespace xa
