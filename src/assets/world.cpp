#include "world.h"
#include <sstream>

namespace xa {

bool World::load(const GameFS& fs, const std::string& levelDir) {
    std::vector<uint8_t> dirRaw, bin;
    if (!fs.read(levelDir + "\\SCAPES\\WORLD.DIR", dirRaw) || !fs.read(levelDir + "\\SCAPES\\WORLD.BIN", bin)) {
        error = "missing SCAPES\\WORLD.DIR/BIN"; return false;
    }
    struct Ent { std::string name; size_t off; };
    std::vector<Ent> ents;
    std::istringstream in(decodeText(dirRaw));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        Ent e; int kind; long off;
        if (ls >> e.name >> kind >> off && off >= 0) { e.off = size_t(off); ents.push_back(e); }
    }
    auto rd32 = [&](size_t p) { return uint32_t(bin[p] | (bin[p + 1] << 8) | (bin[p + 2] << 16) | (uint32_t(bin[p + 3]) << 24)); };
    for (size_t i = 0; i < ents.size(); i++) {
        size_t end = i + 1 < ents.size() ? ents[i + 1].off : bin.size();
        sets.push_back(ents[i].name);
        for (size_t p = ents[i].off; p + 38 <= end && p + 38 <= bin.size(); p += 38) {
            WorldObject o;
            o.objClass = rd32(p + 4);
            o.proto = rd32(p + 8);
            o.radius = int32_t(rd32(p + 12));
            for (int a = 0; a < 3; a++) o.pos[a] = int32_t(rd32(p + 16 + 4 * a));
            for (int a = 0; a < 5; a++) o.angle[a] = int16_t(bin[p + 28 + 2 * a] | (bin[p + 29 + 2 * a] << 8));
            o.set = ents[i].name;
            objects_.push_back(o);
        }
    }
    return true;
}

} // namespace xa
