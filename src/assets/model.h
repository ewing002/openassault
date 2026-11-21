#pragma once
#include "../core/fs.h"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace xa {

// One polygon of a prototype as stored in OBJECTS\PROTOTYP.BIN.
struct Polygon {
    enum Kind : uint8_t { Flat, Gouraud, Textured };
    Kind kind = Flat;
    uint16_t drawType = 0;          // original type word (0 flat, 4 gouraud, 8/16/... textured)
    uint16_t color = 0;             // RGB565 for flat polygons
    int16_t normal = 0;             // 1-based index into normal table, sign = facing
    int32_t planeDist = 0;
    uint32_t texture = 0;           // texture page index (textured)
    uint16_t gouraudRamp = 0;       // ramp index (gouraud)
    std::vector<uint16_t> idx;      // vertex indices
    std::vector<uint16_t> u, v;     // texture coordinates, 0..32767 == 0..1
    std::vector<uint16_t> shade;    // per-vertex shade (gouraud)
};

// Model parts form a hierarchy (tags 0x40 push / 0x44 pop). Child vertices are
// relative to the part's pivot, which is relative to the parent part.
struct ModelPart {
    int parent = -1;
    int32_t pivot[3] = {0, 0, 0};
    int variableOffset = 0;         // tag 0x48: byte offset of the part's 3 angles (alpha,beta,gamma)
    bool billboard = false;         // tag 0x9C: part always faces the camera (sprites, halos, flames)
};

struct Model {
    std::string name;
    int32_t radius = 0;
    int32_t bboxMin[3] = {0, 0, 0}, bboxMax[3] = {0, 0, 0};
    std::vector<int32_t> verts;     // x,y,z triples (game units)
    std::vector<ModelPart> parts;   // parts[0] is the main object
    std::vector<uint8_t> vertexPart;// part index for every vertex
    std::vector<int> lods;          // tag 0x50/0x54: LOD selector -> model indices (high..low detail)
    int aliasOf = -1;
    int objClass = -1;              // default object type (PROTOTYP.DIR), index into OBJECT.TYP               // tag 0x78: shares the geometry of another model
    std::vector<Polygon> polys;
};

// PROTOTYP.DIN (index) + PROTOTYP.BIN (data). Chunk offsets inside the BIN are
// self-relative and may point into neighbouring objects (LOD sharing).
class ModelLib {
public:
    bool load(const GameFS& fs, const std::string& levelDir);
    const std::vector<Model>& models() const { return models_; }
    int find(const std::string& name) const;
    std::string error;
    int failed = 0;
    std::vector<std::string> failedNames;
private:
    bool parseModel(const std::vector<uint8_t>& bin, size_t base, Model& m);
    bool parseTree(const std::vector<uint8_t>& bin, size_t& p, Model& m, int depth);
    bool parseLeaf(const std::vector<uint8_t>& bin, size_t& p, Model& m);
    std::vector<Model> models_;
    std::unordered_map<std::string, int> byName_;
};

} // namespace xa
