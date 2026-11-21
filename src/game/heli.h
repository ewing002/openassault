#pragma once
#include "../render/math.h"
#include "collision.h"

namespace xa {

struct HeliInput {
    float forward = 0, strafe = 0, lift = 0;   // -1..1
    float yawRate = 0;                         // radians per second (positive = turn left); keys: the original filtered rotation
    float mouseYaw = 0;                        // radians this update, applied directly (mouse look; not in the original)
    bool boost = false;
};

// The player's vehicle. Movement follows the original per-tick code (heli 0x333e9, tank 0x33b05):
// the four input axes (-12..12; keys give +-12) drive filtered velocities in game units per tick.
// update() must be called with dt = 1/60 s (one original tick).
class Heli {
public:
    Vec3 pos, vel;
    float yaw = 0;          // radians, 0 = nose towards +Y
    float tiltPitch = 0, tiltRoll = 0;
    float rotorTicks = 0;   // rotor animation clock
    float rotorSpeed = 0;   // 0..1
    bool landed = true;
    float maxSpeed = 70.f, boostSpeed = 130.f, climbSpeed = 35.f, accel = 2.2f;
    float radius = 3.5f;    // collision sphere
    float clearance = 2.6f; // centre height above skids
    bool tank = false;      // ADV_PANZA: ground vehicle (W/S drive, A/D or mouse turn, follows the terrain)
    float tankSpeed = 32.f, tankBoost = 45.f;

    // original state (game units / tick, angle units of 65536 per turn)
    float slideV = 0, slideSm = 0, fwdV = 0, fwdSm = 0, rotV = 0, rotSm = 0, liftV = 0;
    // cockpit camera lead (0x62f98): turn and height input swing the cockpit camera ahead of the helicopter;
    // the cockpit model keeps the helicopter's angles, so it slides across the view while turning
    float camTurnC = 0, camTurnD = 0, camLiftA = 0, camLiftB = 0;
    float cockpitYaw() const { return std::trunc(camTurnD / 512.f) * 6.2831853f / 4096.f; }     // radians, + = left
    float cockpitPitch() const { return std::trunc(camLiftB / 512.f) * 6.2831853f / 4096.f; }   // radians, + = up
    float unitScale = 0.001f;
    float scale[4] = {1, 1, 1, 1};   // [SCALESPEED] forward, rotate, slide, height (applied to the smoothed rates)
    void update(const HeliInput& in, float dt, const CollisionWorld* world);
    void updateTank(const HeliInput& in, float dt, const CollisionWorld* world);
    Mat4 rotation() const { return Mat4::rotZ(yaw) * Mat4::rotX(tiltPitch) * Mat4::rotY(tiltRoll); }
    Vec3 forwardDir() const { return {-std::sin(yaw), std::cos(yaw), 0}; }
    Vec3 rightDir() const { return {std::cos(yaw), std::sin(yaw), 0}; }
};

} // namespace xa
