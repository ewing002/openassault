#include "sprites.h"
#include <cstdio>
#include <algorithm>

namespace xa {

static bool decodeTga(const std::vector<uint8_t>& f, int& w, int& h, std::vector<uint8_t>& rgba) {
    if (f.size() < 18 || f[2] != 2 || (f[16] != 24 && f[16] != 32)) return false;
    w = f[12] | (f[13] << 8); h = f[14] | (f[15] << 8);
    int bpp = f[16] / 8;
    size_t off = 18 + f[0];
    if (f.size() < off + size_t(w) * h * bpp) return false;
    bool topDown = (f[17] & 0x20) != 0;
    rgba.assign(size_t(w) * h * 4, 0);
    for (int y = 0; y < h; y++) {
        int sy = topDown ? y : h - 1 - y;
        for (int x = 0; x < w; x++) {
            const uint8_t* s = &f[off + (size_t(sy) * w + x) * bpp];
            uint8_t* d = &rgba[(size_t(y) * w + x) * 4];
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
    }
    return true;
}

bool SpriteLib::load(const GameFS& fs) {
    struct Src { const char* dir; int first; };
    const Src srcs[Count] = {{"DATA\\GLOBAL\\EXPLAIR\\SMALL", 1}, {"DATA\\GLOBAL\\EXPLGRND\\SMALL", 1},
                             {"DATA\\GLOBAL\\EXPSMOKE", 0}, {"DATA\\GLOBAL\\EXPLSML", 0}};
    std::vector<uint8_t> file, img;
    for (int k = 0; k < Count; k++) {
        anims_[k].firstLayer = layers_;
        for (int i = srcs[k].first; i < srcs[k].first + 64; i++) {
            char name[96]; std::snprintf(name, sizeof name, "%s\\EXPLO.%03d", srcs[k].dir, i);
            int w, h;
            if (!fs.read(name, file) || !decodeTga(file, w, h, img)) break;
            // nearest-neighbour scale into a 64x64 layer
            size_t base = rgba_.size();
            rgba_.resize(base + kSize * kSize * 4);
            for (int y = 0; y < kSize; y++)
                for (int x = 0; x < kSize; x++) {
                    const uint8_t* s = &img[(size_t(y * h / kSize) * w + size_t(x * w / kSize)) * 4];
                    std::copy(s, s + 4, &rgba_[base + (size_t(y) * kSize + x) * 4]);
                }
            layers_++;
            anims_[k].frames++;
        }
    }
    return layers_ > 0;
}

} // namespace xa
