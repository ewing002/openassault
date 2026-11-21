#include "model.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace xa {
namespace {
struct Reader {
    const std::vector<uint8_t>& b;
    bool ok = true;
    bool in(size_t p, size_t n) { if (p + n > b.size()) ok = false; return ok; }
    uint16_t u16(size_t p) { return in(p, 2) ? uint16_t(b[p] | (b[p + 1] << 8)) : 0; }
    int16_t s16(size_t p) { return int16_t(u16(p)); }
    int32_t s32(size_t p) { return in(p, 4) ? int32_t(uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24)) : 0; }
};
std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper((unsigned char)c)); return s; }
}

int ModelLib::find(const std::string& name) const {
    auto it = byName_.find(upper(name));
    return it == byName_.end() ? -1 : it->second;
}

bool ModelLib::load(const GameFS& fs, const std::string& levelDir) {
    std::vector<uint8_t> din, bin;
    if (!fs.read(levelDir + "\\OBJECTS\\PROTOTYP.DIN", din) || !fs.read(levelDir + "\\OBJECTS\\PROTOTYP.BIN", bin)) {
        error = "missing PROTOTYP.DIN/BIN"; return false;
    }
    size_t n = din.size() / 18;
    models_.resize(n);
    for (size_t i = 0; i < n; i++) {
        const uint8_t* e = &din[i * 18];
        Model& m = models_[i];
        m.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 14));
        uint32_t off = uint32_t(e[14] | (e[15] << 8) | (e[16] << 16) | (uint32_t(e[17]) << 24));
        byName_[upper(m.name)] = int(i);
        Model tmp = m;
        if (off < bin.size() && parseModel(bin, off, tmp)) m = std::move(tmp);
        else { failed++; failedNames.push_back(m.name); }
    }
    // PROTOTYP.DIR: "name 0 index class radius"
    std::vector<uint8_t> dirRaw;
    if (fs.read(levelDir + "\\OBJECTS\\PROTOTYP.DIR", dirRaw)) {
        std::istringstream in(decodeText(dirRaw));
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string name; int zero, index, cls; long radius;
            if (ls >> name >> zero >> index >> cls >> radius && index >= 0 && size_t(index) < models_.size())
                models_[size_t(index)].objClass = cls;
        }
    }
    return true;
}

bool ModelLib::parseTree(const std::vector<uint8_t>& bin, size_t& p, Model& m, int) {
    // The tree is stored depth-first: a split node (-1) is followed by its front and
    // back subtrees, a leaf is a polygon count followed by the polygons. Walk it
    // iteratively (some level geometry has very deep, degenerate trees).
    Reader r{bin};
    for (int pending = 1; pending > 0; pending--) {
        int16_t k = r.s16(p);
        if (!r.ok) return false;
        if (k == -1) { p += 10; pending += 2; continue; }   // plane + front size
        if (!parseLeaf(bin, p, m)) return false;
    }
    return r.ok;
}

bool ModelLib::parseLeaf(const std::vector<uint8_t>& bin, size_t& p, Model& m) {
    Reader r{bin};
    int16_t k = r.s16(p);
    if (k < 0 || k > 4000) return false; // leaf: polygon count
    p += 2;
    const size_t nverts = m.verts.size() / 3;
    for (int i = 0; i < k; i++) {
        Polygon poly;
        poly.drawType = r.u16(p);
        poly.normal = r.s16(p + 2);
        poly.planeDist = r.s32(p + 4);
        uint16_t len = r.u16(p + 8);
        uint16_t col = r.u16(p + 10);
        uint16_t n = r.u16(p + 12);
        if (!r.ok || n < 3 || n > 64) return false;
        size_t q = p + 14;
        for (int j = 0; j < n; j++) { uint16_t ix = r.u16(q + 2 * j); if (ix >= nverts) return false; poly.idx.push_back(ix); }
        q += 2 * n;
        if (len == 14 + 6 * n + 4) {
            poly.kind = Polygon::Textured;
            for (int j = 0; j < n; j++) { poly.u.push_back(r.u16(q + 4 * j)); poly.v.push_back(r.u16(q + 4 * j + 2)); }
            poly.texture = uint32_t(r.s32(q + 4 * n));
        } else if (len == 14 + 4 * n + 2) {
            poly.kind = Polygon::Gouraud;
            for (int j = 0; j < n; j++) poly.shade.push_back(r.u16(q + 2 * j));
            poly.gouraudRamp = r.u16(q + 2 * n);
        } else if (len == 14 + 2 * n) {
            poly.kind = Polygon::Flat;
        } else return false;
        poly.color = col;
        if (!r.ok || len < 14) return false;
        m.polys.push_back(std::move(poly));
        p += len;
    }
    return r.ok;
}

bool ModelLib::parseModel(const std::vector<uint8_t>& bin, size_t base, Model& m) {
    Reader r{bin};
    m.radius = r.s32(base);
    for (int a = 0; a < 3; a++) { m.bboxMin[a] = r.s32(base + 4 + 8 * a); m.bboxMax[a] = r.s32(base + 8 + 8 * a); }
    // Chunk list: (u16 tag, s32 value) until tag 0xFFFF. Offsets are relative to the value field.
    size_t p = base + 0x32;
    std::vector<size_t> trees;
    m.parts.assign(1, ModelPart{});
    std::vector<int> stack{0};
    for (int guard = 0; guard < 8192; guard++) {
        uint16_t tag = r.u16(p);
        if (!r.ok) return false;
        if (tag == 0xFFFF) break;
        int32_t v = r.s32(p + 2);
        size_t target = size_t(int64_t(p + 2) + v);
        switch (tag) {
        case 0x00: {                       // vertex block: u16 count, count * int32[3]
            if (m.aliasOf >= 0) break;     // alias models only declare the count
            uint16_t nv = r.u16(target);
            if (!r.in(target + 2, size_t(nv) * 12)) return false;
            for (size_t i = 0; i < size_t(nv) * 3; i++) m.verts.push_back(r.s32(target + 2 + 4 * i));
            m.vertexPart.insert(m.vertexPart.end(), nv, uint8_t(stack.back()));
            break;
        }
        case 0x40: {                       // push part: pivot x,y,z (+ vertex end, id)
            if (m.parts.size() >= 255) return false;
            ModelPart part;
            part.parent = stack.back();
            for (int a = 0; a < 3; a++) part.pivot[a] = r.s32(target + 4 * a);
            m.parts.push_back(part);
            stack.push_back(int(m.parts.size()) - 1);
            break;
        }
        case 0x44: if (stack.size() > 1) stack.pop_back(); break;
        case 0x48: m.parts[size_t(stack.back())].variableOffset = v; break;
        case 0x9C: m.parts[size_t(stack.back())].variableOffset = v; m.parts[size_t(stack.back())].billboard = true; break;
        case 0x4C: case 0x50: case 0x54: case 0x58: case 0x5C: {  // LOD selector (tag = 0x4C + 4*DETAIL_METHODE): u16 count, u32 model[count]
            uint16_t n = r.u16(target);
            for (int i = 0; i < n && i < 8; i++) m.lods.push_back(r.s32(target + 2 + 4 * i));
            break;
        }
        case 0x78: m.aliasOf = v; break;   // geometry shared with model #v
        case 0x14: trees.push_back(target); break;      // polygon BSP tree
        default: break;                    // 0x04 normals, 0x18/0x24/0x28/0x70/0x74/0x80 flags
        }
        p += 6;
    }
    for (size_t t : trees) { size_t q = t; if (!parseTree(bin, q, m, 0)) return false; }
    return r.ok;
}

} // namespace xa
