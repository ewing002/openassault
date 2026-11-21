#include "font.h"
#include "../core/lang.h"
#include <algorithm>
#include <sstream>

namespace xa {

bool Font::load(const GameFS& fs, const std::string& path) {
    std::vector<uint8_t> f;
    if (!fs.read(path, f) || f.size() < 18 || f[2] != 2) return false;
    texW = f[12] | (f[13] << 8); texH = f[14] | (f[15] << 8);
    int bpp = f[16] / 8;
    size_t off = 18 + f[0];
    if ((bpp != 2 && bpp != 3) || f.size() < off + size_t(texW) * texH * bpp) return false;
    bool topDown = (f[17] & 0x20) != 0;
    rgba.assign(size_t(texW) * texH * 4, 0);
    for (int y = 0; y < texH; y++) {
        int sy = topDown ? y : texH - 1 - y;
        for (int xx = 0; xx < texW; xx++) {
            const uint8_t* s = &f[off + (size_t(sy) * texW + xx) * bpp];
            int r, g, b;
            if (bpp == 3) { b = s[0]; g = s[1]; r = s[2]; }
            else { int v = s[0] | (s[1] << 8); r = ((v >> 10) & 31) * 255 / 31; g = ((v >> 5) & 31) * 255 / 31; b = (v & 31) * 255 / 31; }
            uint8_t* d = &rgba[(size_t(y) * texW + xx) * 4];
            bool key = r > 240 && g < 16 && b > 240;
            d[0] = uint8_t(r); d[1] = uint8_t(g); d[2] = uint8_t(b); d[3] = key ? 0 : 255;
        }
    }
    cellW = texW / 256;
    // unused character cells are filled with yellow; clear them so that linear filtering at the
    // edge of a neighbouring glyph (e.g. 'a' next to the yellow '`' cell) does not show a line
    for (int c = 0; c < 256 && cellW > 0; c++) {
        int yellow = 0, opaque = 0;
        for (int y = 0; y < texH; y++)
            for (int xx = 0; xx < cellW; xx++) {
                const uint8_t* d = &rgba[(size_t(y) * texW + size_t(c * cellW + xx)) * 4];
                if (!d[3]) continue;
                opaque++;
                if (d[0] > 150 && d[1] > 150 && d[2] < 100) yellow++;
            }
        if (opaque > 0 && yellow * 2 > opaque)
            for (int y = 0; y < texH; y++)
                for (int xx = 0; xx < cellW; xx++) rgba[(size_t(y) * texW + size_t(c * cellW + xx)) * 4 + 3] = 0;
    }
    {
        std::string up = path;
        for (auto& ch : up) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (cellW > 0 && (up.find("FONT") != std::string::npos || up.find("FNT") != std::string::npos)) addHungarian();
    }
    for (int c = 0; c < 256; c++) {
        x[c] = c * cellW;
        int last = -1;
        for (int xx = 0; xx < cellW; xx++)
            for (int y = 0; y < texH; y++)
                if (rgba[(size_t(y) * texW + x[c] + xx) * 4 + 3]) { last = xx; break; }
        w[c] = last + 1;
    }
    w[' '] = std::max(2, cellW / 2);
    return true;
}

// The fonts follow code page 850 and hold every Hungarian letter except É Á Ó Ő Ű ő ű (0x90 is È, 0xb5 Ä/Å,
// 0xe0 a second ó). These are built into free cells (see lang.h): the plain letter (E A O U o u) with an acute
// above it, doubled for Ő Ű ő ű. The acute is the part of ó above the top row of a plain o (there it is clean;
// in the capitals the accents touch the letter and its shadow). Fonts without accents (PILOTFNT) get the plain
// letter.
void Font::addHungarian() {
    const int h = texH;
    auto px = [&](int c, int xx, int y) -> uint8_t* { return &rgba[(size_t(y) * texW + size_t(c * cellW + xx)) * 4]; };
    auto rowEmpty = [&](int c, int y) { for (int xx = 0; xx < cellW; xx++) if (px(c, xx, y)[3]) return false; return true; };
    auto firstRow = [&](int c) { int y = 0; while (y < h && rowEmpty(c, y)) y++; return y; };
    auto cellEmpty = [&](int c) { return firstRow(c) >= h; };
    struct Mark { std::vector<uint8_t> pix; int x0 = 0, w = 0, hh = 0; };
    Mark acute;
    {
        const int donor = 0xa2, base = 0x6f;            // ó over o
        const int top = firstRow(base);
        int x0 = cellW, x1 = -1, y0 = h, y1 = -1;
        for (int y = 0; y < top && y < h; y++)
            for (int xx = 0; xx < cellW; xx++)
                if (px(donor, xx, y)[3]) { x0 = std::min(x0, xx); x1 = std::max(x1, xx); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        if (x1 >= 0) {
            acute.x0 = x0; acute.w = x1 - x0 + 1; acute.hh = y1 - y0 + 1;
            acute.pix.assign(size_t(acute.w) * acute.hh * 4, 0);
            for (int y = 0; y < acute.hh; y++)
                for (int xx = 0; xx < acute.w; xx++) std::copy_n(px(donor, x0 + xx, y0 + y), 4, &acute.pix[(size_t(y) * acute.w + xx) * 4]);
        }
    }
    auto compose = [&](int dst, int base, bool twice) {
        if (!cellEmpty(dst) || cellEmpty(base)) return;
        int bx0 = cellW, bx1 = -1;
        for (int y = 0; y < h; y++)
            for (int xx = 0; xx < cellW; xx++) {
                if (!px(base, xx, y)[3]) continue;
                std::copy_n(px(base, xx, y), 4, px(dst, xx, y));
                bx0 = std::min(bx0, xx); bx1 = std::max(bx1, xx);
            }
        if (acute.w == 0) return;
        const int step = twice ? std::max(2, (acute.w + 1) / 2 + 1) : 0;   // second stroke beside the first
        const int total = acute.w + step;
        const int top = std::max(0, firstRow(base) - acute.hh);           // right above the letter
        int left = std::clamp((bx0 + bx1 + 1 - total) / 2, 0, std::max(0, cellW - total));
        for (int k = 0; k < (twice ? 2 : 1); k++)
            for (int y = 0; y < acute.hh; y++)
                for (int xx = 0; xx < acute.w; xx++) {
                    const uint8_t* s = &acute.pix[(size_t(y) * acute.w + xx) * 4];
                    int tx = left + k * step + xx, ty = top + y;
                    if (!s[3] || tx < 0 || tx >= cellW || ty >= h) continue;
                    uint8_t* d = px(dst, tx, ty);
                    if (d[3] && s[0] + s[1] + s[2] < d[0] + d[1] + d[2]) continue;   // a shadow pixel never covers a stroke
                    std::copy_n(s, 4, d);
                }
    };
    compose(0x91, 'E', false);    // É
    compose(0x92, 'A', false);    // Á
    compose(0x98, 'O', false);    // Ó
    compose(0x9b, 'O', true);     // Ő
    compose(0x9c, 'U', true);     // Ű
    compose(0x9d, 'o', true);     // ő
    compose(0x9e, 'u', true);     // ű
}

int Font::textWidth(const std::string& s, int spacing) const {
    int t = 0;
    for (unsigned char c : s) t += w[c] + spacing;
    return t;
}

bool TextDB::load(const GameFS& fs, const std::string& path) {
    entries_.clear();
    std::vector<uint8_t> raw;
    if (fs.read(path, raw)) lang::parseTable(decodeText(raw), entries_);
    lang::overlay(path, entries_);   // the chosen language replaces entries (lang/<code>/<path>)
    return !entries_.empty();
}

const std::string& TextDB::get(int id) const {
    static const std::string empty;
    for (const auto& e : entries_) if (e.first == id) return e.second;
    return empty;
}

} // namespace xa
