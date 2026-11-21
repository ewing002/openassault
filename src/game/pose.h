#pragma once
#include "../assets/model.h"
#include "../assets/typ.h"
#include "../render/math.h"

namespace xa {

// How a polygon is blended (derived from the original draw type word).
enum class Blend : uint8_t { Opaque, ColorKey, Additive, Translucent, Shadow };
Blend polygonBlend(const Polygon& p);
inline bool isInvisible(const Polygon& p) {   // magenta collision/trigger surfaces
    return p.kind == Polygon::Flat && ((p.color >> 11) & 31) >= 30 && ((p.color >> 5) & 63) == 0 && (p.color & 31) >= 30;
}

struct PoseInput {
    float unitScale = 1.0f / 1000.0f;
    float ticks = 0;            // game ticks elapsed (drives ROTATE_PART)
    float rotorTicks = -1;      // rotor animation clock (<0: same as ticks)
    Mat4 billboardLocal;        // rotation that makes a part face the camera (in instance space)
    bool aim = false;           // turret aiming: "turm" parts turn by aimYaw, "rohr" parts pitch by aimPitch (radians)
    float aimYaw = 0, aimPitch = 0;
    const int16_t* vars = nullptr;   // object angle words (4096 = 360 degrees), indexed by VARIABLE_OFFSET / 2
    int nVars = 0;
};

// Orientation of a placed world object (angle[2..4] = alpha/X, beta/Y, gamma/Z; 4096 = 360 degrees).
Mat4 worldObjectRotation(const int16_t angle[5]);

// Computes one matrix per model part (part 0 = identity). Returns the number of parts written.
int computePose(const Model& m, const ObjectType* t, const PoseInput& in, Mat4* out, int maxParts);

} // namespace xa
