#include "pose.h"
#include <algorithm>

namespace xa {

Blend polygonBlend(const Polygon& p) {
    if (p.kind == Polygon::Textured) {
        if (p.drawType == 28) return Blend::Additive;    // halos, flames, smoke, shields
        if (p.drawType == 32) return Blend::Shadow;      // blob shadows
        if (p.drawType & 4) return Blend::ColorKey;      // trees, fences (colour 0 = transparent)
        return Blend::Opaque;
    }
    if (p.drawType == 12) return Blend::Translucent;     // glass / energy surfaces
    return Blend::Opaque;
}

Mat4 worldObjectRotation(const int16_t angle[5]) {
    const float k = 6.2831853f / 4096.0f;
    // Placement angles as the original uses them: Rx(-x) * Ry(+y) * Rz(-z), the turn about the vertical axis
    // innermost. The z angle turns clockwise (L1 jail generators at 0 / 1014 / 2042 / 3064 face the jail); x,
    // y and the order were fitted to objects resting on the ground (L3 25/26, L5 11/11 only fit this way). The
    // briefing flights (HLM) use the same signs.
    return Mat4::rotX(-angle[2] * k) * Mat4::rotY(angle[3] * k) * Mat4::rotZ(-angle[4] * k);
}

static Mat4 angleMatrix(ObjectType::Method method, const float a[3]) {
    const float k = 6.2831853f / 4096.0f;
    if (method == ObjectType::RotateGaAlBe) return Mat4::rotY(a[1] * k) * Mat4::rotX(a[0] * k) * Mat4::rotZ(a[2] * k);
    return Mat4::rotZ(a[2] * k) * Mat4::rotX(a[0] * k) * Mat4::rotY(a[1] * k);
}

int computePose(const Model& m, const ObjectType* t, const PoseInput& in, Mat4* out, int maxParts) {
    out[0] = Mat4();
    const float rotorTicks = in.rotorTicks >= 0 ? in.rotorTicks : in.ticks;
    int n = int(std::min<size_t>(m.parts.size(), size_t(maxParts)));
    for (int i = 1; i < n; i++) {
        const ModelPart& p = m.parts[size_t(i)];
        float ang[3] = {0, 0, 0};
        if (in.vars && p.variableOffset > 0 && p.variableOffset / 2 + 2 < in.nVars)   // e.g. spider legs (0x7623c)
            for (int k = 0; k < 3; k++) ang[k] = float(in.vars[p.variableOffset / 2 + k]);
        ObjectType::Method method = ObjectType::TranslateRotate;
        if (t) {
            // Animation variables are shared by all parts with the same offset.
            for (const auto& s : t->spins)
                if (s.part >= 0 && size_t(s.part) < t->parts.size() && t->parts[size_t(s.part)].variableOffset == p.variableOffset)
                    for (int k = 0; k < 3; k++) ang[k] += float(s.speed[k]) * in.ticks;
            for (const auto& tp : t->parts)
                if (tp.variableOffset == p.variableOffset) {
                    method = tp.method;
                    if (in.aim && tp.name.size() >= 4) {
                        const float inv = 4096.f / 6.2831853f;
                        char c0 = char(tp.name[0] | 32), c1 = char(tp.name[1] | 32), c2 = char(tp.name[2] | 32), c3 = char(tp.name[3] | 32);
                        if (c0 == 't' && c1 == 'u' && c2 == 'r' && c3 == 'm') ang[2] += in.aimYaw * inv;          // turret
                        else if (c0 == 'r' && c1 == 'o' && c2 == 'h' && c3 == 'r') ang[0] += in.aimPitch * inv;   // barrel
                    }
                    // Helicopter rotors are driven by behaviour code (ADV_HELI etc.).
                    if (t->spins.empty() && (tp.name.rfind("rotor", 0) == 0 || tp.name.rfind("Rotor", 0) == 0))
                        ang[2] += (tp.name.back() == '2' ? -1.f : 1.f) * 380.f * rotorTicks;
                    break;
                }
        }
        const Mat4& parent = out[(p.parent >= 0 && p.parent < i) ? p.parent : 0];
        Mat4 local = Mat4::translate({p.pivot[0] * in.unitScale, p.pivot[1] * in.unitScale, p.pivot[2] * in.unitScale});
        if (p.billboard) {
            Vec3 o = (parent * local).transformPoint({0, 0, 0});
            out[i] = Mat4::translate(o) * in.billboardLocal;
            continue;
        }
        if (method != ObjectType::NoRotate) local = local * angleMatrix(method, ang);
        out[i] = parent * local;
    }
    for (int i = n; i < maxParts; i++) out[i] = Mat4();
    return n;
}

} // namespace xa
