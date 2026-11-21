#include "heli.h"
#include <algorithm>
#include <cmath>

namespace xa {

namespace {
constexpr float kAngle = 6.2831853f / 65536.f;          // original angle unit -> radians
inline float approach(float cur, float want, float step) {
    if (cur < want) return std::min(cur + step, want);
    return std::max(cur - step, want);
}
inline float toAxis(float v) { return std::clamp(v * 12.f, -12.f, 12.f); }
}

void Heli::updateTank(const HeliInput& in, float dt, const CollisionWorld* world) {
    rotorSpeed = 1; landed = true;
    const float S = toAxis(in.strafe), F = toAxis(in.forward);
    const float R = std::clamp(in.yawRate / (12.f * 140.f / 8.f * kAngle * 60.f) * 12.f, -12.f, 12.f);
    // 0x33b05: slide (target 18/step, 1/16 filter, smoothed 6/tick; released: 1/8 decay, 8/tick)
    if (S != 0) { slideV += (S * 18.f - slideV) / 16.f; slideSm = approach(slideSm, slideV, 6.f); }
    else { slideV -= slideV / 8.f; if (std::fabs(slideV) < 8.f) slideV = 0; slideSm = approach(slideSm, slideV, 8.f); }
    // forward 52/step, backward 37/step (0x33cad); smoothed 8/tick in the pressed direction, 20 otherwise
    fwdV += ((F > 0 ? F * 52.f : F * 37.f) - fwdV) / 16.f;
    if (F == 0 && std::fabs(fwdV) < 16.f) fwdV = 0;
    bool same = F != 0 && !((F > 0 && fwdSm < 0) || (F < 0 && fwdSm > 0));
    fwdSm = approach(fwdSm, fwdV, same ? 8.f : 20.f);
    // rotation 140/step (1/32 filter, 35/tick); released: 1/16 decay, 70/tick
    if (R != 0) { rotV += (R * 140.f - rotV) / 32.f; rotSm = approach(rotSm, rotV, 35.f); }
    else { rotV -= rotV / 16.f; rotSm = approach(rotSm, rotV, 70.f); if (std::fabs(rotV) < 32.f) rotV = 0; }
    yaw += rotSm * scale[1] / 8.f * kAngle + in.mouseYaw;
    Vec3 move = (forwardDir() * (fwdSm * scale[0]) + rightDir() * (slideSm * scale[2])) * unitScale;
    vel.x = move.x / dt; vel.y = move.y / dt;
    vel.z -= 40.f * dt;
    pos = pos + Vec3{move.x, move.y, vel.z * dt};
    if (!world) return;
    Vec3 body = pos + Vec3{0, 0, 0.5f};
    world->resolveSphere(body, radius, &vel, nullptr);
    pos = body - Vec3{0, 0, 0.5f};
    float g = world->groundBelow(pos + Vec3{0, 0, 3});
    if (g > -1e8f && pos.z - g <= clearance + 0.3f) {
        pos.z = g + clearance; if (vel.z < 0) vel.z = 0;
        // align with the slope (sample in front/behind and left/right)
        Vec3 f = forwardDir() * 3.f, rt = rightDir() * 2.f;
        float gf = world->groundBelow(pos + f + Vec3{0, 0, 3}), gb = world->groundBelow(pos - f + Vec3{0, 0, 3});
        float gr = world->groundBelow(pos + rt + Vec3{0, 0, 3}), gl = world->groundBelow(pos - rt + Vec3{0, 0, 3});
        float wantPitch = (gf > -1e8f && gb > -1e8f) ? std::atan2(gf - gb, 6.f) : 0.f;
        float wantRoll = (gr > -1e8f && gl > -1e8f) ? -std::atan2(gr - gl, 4.f) : 0.f;
        tiltPitch += (wantPitch - tiltPitch) * std::min(1.f, dt * 8.f);
        tiltRoll += (wantRoll - tiltRoll) * std::min(1.f, dt * 8.f);
    }
}

void Heli::update(const HeliInput& in, float dt, const CollisionWorld* world) {
    if (tank) { updateTank(in, dt, world); return; }
    // Rotor spins up before the helicopter can lift off (visual).
    float wantRotor = (landed && in.lift <= 0 && std::fabs(in.forward) + std::fabs(in.strafe) < 0.01f) ? 0.35f : 1.0f;
    rotorSpeed += (wantRotor - rotorSpeed) * std::min(1.f, dt * 0.8f);
    rotorTicks += dt * 25.f * rotorSpeed * 1.6f;

    const float S = toAxis(in.strafe), F = toAxis(in.forward), H = toAxis(in.lift);
    // full key rotation: 12 * 160 / 8 angle units per tick
    // mouse look goes through the same turn input as the stick (in.yawRate, rad/s, limited to the stick's full range),
    // so it accelerates, stops and tops out like the controller
    const float turnRate = std::clamp(in.yawRate + in.mouseYaw / dt, -1.6f, 1.6f);
    const float R = std::clamp(turnRate / (12.f * 160.f / 8.f * kAngle * 60.f) * 12.f, -12.f, 12.f);
    // slide (0x3344f): target 50/step with a 1/16 filter, smoothed 25/tick; released: 1/16 decay, 30/tick
    if (S != 0) { slideV += (S * 50.f - slideV) / 16.f; slideSm = approach(slideSm, slideV, 25.f); }
    else { slideV -= slideV / 16.f; if (std::fabs(slideV) < 16.f) slideV = 0; slideSm = approach(slideSm, slideV, 30.f); }
    // forward 120/step, backward 60/step (0x33642); smoothed 30/tick in the pressed direction, 20 otherwise
    fwdV += ((F > 0 ? F * 120.f : F * 60.f) - fwdV) / 16.f;
    if (F == 0 && std::fabs(fwdV) < 16.f) fwdV = 0;
    bool same = F != 0 && !((F > 0 && fwdSm < 0) || (F < 0 && fwdSm > 0));
    fwdSm = approach(fwdSm, fwdV, same ? 30.f : 20.f);
    // rotation (0x3386b): 160/step with a 1/32 filter, smoothed 60/tick; released: 1/16 decay, 50/tick
    if (R != 0) { rotV += (R * 160.f - rotV) / 32.f; rotSm = approach(rotSm, rotV, 60.f); }
    else { rotV -= rotV / 16.f; if (std::fabs(rotV) < 32.f) rotV = 0; rotSm = approach(rotSm, rotV, 50.f); }
    yaw += rotSm * scale[1] / 8.f * kAngle;
    {   // 0x62f98 (per tick): turn input -> target R*6144, approached by 0x800 steps, then a 1/16 filter; the cockpit
        // camera's extra gamma is that / 512 (+-144 of 4096 at full turn, ahead of the turn). Height input: the
        // value rises by 0x200 to H*1024 or falls to H*2048, 1/16 filter, / 512 -> extra alpha (look up / down).
        const float tc = R * 6144.f;
        if (tc > camTurnC) camTurnC = std::min(camTurnC + 2048.f, tc); else if (tc < camTurnC) camTurnC = std::max(camTurnC - 2048.f, tc);
        camTurnD += std::trunc((camTurnC - camTurnD) * 32.f / 512.f);
        if (H * 1024.f > camLiftA) camLiftA += 512.f; else if (H * 2048.f < camLiftA) camLiftA -= 512.f;
        camLiftB += std::trunc((camLiftA - camLiftB) * 32.f / 512.f);
    }
    // height (0x3396c): +-1.25 per tick and step, released: back to 0 by 20/tick, at most 800
    if (H == 0) liftV = approach(liftV, 0.f, 20.f);
    liftV = std::clamp(liftV + H * 20.f / 16.f, -800.f, 800.f);
    if (landed && liftV < 0) liftV = 0;

    Vec3 move = (forwardDir() * (fwdSm * scale[0]) + rightDir() * (slideSm * scale[2]) + Vec3{0, 0, liftV * scale[3]}) * unitScale;
    vel = move * (1.f / dt);
    // visual tilt: pitch -fwd*30/32, bank = rotation + rotation*fwd/2300 - 3*slide (clamped to 5000)
    tiltPitch = -fwdV * 30.f / 32.f * kAngle;
    float bank = std::clamp(rotSm + rotSm * fwdV / 2300.f - slideSm * 3.f, -5000.f, 5000.f);
    tiltRoll += (-bank * kAngle - tiltRoll) * std::min(1.f, dt * 10.f);

    pos = pos + move;
    if (world) {
        Vec3 n{0, 0, 1};
        Vec3 body = pos;                               // sphere centred slightly above skids
        world->resolveSphere(body, radius, &vel, &n);
        pos = body;
        float ground = world->groundBelow(pos + Vec3{0, 0, 1});
        landed = false;
        if (ground > -1e8f && pos.z - ground < clearance) {
            pos.z = ground + clearance;
            if (liftV < 0) liftV = 0;
            landed = true;
        }
    }
}

} // namespace xa
