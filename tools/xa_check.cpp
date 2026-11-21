// Loads every level's assets and reports parse statistics (no graphics needed).
#include "core/fs.h"
#include "assets/textures.h"
#include "assets/model.h"
#include "assets/world.h"
#include <cstdio>
#include <algorithm>
using namespace xa;
int main(int argc, char** argv) {
    GameFS fs(argc > 1 ? argv[1] : ".");
    int bad = 0;
    for (int l = 1; l <= 6; l++) {
        char lv[32]; std::snprintf(lv, sizeof lv, "DATA\\LEVEL%02d", l);
        TextureLib t; ModelLib m; World w;
        bool ok = t.load(fs, lv) && m.load(fs, lv) && w.load(fs, lv);
        size_t polys = 0, maxParts = 0; for (auto& x : m.models()) { polys += x.polys.size(); maxParts = std::max(maxParts, x.parts.size()); }
        size_t badRef = 0; for (auto& o : w.objects()) if (o.proto >= m.models().size()) badRef++;
        std::printf("LEVEL%02d %s textures=%zu (%s) models=%zu failed=%d polys=%zu objects=%zu badref=%zu maxparts=%zu\n", l, ok ? "ok" : "FAIL",
            t.pages().size(), t.error.empty() ? "all decoded" : t.error.c_str(), m.models().size(), m.failed, polys, w.objects().size(), badRef, maxParts);
        for (auto& n : m.failedNames) std::printf("   failed model: %s\n", n.c_str());
        bad += !ok || m.failed || badRef || !t.error.empty();
    }
    return bad ? 1 : 0;
}
