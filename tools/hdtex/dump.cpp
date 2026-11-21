#include "assets/textures.h"
#include <cstdio>
#include <set>
using namespace xa;
uint64_t fnv(const std::vector<uint16_t>& d) { uint64_t h = 1469598103934665603ull; for (uint16_t v : d) { h ^= v & 0xff; h *= 1099511628211ull; h ^= v >> 8; h *= 1099511628211ull; } return h; }
int main(int argc, char** argv) {
    GameFS fs(argv[1]); std::set<uint64_t> seen; int total = 0;
    for (int l = 1; l <= 6; l++) {
        char dir[32]; std::snprintf(dir, sizeof dir, "DATA\\LEVEL%02d", l);
        TextureLib t; if (!t.load(fs, dir)) continue;
        for (auto& p : t.pages()) {
            if (p.width != 256 || p.height != 256) continue;
            total++;
            uint64_t h = fnv(p.rgb565);
            if (!seen.insert(h).second) continue;
            std::vector<uint8_t> rgba; rgb565ToRgba(p.rgb565, rgba, false);
            char out[256]; std::snprintf(out, sizeof out, "%s/%016llx.tga", argv[2], (unsigned long long)h);
            writeTga(out, 256, 256, rgba);
        }
    }
    std::printf("pages %d unique %zu\n", total, seen.size());
}
