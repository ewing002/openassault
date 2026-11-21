#pragma once
#include "../core/fs.h"
#include <string>
#include <vector>
#include <cstdint>

namespace xa {

// NOSCAPE.3DW is a library of object templates (every vehicle, projectile, wreck ... lined up
// in the valley) and EXTRAS.3DW the pool of hostages/extras: neither is part of the visible landscape.
inline bool isTemplateSet(const std::string& set) {
    std::string u; for (char c : set) u += char(c >= 'a' && c <= 'z' ? c - 32 : c);
    return u == "NOSCAPE.3DW" || u == "EXTRAS.3DW";
}

// A placed object from SCAPES\WORLD.BIN (38-byte records grouped into *.3dw sets).
struct WorldObject {
    uint32_t objClass = 0;      // object class (matches PROTOTYP.DIR type column)
    uint32_t proto = 0;         // index into PROTOTYP
    int32_t radius = 0;
    int32_t pos[3] = {0, 0, 0}; // game units, z is up
    int16_t angle[5] = {0, 0, 0, 0, 0};  // last two: pitch, yaw (4096 = full turn)
    std::string set;            // which .3dw group it belongs to
};

class World {
public:
    bool load(const GameFS& fs, const std::string& levelDir);
    const std::vector<WorldObject>& objects() const { return objects_; }
    std::vector<std::string> sets;
    std::string error;
private:
    std::vector<WorldObject> objects_;
};

} // namespace xa
