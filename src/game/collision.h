#pragma once
#include "../assets/model.h"
#include "../assets/world.h"
#include "../assets/typ.h"
#include "../render/math.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace xa {

// Static triangle soup of the level (all placed objects at rest pose), bucketed
// in a 2D grid for fast queries. Includes the invisible collision surfaces.
class CollisionWorld {
public:
    struct Tri { Vec3 a, b, c, n; int owner; };   // owner = world object index
    void build(const World& world, const ModelLib& lib, const TypeDB& types, float unitScale, int skipWorldIndex);
    // Highest surface below 'from' (z), or -1e9 if none.
    float groundBelow(Vec3 from) const;
    // Pushes a sphere out of all geometry. Returns true if it touched anything.
    bool resolveSphere(Vec3& center, float radius, Vec3* velocity, Vec3* contactNormal = nullptr) const;
    // First hit along the segment a->b (ignores disabled owners). Returns false if nothing.
    bool raycast(Vec3 a, Vec3 b, Vec3& hit, int& owner, Vec3* normal = nullptr) const;
    // Triangles whose closest point lies within r of c (0xe108a / 0xe1948): the plane normal turned towards c,
    // the projection of c onto that plane and the distance to it.
    struct Touch { Vec3 n, onPlane; float dist; int owner; bool ground; };
    void touching(Vec3 c, float r, std::vector<Touch>& out) const;
    // object list of an owner (loader 0x60769)
    enum OwnerKind { KOther = 0, KGround = 1, KWater = 2, KTree = 3 };
    int ownerKind(int owner) const { return owner >= 0 && size_t(owner) < ownerKind_.size() ? ownerKind_[size_t(owner)] : KOther; }
    bool isGround(int owner) const { return ownerKind(owner) == KGround; }
    // highest surface of that kind (-1: any) below from, with its upward normal; -1e9 if none
    float surfaceBelow(Vec3 from, int kind, Vec3* normal) const;
    // Destroyed objects stop colliding.
    void setOwnerEnabled(int owner, bool enabled);
    bool ownerEnabled(int owner) const { return owner < 0 || size_t(owner) >= ownerDisabled_.size() || !(ownerDisabled_[size_t(owner)] & 1); }
    // Landscape sets outside the current area (SCAPESWITCH): not solid for the player, but the original's ground
    // and object lists keep them (its queries check no set), so enemy movement sees them (includeHiddenScape).
    void setScapeHidden(int owner, bool hidden) {
        if (owner >= 0 && size_t(owner) < ownerDisabled_.size()) ownerDisabled_[size_t(owner)] = uint8_t((ownerDisabled_[size_t(owner)] & 1) | (hidden ? 2 : 0));
    }
    void includeHiddenScape(bool on) const { includeHidden_ = on; }
    size_t triangleCount() const { return tris_.size(); }
    // Moving walls / blocks: shift all triangles of an owner (re-bucketed in the grid).
    void moveOwner(int owner, Vec3 delta);
    // Rigid motion: the owner's triangles become m * (their pose at the first call).
    void transformOwner(int owner, const Mat4& m);
    // owner of the last triangle resolveSphere pushed against (-1: none); reading clears it
    int takeContact() const { int o = lastContact_; lastContact_ = -1; return o; }
private:
    int64_t key(int cx, int cy) const { return (int64_t(cx) << 32) ^ uint32_t(cy); }
    int cell(float v) const { return int(std::floor(v / cellSize_)); }
    void gather(float x0, float y0, float x1, float y1, std::vector<uint32_t>& out) const;
    float cellSize_ = 40.f;
    std::vector<Tri> tris_;
    std::vector<Tri> waterOnly_;   // see-through WATER polygons: only for surfaceBelow(KWater)
    std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    std::vector<uint8_t> ownerDisabled_, ownerKind_;
    std::unordered_map<int, std::vector<uint32_t>> ownerTris_;
    std::unordered_map<int, std::vector<Tri>> ownerRest_;
    const std::vector<uint32_t>& trisOf(int owner);
    void bucket(uint32_t id, bool add);
    bool enabled(const Tri& t) const {
        if (t.owner < 0 || size_t(t.owner) >= ownerDisabled_.size()) return true;
        const uint8_t f = ownerDisabled_[size_t(t.owner)];
        return f == 0 || (f == 2 && includeHidden_);
    }
    mutable bool includeHidden_ = false;
    mutable std::vector<uint32_t> stamp_;
    mutable uint32_t frame_ = 0;
    mutable int lastContact_ = -1;
};

} // namespace xa
