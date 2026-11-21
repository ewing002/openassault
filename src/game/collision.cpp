#include "collision.h"
#include "pose.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace xa {

void CollisionWorld::build(const World& world, const ModelLib& lib, const TypeDB& types, float unitScale, int skipWorldIndex) {
    tris_.clear(); grid_.clear(); waterOnly_.clear();
    const auto& models = lib.models();
    const auto& objs = world.objects();
    Mat4 parts[64];
    for (size_t oi = 0; oi < objs.size(); oi++) {
        const WorldObject& o = objs[oi];
        if (int(oi) == skipWorldIndex || o.proto >= models.size()) continue;
        if (o.set.rfind("sky", 0) == 0) continue;
        if (isTemplateSet(o.set)) continue;
        int mi = int(o.proto);
        if (!models[size_t(mi)].lods.empty()) mi = models[size_t(mi)].lods[0];
        if (mi < 0 || size_t(mi) >= models.size()) continue;
        if (models[size_t(mi)].aliasOf >= 0) mi = models[size_t(mi)].aliasOf;
        if (mi < 0 || size_t(mi) >= models.size()) continue;
        const Model& m = models[size_t(mi)];
        const ObjectType* t = types.get(o.objClass);
        if (t && !t->parts.empty() && t->parts[0].method == ObjectType::RotateLikeCamera) continue;  // sprites
        PoseInput pin; pin.unitScale = unitScale;
        computePose(m, t, pin, parts, 64);
        Mat4 xf = Mat4::translate({o.pos[0] * unitScale, o.pos[1] * unitScale, o.pos[2] * unitScale}) * worldObjectRotation(o.angle);
        const bool water = t && t->behavior == "WATER";
        for (const Polygon& p : m.polys) {
            Blend b = polygonBlend(p);
            const bool solid = b == Blend::Opaque || b == Blend::ColorKey;
            if (!solid && !water) continue;   // see-through water is not solid, but vehicles notice it
            auto P = [&](size_t k) {
                size_t vi = p.idx[k];
                const int32_t* v = &m.verts[vi * 3];
                int part = vi < m.vertexPart.size() ? std::min<int>(m.vertexPart[vi], 63) : 0;
                if (part < int(m.parts.size()) && m.parts[size_t(part)].billboard) part = -1;
                Vec3 lp{float(v[0]) * unitScale, float(v[1]) * unitScale, float(v[2]) * unitScale};
                return part < 0 ? Vec3{1e9f, 1e9f, 1e9f} : (xf * parts[part]).transformPoint(lp);
            };
            for (size_t k = 1; k + 1 < p.idx.size(); k++) {
                Tri tr{P(0), P(k), P(k + 1), {}, int(oi)};
                if (tr.a.x > 1e8f || tr.b.x > 1e8f || tr.c.x > 1e8f) continue;
                Vec3 n = cross(tr.b - tr.a, tr.c - tr.a);
                float len = std::sqrt(dot(n, n));
                if (len < 1e-6f) continue;
                tr.n = n * (1.f / len);
                if (!solid) { waterOnly_.push_back(tr); continue; }
                uint32_t id = uint32_t(tris_.size());
                tris_.push_back(tr);
                float x0 = std::min({tr.a.x, tr.b.x, tr.c.x}), x1 = std::max({tr.a.x, tr.b.x, tr.c.x});
                float y0 = std::min({tr.a.y, tr.b.y, tr.c.y}), y1 = std::max({tr.a.y, tr.b.y, tr.c.y});
                for (int cx = cell(x0); cx <= cell(x1); cx++)
                    for (int cy = cell(y0); cy <= cell(y1); cy++) grid_[key(cx, cy)].push_back(id);
            }
        }
    }
    stamp_.assign(tris_.size(), 0);
    ownerDisabled_.assign(objs.size(), 0);
    // object lists of the loader (0x60769): GROUND / GROUND_LAVA (0x21c568, what spiders and vehicles stand
    // on), WATER (0x21c56c), TREE
    ownerKind_.assign(objs.size(), KOther);
    for (size_t oi = 0; oi < objs.size(); oi++) {
        const ObjectType* t = types.get(objs[oi].objClass);
        if (!t) continue;
        if (t->behavior == "GROUND" || t->behavior == "GROUND_LAVA") ownerKind_[oi] = KGround;
        else if (t->behavior == "WATER") ownerKind_[oi] = KWater;
        else if (t->behavior == "TREE") ownerKind_[oi] = KTree;
    }
    if (std::getenv("XA_DEBUG")) {
        size_t n[4] = {0, 0, 0, 0}, o[4] = {0, 0, 0, 0};
        for (const Tri& t : tris_) n[ownerKind(t.owner)]++;
        for (uint8_t k : ownerKind_) o[k]++;
        std::printf("collision: tris other %zu ground %zu water %zu (+%zu see-through) tree %zu; objects water %zu tree %zu\n", n[0], n[1], n[2], waterOnly_.size(), n[3], o[2], o[3]);
    }
}

void CollisionWorld::bucket(uint32_t id, bool add) {
    const Tri& tr = tris_[id];
    float x0 = std::min({tr.a.x, tr.b.x, tr.c.x}), x1 = std::max({tr.a.x, tr.b.x, tr.c.x});
    float y0 = std::min({tr.a.y, tr.b.y, tr.c.y}), y1 = std::max({tr.a.y, tr.b.y, tr.c.y});
    for (int cx = cell(x0); cx <= cell(x1); cx++)
        for (int cy = cell(y0); cy <= cell(y1); cy++) {
            auto& v = grid_[key(cx, cy)];
            if (add) v.push_back(id);
            else { auto it = std::find(v.begin(), v.end(), id); if (it != v.end()) { *it = v.back(); v.pop_back(); } }
        }
}

const std::vector<uint32_t>& CollisionWorld::trisOf(int owner) {
    auto it = ownerTris_.find(owner);
    if (it == ownerTris_.end()) {
        std::vector<uint32_t> ids;
        for (uint32_t i = 0; i < tris_.size(); i++) if (tris_[i].owner == owner) ids.push_back(i);
        it = ownerTris_.emplace(owner, std::move(ids)).first;
    }
    return it->second;
}

void CollisionWorld::transformOwner(int owner, const Mat4& m) {
    const auto& ids = trisOf(owner);
    auto rit = ownerRest_.find(owner);
    if (rit == ownerRest_.end()) {
        std::vector<Tri> rest; rest.reserve(ids.size());
        for (uint32_t id : ids) rest.push_back(tris_[id]);
        rit = ownerRest_.emplace(owner, std::move(rest)).first;
    }
    for (size_t k = 0; k < ids.size(); k++) {
        uint32_t id = ids[k];
        const Tri& r = rit->second[k];
        bucket(id, false);
        Tri& t = tris_[id];
        t.a = m.transformPoint(r.a); t.b = m.transformPoint(r.b); t.c = m.transformPoint(r.c);
        Vec3 n = cross(t.b - t.a, t.c - t.a); float len = std::sqrt(dot(n, n));
        if (len > 1e-6f) t.n = n * (1.f / len);
        bucket(id, true);
    }
}

void CollisionWorld::moveOwner(int owner, Vec3 delta) {
    for (uint32_t id : trisOf(owner)) {
        bucket(id, false);
        Tri& t = tris_[id]; t.a = t.a + delta; t.b = t.b + delta; t.c = t.c + delta;
        bucket(id, true);
    }
}

void CollisionWorld::setOwnerEnabled(int owner, bool en) {
    if (owner >= 0 && size_t(owner) < ownerDisabled_.size()) ownerDisabled_[size_t(owner)] = uint8_t((ownerDisabled_[size_t(owner)] & 2) | (en ? 0 : 1));
}

void CollisionWorld::gather(float x0, float y0, float x1, float y1, std::vector<uint32_t>& out) const {
    out.clear();
    if (++frame_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0); frame_ = 1; }
    for (int cx = cell(x0); cx <= cell(x1); cx++)
        for (int cy = cell(y0); cy <= cell(y1); cy++) {
            auto it = grid_.find(key(cx, cy));
            if (it == grid_.end()) continue;
            for (uint32_t id : it->second) if (stamp_[id] != frame_) { stamp_[id] = frame_; out.push_back(id); }
        }
}

float CollisionWorld::groundBelow(Vec3 from) const { return surfaceBelow(from, -1, nullptr); }

float CollisionWorld::surfaceBelow(Vec3 from, int kind, Vec3* normal) const {
    static thread_local std::vector<uint32_t> ids;
    gather(from.x, from.y, from.x, from.y, ids);
    float best = -1e9f;
    for (uint32_t id : ids) {
        const Tri& t = tris_[id];
        if (std::fabs(t.n.z) < 0.05f || !enabled(t)) continue;
        if (kind >= 0 && ownerKind(t.owner) != kind) continue;
        // barycentric in XY
        float d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
        if (std::fabs(d) < 1e-9f) continue;
        float l1 = ((t.b.y - t.c.y) * (from.x - t.c.x) + (t.c.x - t.b.x) * (from.y - t.c.y)) / d;
        float l2 = ((t.c.y - t.a.y) * (from.x - t.c.x) + (t.a.x - t.c.x) * (from.y - t.c.y)) / d;
        float l3 = 1 - l1 - l2;
        if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) continue;
        float z = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
        if (z <= from.z + 0.01f && z > best) { best = z; if (normal) *normal = t.n.z < 0 ? t.n * -1.f : t.n; }
    }
    if (kind == KWater)
        for (const Tri& t : waterOnly_) {
            if (from.x < std::min({t.a.x, t.b.x, t.c.x}) || from.x > std::max({t.a.x, t.b.x, t.c.x}) ||
                from.y < std::min({t.a.y, t.b.y, t.c.y}) || from.y > std::max({t.a.y, t.b.y, t.c.y}) || std::fabs(t.n.z) < 0.05f) continue;
            float d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
            if (std::fabs(d) < 1e-9f) continue;
            float l1 = ((t.b.y - t.c.y) * (from.x - t.c.x) + (t.c.x - t.b.x) * (from.y - t.c.y)) / d;
            float l2 = ((t.c.y - t.a.y) * (from.x - t.c.x) + (t.a.x - t.c.x) * (from.y - t.c.y)) / d;
            float l3 = 1 - l1 - l2;
            if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) continue;
            float z = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
            if (z <= from.z + 0.01f && z > best) { best = z; if (normal) *normal = t.n.z < 0 ? t.n * -1.f : t.n; }
        }
    return best;
}

static Vec3 closestOnTriangle(Vec3 p, const Vec3& a, const Vec3& b, const Vec3& c) {
    // Ericson, Real-Time Collision Detection 5.1.5
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    Vec3 bp = p - b; float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c; float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float den = 1.f / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

void CollisionWorld::touching(Vec3 c, float r, std::vector<Touch>& out) const {
    static thread_local std::vector<uint32_t> ids;
    out.clear();
    gather(c.x - r, c.y - r, c.x + r, c.y + r, ids);
    for (uint32_t id : ids) {
        const Tri& t = tris_[id];
        if (!enabled(t)) continue;
        Vec3 q = closestOnTriangle(c, t.a, t.b, t.c);
        Vec3 d = c - q;
        if (dot(d, d) >= r * r) continue;
        Vec3 n = normalize(t.n);
        float h = dot(c - t.a, n);
        if (h < 0) { n = n * -1.f; h = -h; }
        out.push_back({n, c - n * h, h, t.owner, isGround(t.owner)});
    }
}

bool CollisionWorld::resolveSphere(Vec3& center, float radius, Vec3* velocity, Vec3* contactNormal) const {
    static thread_local std::vector<uint32_t> ids;
    bool hit = false;
    for (int iter = 0; iter < 4; iter++) {
        gather(center.x - radius, center.y - radius, center.x + radius, center.y + radius, ids);
        bool moved = false;
        for (uint32_t id : ids) {
            const Tri& t = tris_[id];
            if (!enabled(t)) continue;
            Vec3 q = closestOnTriangle(center, t.a, t.b, t.c);
            Vec3 d = center - q;
            float dist2 = dot(d, d);
            if (dist2 >= radius * radius) continue;
            float dist = std::sqrt(dist2);
            Vec3 n = dist > 1e-5f ? d * (1.f / dist) : t.n;
            center = center + n * (radius - dist + 1e-3f);
            if (velocity) { float vn = dot(*velocity, n); if (vn < 0) *velocity = *velocity - n * vn; }
            if (contactNormal) *contactNormal = n;
            lastContact_ = t.owner;
            hit = moved = true;
        }
        if (!moved) break;
    }
    return hit;
}

} // namespace xa

namespace xa {
bool CollisionWorld::raycast(Vec3 a, Vec3 b, Vec3& hit, int& owner, Vec3* normal) const {
    static thread_local std::vector<uint32_t> ids;
    gather(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y), ids);
    Vec3 d = b - a;
    float best = 2.f;
    for (uint32_t id : ids) {
        const Tri& t = tris_[id];
        if (!enabled(t)) continue;
        // Moller-Trumbore, two-sided
        Vec3 e1 = t.b - t.a, e2 = t.c - t.a, p = cross(d, e2);
        float det = dot(e1, p);
        if (std::fabs(det) < 1e-9f) continue;
        float inv = 1.f / det;
        Vec3 s = a - t.a;
        float u = dot(s, p) * inv; if (u < 0 || u > 1) continue;
        Vec3 q = cross(s, e1);
        float v = dot(d, q) * inv; if (v < 0 || u + v > 1) continue;
        float tt = dot(e2, q) * inv;
        if (tt >= 0 && tt <= 1 && tt < best) { best = tt; owner = t.owner; if (normal) *normal = t.n; }
    }
    if (best > 1.f) return false;
    hit = a + d * best;
    return true;
}
}
