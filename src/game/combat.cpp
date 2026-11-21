#include "combat.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace xa {

namespace {
enum Kind { Bullet, Rocket, EnemyBullet, EnemyRocket };
const float kBulletSpeed = 600.f, kPowerSpeed = 420.f, kBeamSpeed = 900.f, kRocketSpeed = 260.f;
const float kRocketSplash = 14.f;
const float kTick = 60.f;                       // original game tick (mission timer unit = 1/60 s)
// Extra (pickup) table, same order as the original handler jump table (0x79390).
const char* kExtraNames[19] = {"SMARTBOMB", "FREEZEBOMB", "RADAR", "INVISIBLE", "WEAPON1MUN", "WEAPON2MUN", "WEAPON3MUN",
                               "WEAPON1LEVEL", "WEAPON2LEVEL", "WEAPON3LEVEL", "ROCKETS", "IMMORTAL", "SHIELD", "MAXSHIELD",
                               "MEGASHIELD", "NAVCOM", "MAGMA_ACTIVATE", "MAGMA", "HOSTAGE"};
enum Extra { XSmart, XFreeze, XRadar, XInvisible, XW1Mun, XW2Mun, XW3Mun, XW1Lvl, XW2Lvl, XW3Lvl, XRockets, XImmortal,
             XShield, XMaxShield, XMegaShield, XNavcom, XMagmaAct, XMagma, XHostage };
// RELEASE_EXTRA codes: random kinds 1..5, a fixed extra is 100 + index
enum { RNone = -1, RRandom = 1, RSeldom, RBetter, RShieldR, RAmmo };
std::string upper(std::string v) { for (auto& c : v) c = char(std::toupper(unsigned(c))); return v; }
bool isExtraBehavior(const std::string& behavior, int x) {
    std::string b = upper(behavior);
    return b == std::string(kExtraNames[x]) + "_EXTRA" || (x >= XMagmaAct && x != XMagma && b == kExtraNames[x]);
}

Mat4 orientAlong(Vec3 dir) {
    float yaw = std::atan2(-dir.x, dir.y);
    float pitch = std::atan2(dir.z, std::sqrt(dir.x * dir.x + dir.y * dir.y));
    return Mat4::rotZ(yaw) * Mat4::rotX(pitch);
}
}

const char* Combat::extraName(int x) { return x >= 0 && x < 19 ? kExtraNames[x] : "?"; }

int Combat::findModelForClass(int cls) const {
    if (cls < 0) return -1;
    int best = -1;
    const auto& ms = lib_->models();
    for (size_t i = 0; i < ms.size(); i++) {
        if (ms[i].objClass != cls) continue;
        if (!ms[i].lods.empty()) return int(i);               // LOD selector is the canonical entry
        if (best < 0 || ms[i].name.find(".3DI") != std::string::npos) best = int(i);
    }
    return best;
}

void Combat::init(const World& world, const ModelLib& lib, const TypeDB& types, const DefDB& defs,
                  const SpriteLib& sprites, Renderer& renderer, CollisionWorld& coll, Audio* audio, int playerWorld) {
    lib_ = &lib; types_ = &types; sprites_ = &sprites; renderer_ = &renderer; coll_ = &coll; audio_ = audio; world_ = &world;
    // name -> TYP index
    auto typeIndex = [&](const std::string& n) {
        for (size_t i = 0; i < types.size(); i++) if (types.get(i)->name == n) return int(i);
        return -1;
    };
    bulletClass_ = typeIndex("MG_SHOOT");      bulletModel_ = findModelForClass(bulletClass_);
    rocketClass_ = typeIndex("ROCKET_SHOOT");  rocketModel_ = findModelForClass(rocketClass_);
    if (int i = lib.find("GESCHOSS.3DI"); i >= 0) bulletModel_ = i;
    enemyBulletClass_ = typeIndex("ENEMY_SHOOT"); enemyBulletModel_ = findModelForClass(enemyBulletClass_);
    enemyRocketClass_ = typeIndex("SMALL_ROCKET"); enemyRocketModel_ = findModelForClass(enemyRocketClass_);

    auto shot = [&](const char* n, float defDamage) {
        ShotType t; t.cls = typeIndex(n); t.model = findModelForClass(t.cls);
        t.damage = float(defs.get(n, "cause_damage", int(defDamage)));
        return t;
    };
    const char* mg[3] = {"MG_SHOOT", "MG_SHOOT_1", "MG_SHOOT_2"};
    const char* pw[3] = {"POWER_SHOOT", "POWER_SHOOT_1", "POWER_SHOOT_2"};
    const char* bm[3] = {"SHOOT_BEAM3", "SHOOT_BEAM4", "SHOOT_BEAM5"};
    for (int l = 0; l < 3; l++) { wType_[0][l] = shot(mg[l], 300); wType_[1][l] = shot(pw[l], 1500); wType_[2][l] = shot(bm[l], 1000); }
    for (int l = 0; l < 3; l++) if (wType_[0][l].model < 0) wType_[0][l].model = bulletModel_;
    rocketType_ = shot("ROCKET_SHOOT", 10000); if (rocketType_.model < 0) { rocketType_.model = rocketModel_; rocketType_.cls = rocketClass_; }
    smartType_ = shot("SMARTBOMB_SHOOT", 20000); if (smartType_.model < 0) { smartType_.model = rocketType_.model; smartType_.cls = rocketType_.cls; }
    thudType_ = shot("ROCKET_THUD", 20000); if (thudType_.model < 0) { thudType_.model = rocketType_.model; thudType_.cls = rocketType_.cls; }
    enemyBulletDamage_ = float(defs.get("ENEMY_SHOOT", "cause_damage", 300));
    enemyRocketDamage_ = float(defs.get("SMALL_ROCKET_ENEMY", "cause_damage", 1000));
    extraClass_.assign(19, -1); extraModel_.assign(19, -1);
    for (size_t i = 0; i < types.size(); i++) {
        std::string b = upper(types.get(i)->behavior);
        for (int x = 0; x < 19; x++)
            if (b == std::string(kExtraNames[x]) + "_EXTRA" || (x >= XMagmaAct && x != XMagma && b == kExtraNames[x])) {
                if (extraClass_[size_t(x)] < 0) { extraClass_[size_t(x)] = int(i); extraModel_[size_t(x)] = findModelForClass(int(i)); }
            }
    }
    auto releaseCode = [&](const std::string& r0) {
        std::string r = upper(r0);
        if (r.empty() || r == "NEVER") return int(RNone);
        if (r == "RANDOM") return int(RRandom);
        if (r == "RANDOM_SELDOM") return int(RSeldom);
        if (r == "RANDOM_BETTER") return int(RBetter);
        if (r == "RANDOM_SHIELD") return int(RShieldR);
        if (r == "RANDOM_AMMO") return int(RAmmo);
        for (int x = 0; x < 19; x++) if (r == kExtraNames[x] || r == std::string(kExtraNames[x]) + "UP" || r == std::string(kExtraNames[x]) + "_EXTRA") return 100 + x;
        return int(RNone);
    };

    const auto& objs = world.objects();
    targets_.assign(objs.size(), Target{});
    releaseOf_.assign(objs.size(), RNone);
    pickups_.clear();
    for (size_t i = 0; i < objs.size(); i++) {
        const ObjectType* t = types.get(objs[i].objClass);
        if (!t) continue;
        releaseOf_[i] = releaseCode(t->releaseExtra);
        for (int x = 0; x < 19; x++)
            if (int(objs[i].objClass) == extraClass_[size_t(x)] || isExtraBehavior(t->behavior, x)) {      // extra placed in the world
                pickups_.push_back({x, {objs[i].pos[0] * renderer.unitScale, objs[i].pos[1] * renderer.unitScale, objs[i].pos[2] * renderer.unitScale},
                                    -1, int(objs[i].objClass), int(i)});
                coll.setOwnerEnabled(int(i), false);
                // Extras stored below the terrain are the level's drop pool: the original (0x733df)
                // takes a matching extra from the active object list and moves it to the wreck.
                Pickup& pk = pickups_.back();
                pk.radius = std::max(3.f, float(objs[i].radius) * renderer.unitScale);
                // (caves, e.g. level 2: an extra standing on a floor under the terrain is a normal one)
                float g = coll.groundBelow(pk.pos + Vec3{0, 0, 500});
                float floor = coll.groundBelow(pk.pos + Vec3{0, 0, 2});
                bool standing = floor > -1e8f && pk.pos.z - floor < 8.f;
                pk.pooled = (g > -1e8f && pk.pos.z < g - 5.f && !standing) || isTemplateSet(objs[i].set);
                if (std::getenv("XA_DEBUG") && standing && g > -1e8f && pk.pos.z < g - 5.f) std::printf("  cave extra %s at %.0f,%.0f,%.0f (floor %.0f, surface %.0f)\n", kExtraNames[x], pk.pos.x, pk.pos.y, pk.pos.z, floor, g);
                break;
            }
    }
    for (size_t i = 0; i < objs.size(); i++) {
        if (int(i) == playerWorld || isTemplateSet(objs[i].set)) continue;
        const ObjectType* t = types.get(objs[i].objClass);
        if (!t) continue;
        Target& tg = targets_[i];
        tg.origModel = int(objs[i].proto); tg.origClass = int(objs[i].objClass);
        tg.radius = float(objs[i].radius) * renderer.unitScale;
        bool destroyable = !t->destroyedType.empty() && t->destroyedType != "NOTHING";
        int hp = defs.get(t->behavior, "get_damage", -1);
        if (!destroyable && hp < 0) continue;
        if (t->behavior == "GROUND") continue;
        tg.isTarget = true;
        tg.maxHp = tg.hp = float(hp > 0 ? hp : (t->behavior == "TREE" ? 400 : 3000));
        tg.score = defs.get(t->behavior, "score", 10);
        if (destroyable) { tg.wreckClass = typeIndex(t->destroyedType); tg.wreckModel = findModelForClass(tg.wreckClass); }
    }
    int nx = 0; for (int m : extraModel_) nx += m >= 0;
    std::printf("Extras: %d/19 models, %zu placed in the world\n", nx, pickups_.size());
    if (std::getenv("XA_DEBUG"))
        for (const auto& p : pickups_)
            std::printf("  extra %s at %.0f,%.0f,%.0f r %.1f%s set %s typ %s (world %d)\n", kExtraNames[p.extra], p.pos.x, p.pos.y, p.pos.z, float(objs[size_t(p.world)].radius) * renderer.unitScale, p.pooled ? " (pool)" : "", objs[size_t(p.world)].set.c_str(), types.get(objs[size_t(p.world)].objClass) ? types.get(objs[size_t(p.world)].objClass)->name.c_str() : "?", p.world);
    if (audio) {
        sGun_ = audio->sound("FIRE001.WAV"); sRocket_ = audio->sound("ROCKET.WAV");
        sHit_ = audio->sound("ENEMYHIT.WAV"); sSmall_ = audio->sound("SMLEXPLO.WAV");
        sAir_ = audio->sound("AIREXPLO.WAV"); sGround_ = audio->sound("GRNEXPLO.WAV");
        sEnemyShot_ = audio->sound("ENSHOOT1.WAV"); sHeliHit_ = audio->sound("HELIHIT.WAV");
        sExtra_ = audio->sound("EXTRA.WAV");
    }
}

void Combat::setActive(int wi, bool active) {
    if (wi < 0 || size_t(wi) >= targets_.size()) return;
    targets_[size_t(wi)].active = active;
}

void Combat::resetTarget(int wi) {
    if (wi < 0 || size_t(wi) >= targets_.size()) return;
    Target& t = targets_[size_t(wi)];
    t.hp = t.maxHp; t.alive = true;
    if (Renderer::Instance* in = renderer_->instanceForWorldObject(wi)) {
        if (t.origModel >= 0) { in->model = t.origModel; in->objClass = t.origClass; }
        bool collected = false;                       // collected extras/hostages stay away
        for (const auto& p : pickups_) if (p.world == wi && p.extra < 0) collected = true;
        in->hidden = collected;
        in->animTicks = -1;
    }
}

void Combat::saveState(std::vector<uint8_t>& alive, std::vector<int>& taken) const {
    alive.assign(targets_.size(), 1);
    for (size_t i = 0; i < targets_.size(); i++) if (targets_[i].isTarget && !targets_[i].alive) alive[i] = 0;
    taken.clear();
    for (const auto& p : pickups_) if (p.world >= 0 && p.extra < 0) taken.push_back(p.world);
}

void Combat::loadState(const std::vector<uint8_t>& alive, const std::vector<int>& taken) {
    for (size_t i = 0; i < targets_.size() && i < alive.size(); i++) {
        Target& t = targets_[i];
        if (!t.isTarget || alive[i]) continue;
        t.alive = false; t.hp = 0;
        if (Renderer::Instance* in = renderer_->instanceForWorldObject(int(i))) {
            if (t.wreckModel >= 0) { in->model = t.wreckModel; in->objClass = t.wreckClass; in->animTicks = renderer_->currentTicks; }
            else in->hidden = true;
        }
    }
    for (int w : taken)
        for (auto& p : pickups_) if (p.world == w) { p.extra = -1; p.taken = -1; if (Renderer::Instance* in = renderer_->instanceForWorldObject(w)) in->hidden = true; }
}

void Combat::fireEnemy(Vec3 origin, Vec3 dir, float speed, bool rocket, float damage) {
    if (damage <= 0) damage = rocket ? enemyRocketDamage_ : enemyBulletDamage_;
    shots_.push_back({rocket ? EnemyRocket : EnemyBullet, origin, dir * speed, rocket ? 5.f : 3.f, damage * scaleEnemyShots});
    if (audio_) audio_->playAt(rocket ? sRocket_ : sEnemyShot_, origin, 0.6f);
}

void Combat::setGunSound(bool firing) {
    if (!audio_) return;
    if (firing && !gunVoice_) gunVoice_ = audio_->play(sGun_, 0.7f, 0, true);
    if (!firing && gunVoice_) { audio_->stop(gunVoice_); gunVoice_ = 0; }
}

bool Combat::nearestPickup(Vec3 from, Vec3& out) const {
    float best = 1e30f; bool found = false;
    for (const auto& p : pickups_) {
        if (p.extra < 0 || p.pooled) continue;
        Vec3 pos = p.pos;
        if (p.world >= 0) { Renderer::Instance* in = renderer_->instanceForWorldObject(p.world); if (!in || in->hidden) continue; pos = in->pos; }
        Vec3 d = pos - from; float d2 = dot(d, d);
        if (d2 < best) { best = d2; out = pos; found = true; }
    }
    return found;
}

void Combat::missionStarted() { for (auto& p : pickups_) if (p.world >= 0 && p.extra < 0) p.taken = -1; }

void Combat::missionRestarted() {
    for (auto& p : pickups_) {
        if (p.world < 0 || p.taken < 0) continue;
        p.extra = p.taken; p.taken = -1;
        if (Renderer::Instance* in = renderer_->instanceForWorldObject(p.world)) in->hidden = false;
    }
}

void Combat::hostages(std::vector<Vec3>& out) const {
    out.clear();
    for (const auto& p : pickups_) {
        if (p.extra != XHostage || p.pooled) continue;
        if (p.world >= 0) {
            const Target* t = target(p.world);
            if (t && !t->active) continue;
            Renderer::Instance* in = renderer_->instanceForWorldObject(p.world);
            if (!in || in->hidden || renderer_->hiddenSets.count(in->set)) continue;
            out.push_back(in->pos);
        } else out.push_back(p.pos);
    }
}

void Combat::resetArms(int difficulty) {
    arms = Arms{};
    arms.rockets = difficulty == 1 ? 0 : 2;
    for (auto& c : wCool_) c = 0;
}

bool Combat::fireWeapon(Vec3 origin, Vec3 right, Vec3 dir, Vec3 inherit) {
    const int w = arms.sel;
    if (wCool_[w] > 0) return false;
    if (w == 2 && (!arms.weapon3 || arms.ammo[2] <= 0)) return false;
    const int mode = weaponMode(w);
    // cooldowns in ticks per fire mode (original: MG 16/8/5, power shot 24/16/12)
    static const int cool[3][3] = {{16, 8, 5}, {24, 16, 12}, {12, 8, 5}};
    static const int barrels[3][3] = {{1, 2, 3}, {2, 2, 4}, {1, 1, 1}};
    wCool_[w] = float(cool[w][mode]) / kTick;
    const int nb = barrels[w][mode];
    const int b = barrel_[w]++ % nb;
    float off = nb == 1 ? 0.f : nb == 2 ? (b ? 0.9f : -0.9f) : nb == 3 ? float(b - 1) * 0.9f : (float(b) - 1.5f) * 0.7f;
    arms.ammo[w] = std::max(0, arms.ammo[w] - 10);
    const ShotType& st = wType_[w][std::clamp(arms.level[w], 0, 2)];
    const float speed = w == 0 ? kBulletSpeed : w == 1 ? kPowerSpeed : kBeamSpeed;
    shots_.push_back({Bullet, origin + right * off, dir * speed + inherit, 1.6f, st.damage, st.model, st.cls});
    if (audio_ && w > 0) audio_->playAt(sEnemyShot_, origin, 0.5f);
    return true;
}

bool Combat::fireRocket(Vec3 origin, Vec3 dir, Vec3 inherit, int targetWorld) {
    if (rocketCooldown > 0 || arms.rockets <= 0) return false;
    rocketCooldown = 0.35f;
    arms.rockets--;
    Projectile p{Rocket, origin, dir * kRocketSpeed + inherit, 6.f, rocketType_.damage, rocketType_.model, rocketType_.cls, targetWorld, kRocketSplash};
    shots_.push_back(p);
    if (audio_) audio_->playAt(sRocket_, origin, 1.f);
    return true;
}

bool Combat::fireSmartbomb(Vec3 origin, Vec3 inherit, const std::vector<int>& targets) {
    if (rocketCooldown > 0 || arms.smartbombs <= 0) return false;
    if (targets.empty()) return false;
    rocketCooldown = 0.6f;
    arms.smartbombs--;
    for (size_t i = 0; i < targets.size(); i++) {
        Renderer::Instance* in = renderer_->instanceForWorldObject(targets[i]);
        Vec3 dir = in ? normalize(in->pos - origin) : Vec3{0, 0, 1};
        Vec3 up{0, 0, 1};
        shots_.push_back({Rocket, origin + up * float(i % 3), normalize(dir + up * 0.3f) * kRocketSpeed + inherit, 8.f,
                          smartType_.damage, smartType_.model, smartType_.cls, targets[i], 10.f});
    }
    if (audio_) audio_->playAt(sRocket_, origin, 1.f);
    return true;
}

bool Combat::fireThunderbolt(Vec3 origin, Vec3 dir, Vec3 inherit, int targetWorld) {
    if (rocketCooldown > 0 || arms.thunderbolts <= 0) return false;
    rocketCooldown = 0.6f;
    arms.thunderbolts--;
    shots_.push_back({Rocket, origin, dir * kRocketSpeed + inherit, 6.f, thudType_.damage, thudType_.model, thudType_.cls, targetWorld, 30.f});
    if (audio_) audio_->playAt(sRocket_, origin, 1.f);
    return true;
}

static unsigned nextRand(unsigned& s) { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7fff; }

// Original drop logic (0x731a1): a shared kill counter gates RANDOM (every 3-4 kills) and
// RANDOM_SELDOM (every 10-12); the extra kind is then rolled from 0..100.
void Combat::dropExtra(int wi, Vec3 at) {
    if (wi < 0 || size_t(wi) >= releaseOf_.size()) return;
    int code = releaseOf_[size_t(wi)];
    if (code == RNone) return;
    // 0x7295c flags: trees (8, 0x376bd ...) release nothing; the ground kinds of the dispatcher call it with 2,
    // which puts the extra half a radius above the object (0x73519); air units (1) and the rest leave it where the
    // object is. Extras have no gravity: they float like the placed ones.
    const ObjectType* ot = world_ && size_t(wi) < world_->objects().size() ? types_->get(world_->objects()[size_t(wi)].objClass) : nullptr;
    const std::string beh = ot ? ot->behavior.substr(0, ot->behavior.find('.')) : std::string();
    if (beh == "TREE") return;
    static const std::set<std::string> raised = {"ESEL_ENEMY", "ESEL_AS_ENEMY", "BLITZ_ENEMY", "DRILL", "BLOCK_ENEMY",
        "BLOCK_NO_GRND_ENEMY", "MOVINGWALL", "PANTHER_ENEMY", "BARRACUDA_ENEMY", "BUGGY_ENEMY", "SPIDER_ENEMY",
        "SPIDER360_ENEMY", "ADLER_ENEMY", "R_TURM", "G_TURM", "IMPERATOR_ENEMY", "BUILDING_ENEMY", "VAPORATOR"};
    if (raised.count(beh)) at.z += targets_[size_t(wi)].radius * 0.5f;
    if (code == RRandom || code == RSeldom) {
        releaseCounter_++;
        int need = code == RRandom ? 3 + int(nextRand(rng_) % 2) : 10 + int(nextRand(rng_) % 3);
        if (need > releaseCounter_) return;
        releaseCounter_ = 0;
    }
    int x;
    int r = int(nextRand(rng_) % 101);
    switch (code) {
    case RRandom: case RSeldom: x = r < 50 ? XW1Mun : r < 90 ? XW2Mun : XRockets; break;
    case RBetter: x = r < 5 ? XSmart : r < 80 ? XMaxShield : r < 90 ? XFreeze : XRockets; break;
    case RShieldR: x = r < 75 ? XShield : XMaxShield; break;
    case RAmmo: x = r < 40 ? XW1Mun : r < 80 ? XW2Mun : XRockets; break;
    default: x = code - 100;
    }
    spawnExtra(x, at);
}

void Combat::setDropSources(const std::vector<std::string>& missionSets) {
    dropLand_.clear(); dropTemplate_.clear(); dropFirst_ = -1;
    if (!world_ || !types_) return;
    // behaviours the loader (0x60769) puts into 0x21c5a8: enemies 0x53..0x7d and extras 0x80..0x99
    static const std::set<std::string> listed = {
        "BUILDING_ENEMY", "SHIELD_ENEMY", "BLOCK_ENEMY", "BLOCK_NO_GRND_ENEMY", "PLUTO_ENEMY", "MOVINGWALL", "VAPORATOR",
        "BIENENSTOCK_ENEMY", "KOLIBRI_ENEMY", "WESPE_ENEMY", "MOSQUITO_ENEMY", "DEATHSTORM_ENEMY", "TERROR_GOD_ENEMY",
        "SLIDER_ENEMY", "VIPER_ENEMY", "THUNDER_ENEMY", "EBOLA_ENEMY", "IMPERATOR_ENEMY", "FLOH_ENEMY", "BANDIT_ENEMY",
        "KILLER_ENEMY", "ESEL_ENEMY", "ESEL_AS_ENEMY", "BLITZ_ENEMY", "PANTHER_ENEMY", "BUGGY_ENEMY", "SPIDER_ENEMY",
        "SPIDER360_ENEMY", "NERO_ENEMY", "CAMEL_ENEMY", "SPITFIRE_ENEMY", "BARRACUDA_ENEMY", "MARS_ENEMY", "ADLER_ENEMY",
        "TARANTULA_ENEMY", "SKORPION_ENEMY", "ENDBOSS_CENSOR_ENEMY", "AMBUSH", "TUNNELPATH", "DRILL", "G_TURM", "R_TURM",
        "MARINER_ENEMY", "ALWAYS", "NEVER", "RANDOM", "RANDOM_SELDOM", "RANDOM_BETTER", "RANDOM_SHIELD", "RANDOM_AMMO",
        "SMARTBOMB_EXTRA", "FREEZEBOMB_EXTRA", "RADAR_EXTRA", "INVISIBLE_EXTRA", "WEAPON1MUN_EXTRA", "WEAPON2MUN_EXTRA",
        "WEAPON3MUN_EXTRA", "WEAPON1LEVEL_EXTRA", "WEAPON2LEVEL_EXTRA", "WEAPON3LEVEL_EXTRA", "ROCKETS_EXTRA",
        "IMMORTAL_EXTRA", "SHIELD_EXTRA", "MAXSHIELD_EXTRA", "MEGASHIELD_EXTRA", "NAVCOM_EXTRA", "MAGMA_ACTIVATE",
        "MAGMA_EXTRA", "HOSTAGE"};
    auto upper = [](std::string s) { for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; };
    std::vector<int> kindOf(world_->objects().size(), -1);
    for (const auto& pk : pickups_) if (pk.world >= 0 && size_t(pk.world) < kindOf.size()) kindOf[size_t(pk.world)] = pk.extra;
    const auto& objs = world_->objects();
    for (size_t i = 0; i < objs.size(); i++) {
        const std::string set = upper(objs[i].set);
        const bool tpl = isTemplateSet(objs[i].set);
        if (!tpl && std::find(missionSets.begin(), missionSets.end(), set) != missionSets.end()) continue;
        const ObjectType* t = types_->get(objs[i].objClass);
        if (!t) continue;
        const std::string beh = upper(t->behavior.substr(0, t->behavior.find('.')));
        if (!listed.count(beh)) continue;
        const int x = kindOf[i];
        if (!tpl && dropFirst_ == -1) dropFirst_ = x >= 0 ? x : -2;   // an extra here is dropLand_[0]
        if (x < 0) continue;
        DropSource s{x, int(objs[i].objClass), int(objs[i].proto)};
        (tpl ? dropTemplate_ : dropLand_).push_back(s);
    }
    if (std::getenv("XA_DEBUG")) {
        std::printf("drop sources: %zu landscape, %zu template, first record %s; none for:", dropLand_.size(), dropTemplate_.size(),
                    dropFirst_ >= 0 ? kExtraNames[dropFirst_] : "not an extra");
        for (int x = 0; x < 19; x++) {
            bool any = false;
            for (const auto& s : dropLand_) any |= s.extra == x;
            for (const auto& s : dropTemplate_) any |= s.extra == x;
            if (!any) std::printf(" %s", kExtraNames[x]);
        }
        std::printf("\n");
    }
}

void Combat::spawnExtra(int x, Vec3 at) {
    if (x < 0 || x >= 19) return;
    // 0x733df: the dropped extra is a copy of the first record of the wanted behaviour in 0x21c5a8 (landscape
    // files), else in 0x21c5a0 (template sets); the source stays where it is. Without one the first record of
    // 0x21c5a8 is copied, and only an extra is released (0x734ca).
    const DropSource* src = nullptr;
    for (const auto& s : dropLand_) if (s.extra == x) { src = &s; break; }
    if (!src) for (const auto& s : dropTemplate_) if (s.extra == x) { src = &s; break; }
    if (!src && dropFirst_ >= 0 && !dropLand_.empty()) { src = &dropLand_[0]; x = src->extra; }
    if (!src) { if (std::getenv("XA_DEBUG")) std::printf("drop %s: no record to copy\n", kExtraNames[x]); return; }
    const Vec3 p = at;   // the copy takes the object's place (0x73504), it is not put on the ground
    const ObjectType* st = types_->get(size_t(src->cls));
    if (std::getenv("XA_DEBUG")) std::printf("drop %s (%s) at %.0f,%.0f,%.0f\n", kExtraNames[x], st ? st->name.c_str() : "?", p.x, p.y, p.z);
    pickups_.push_back({x, p, src->model, src->cls, -1});
    pickups_.back().life = pilot > 0 ? 1500.f / kTick : -1.f;
    if (src->model >= 0) pickups_.back().radius = std::max(3.f, lib_->models()[size_t(src->model)].radius * renderer_->unitScale);
}

// Pickup effects (original handler table at 0x79390). An extra that would have no effect
// (ammo, shield or rockets already full) stays on the ground.
bool Combat::collect(int x) {
    auto& a = arms;
    switch (x) {
    case XSmart: if (a.smartbombs >= 99) return false; a.smartbombs++; break;
    case XFreeze: if (a.thunderbolts >= 99) return false; a.thunderbolts++; break;
    case XW1Mun: case XW2Mun: case XW3Mun: { int w = x - XW1Mun; if (a.ammo[w] >= 6000) return false; a.ammo[w] = std::min(6000, a.ammo[w] + 2000); break; }
    case XW1Lvl: if (a.level[0] >= 2) return false; a.level[0]++; break;
    case XW2Lvl: if (a.level[1] >= 2) return false; a.level[1]++; break;
    case XW3Lvl: if (a.weapon3) { if (a.level[2] >= 2) return false; a.level[2]++; } a.weapon3 = true; a.ammo[2] = std::max(a.ammo[2], 2000); break;
    case XRockets: if (a.rockets >= 6) return false; a.rockets++; break;
    case XImmortal: if (a.immortal >= 10000) return false; a.immortal = 10000; break;
    case XShield: if (a.shield >= kMaxShield) return false; a.shield = std::min(kMaxShield, a.shield + 2000); break;
    case XMaxShield: if (a.shield >= kMaxShield) return false; a.shield = kMaxShield; break;
    case XHostage: if (a.hostages >= 64) return false; a.hostages++; break;
    // 0x79aeb MAGMA: full magma drive (10000 = 667 ticks), default speeds
    case XMagma: if (a.magma >= 10000) return false; a.magma = 10000; a.magmaSpeed = 0; a.magmaOn = true; break;
    // 0x79c02 MAGMA_ACTIVATE (speed-up pads): MAGMA_SPEED_AND_TIME speed ticks, only when the drive is off
    case XMagmaAct: {
        if (a.magmaOn) return false;
        const ObjectType* t = types_ && pickupClass_ >= 0 ? types_->get(size_t(pickupClass_)) : nullptr;
        a.magma = t && t->magmaTime >= 0 ? float(t->magmaTime) * 15.f : 10000.f;
        a.magmaSpeed = t && t->magmaSpeed >= 0 ? float(t->magmaSpeed) : 0.f;
        a.magmaOn = true;
        return false; }                     // the pad is not used up (esi stays 0 in 0x79c02)
    // 0x79c84: RADAR, MEGASHIELD and NAVCOM only show their name (no effect in the original)
    case XRadar: case XMegaShield: case XNavcom: break;
    default: break;
    }
    if (audio_) audio_->play(sExtra_, 1.f);
    if (onPickup) onPickup(x);
    return true;
}

void Combat::explode(Vec3 at, float size, bool big) {
    using K = SpriteLib::Kind;
    if (onExplosion) onExplosion(at, size, big);
    if (big) {
        effects_.push_back({K::GroundExplosion, at, 0, 1.1f, size});
        effects_.push_back({K::AirExplosion, at + Vec3{0, 0, size * 0.4f}, -0.15f, 1.0f, size * 0.8f});
        effects_.push_back({K::Smoke, at + Vec3{0, 0, size * 0.6f}, -0.3f, 2.2f, size * 1.2f});
        if (audio_) audio_->playAt(sGround_, at, 1.f);
    } else {
        effects_.push_back({K::SmallExplosion, at, 0, 0.35f, size});
    }
}

void Combat::damage(int wi, float amount, Vec3 at) {
    if (wi < 0 || size_t(wi) >= targets_.size()) return;
    Target& t = targets_[size_t(wi)];
    if (!t.isTarget || !t.alive || !t.active) return;
    if (t.shielded) { if (audio_ && hitSoundCooldown_ <= 0) { audio_->playAt(sHit_, at, 0.5f); hitSoundCooldown_ = 0.08f; } return; }
    t.hp -= amount * scaleAdvShots;
    if (audio_ && hitSoundCooldown_ <= 0) { audio_->playAt(sHit_, at, 0.8f); hitSoundCooldown_ = 0.08f; }
    if (t.hp > 0) return;
    t.alive = false;
    score += t.score * scorePercent / 100; kills++;
    coll_->setOwnerEnabled(wi, false);
    t.dynamic = false;
    if (onDestroyed && onDestroyed(wi)) {            // crashing: wreck comes later
        explode(at, std::clamp(t.radius * 0.5f, 4.f, 20.f), false);
        if (audio_) audio_->playAt(sAir_, at, 0.9f);
        return;
    }
    Renderer::Instance* in = renderer_->instanceForWorldObject(wi);
    finishWreck(wi, in ? in->pos : at);
}

bool Combat::incomingMissile() const {
    if (!playerAlive_) return false;
    for (const auto& pr : shots_) {
        if (pr.kind != EnemyRocket) continue;
        Vec3 d = playerPos_ - pr.pos;
        if (dot(d, d) < 250.f * 250.f && dot(pr.vel, d) > 0) return true;
    }
    return false;
}

void Combat::ramPlayer(int wi, float playerDamage, Vec3 at) {
    if (arms.immortal <= 0) playerDamage_ += playerDamage;
    if (wi < 0 || size_t(wi) >= targets_.size()) return;
    Target& t = targets_[size_t(wi)];
    if (!t.alive) return;
    t.hp = 0; t.alive = false;
    score += t.score * scorePercent / 100; kills++;
    coll_->setOwnerEnabled(wi, false);
    t.dynamic = false;
    finishWreck(wi, at);
}

void Combat::finishWreck(int wi, Vec3 c) {
    if (wi < 0 || size_t(wi) >= targets_.size()) return;
    Target& t = targets_[size_t(wi)];
    Renderer::Instance* in = renderer_->instanceForWorldObject(wi);
    explode(c + Vec3{0, 0, t.radius * 0.2f}, std::clamp(t.radius * 1.1f, 6.f, 60.f), true);
    dropExtra(wi, c);
    if (in) {
        in->pos = c;
        in->animTicks = in->animTicks >= 0 ? in->animTicks : renderer_->currentTicks;   // wrecks don't animate (rotors stop)
        if (t.wreckModel >= 0) { in->model = t.wreckModel; in->objClass = t.wreckClass; }
        else in->hidden = true;
    }
}

void Combat::update(float dt) {
    // 0x79f6f: the magma drive burns 15 fuel per tick
    if (arms.magmaOn) { arms.magma -= 15.f * dt * kTick; if (arms.magma <= 0) { arms.magma = 0; arms.magmaOn = false; } }
    for (auto& c : wCool_) c -= dt;
    rocketCooldown -= dt; hitSoundCooldown_ -= dt;
    if (arms.immortal > 0) arms.immortal = std::max(0.f, arms.immortal - 6.f * kTick * dt);
    extras.clear();
    // pickups: spin, collect when the helicopter touches them (original 0x793e5: the player's
    // collision box against the extra's bounds; the flare makes extras fairly large)
    auto touch = [&](const Pickup& p, Vec3 d) {
        float r = playerRadius_ + p.radius;
        return d.x * d.x + d.y * d.y < r * r && std::fabs(d.z) < r + 2.f;
    };
    for (auto& p : pickups_) {
        p.age += dt;
        if (p.extra < 0) continue;
        if (p.world >= 0) {
            if (p.pooled) continue;
            Renderer::Instance* in = renderer_->instanceForWorldObject(p.world);
            if (!in) continue;
            if (p.life > 0) {                                  // appeared extras with a time limit (0x590a0)
                p.life -= dt;
                if (p.life <= 0) { in->hidden = true; p.extra = -1; continue; }
                in->hidden = p.life < 300.f / kTick && std::fmod(p.age, 0.2f) < 0.1f;
                if (in->hidden) continue;
            }
            if (in->hidden) continue;
            if (p.extra == XHostage && playerAlive_ && playerLanded) {      // hostages run to the landed helicopter
                Vec3 d = playerPos_ - in->pos; d.z = 0;
                float l = std::sqrt(dot(d, d));
                if (l < 60.f && l > 1.f) {
                    Vec3 np = in->pos + d * (std::min(l, 6.f * dt) / l);
                    float g = coll_->groundBelow(np + Vec3{0, 0, 3});
                    if (g > -1e8f) np.z = g + (in->pos.z - coll_->groundBelow(in->pos + Vec3{0, 0, 3}));
                    in->pos = np;
                    in->rot = Mat4::rotZ(std::atan2(-d.x, d.y));
                }
            }
            p.pos = in->pos;
            if (p.extra == XHostage && playerAlive_ && playerLanded) {          // released hostages run to the helicopter too
            Vec3 d = playerPos_ - p.pos; d.z = 0;
            float l = std::sqrt(dot(d, d));
            if (l < 60.f && l > 1.f) {
                p.pos = p.pos + d * (std::min(l, 6.f * dt) / l);
                float g = coll_->groundBelow(p.pos + Vec3{0, 0, 3});
                if (g > -1e8f) p.pos.z = g + 1.f;
            }
        }
        if (playerAlive_) { Vec3 d = p.pos - playerPos_; if (touch(p, d) && (pickupClass_ = p.cls, collect(p.extra))) { in->hidden = true; if (p.extra == XHostage) rescued[p.cls]++; p.taken = p.extra; p.extra = -1; } }
            continue;
        }
        if (playerAlive_) { Vec3 d = p.pos - playerPos_; if (touch(p, d) && (pickupClass_ = p.cls, collect(p.extra))) { if (p.extra == XHostage) rescued[p.cls]++; p.extra = -1; continue; } }
        if (p.life > 0) {                                  // 0x38905: timed extras vanish, blinking for the last 300 ticks
            p.life -= dt;
            if (p.life <= 0) { p.extra = -1; continue; }
            if (p.life < 300.f / kTick && std::fmod(p.age, 0.2f) < 0.1f) continue;
        }
        if (p.model >= 0) extras.push_back({p.model, p.cls, p.pos + Vec3{0, 0, 0.6f * std::sin(p.age * 2.f)}, Mat4::rotZ(p.age * 1.5f)});
        else effects_.push_back({SpriteLib::SmallExplosion, p.pos, 0, 0.05f, 2.f});
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(), [](const Pickup& p) { return p.extra < 0 && p.world < 0; }), pickups_.end());
    auto segSphere = [](Vec3 a, Vec3 b, Vec3 c, float r, float& t) {
        Vec3 d = b - a, m = a - c;
        float dd = dot(d, d); if (dd < 1e-9f) return false;
        float tt = std::clamp(-dot(m, d) / dd, 0.f, 1.f);
        Vec3 q = a + d * tt - c;
        if (dot(q, q) > r * r) return false;
        t = tt; return true;
    };
    for (auto& s : shots_) {
        if (s.kind == Rocket && s.target >= 0) {
            const Target* tg = target(s.target);
            Renderer::Instance* in = renderer_->instanceForWorldObject(s.target);
            if (tg && tg->alive && in) {
                Vec3 want = normalize(in->pos - s.pos);
                float sp = std::sqrt(dot(s.vel, s.vel));
                Vec3 cur = s.vel * (1.f / std::max(sp, 1e-3f));
                Vec3 nd = normalize(cur + (want - cur) * std::min(1.f, 3.5f * dt));
                sp = std::min(sp + 120.f * dt, 340.f);
                s.vel = nd * sp;
            } else s.target = -1;
        }
        Vec3 next = s.pos + s.vel * dt;
        if ((s.kind == Rocket && s.target < 0) || s.kind == EnemyRocket) s.vel.z -= 4.f * dt;
        const bool enemy = s.kind == EnemyBullet || s.kind == EnemyRocket;
        Vec3 hit, n{0, 0, 1}; int owner = -1;
        bool hitWorld = coll_->raycast(s.pos, next, hit, owner, &n);
        float tWorld = 2.f;
        if (hitWorld) { Vec3 dd = next - s.pos; float l2 = dot(dd, dd); tWorld = l2 > 0 ? dot(hit - s.pos, dd) / l2 : 0; }
        if (enemy) {
            float tp;
            if (playerAlive_ && segSphere(s.pos, next, playerPos_, playerRadius_, tp) && tp < tWorld) {
                if (arms.immortal <= 0) playerDamage_ += s.damage;
                Vec3 at = s.pos + (next - s.pos) * tp;
                explode(at, s.kind == EnemyRocket ? 5.f : 2.5f, false);
                if (audio_) audio_->playAt(sHeliHit_, at, 1.f);
                s.life = -1; continue;
            }
            if (hitWorld) { explode(hit + n * 0.5f, s.kind == EnemyRocket ? 5.f : 2.f, false); s.life = -1; continue; }
        } else {
            // moving enemies are tested as spheres (their collision triangles are disabled)
            float best = tWorld; int dynHit = -1;
            for (size_t i = 0; i < targets_.size(); i++) {
                const Target& tg = targets_[i];
                if (!tg.dynamic || !tg.alive || !tg.active) continue;
                Renderer::Instance* in = renderer_->instanceForWorldObject(int(i));
                if (!in) continue;
                float tt;
                if (segSphere(s.pos, next, in->pos, std::max(2.f, tg.radius * 0.6f), tt) && tt < best) { best = tt; dynHit = int(i); }
            }
            if (dynHit >= 0) { hitWorld = true; owner = dynHit; hit = s.pos + (next - s.pos) * best; n = normalize(s.pos - hit); }
        }
        if (hitWorld) {
            if (s.kind == Bullet) {
                damage(owner, s.damage, hit);
                explode(hit + n * 0.5f, 2.5f, false);
                if (audio_ && hitSoundCooldown_ <= 0 && !(target(owner) && target(owner)->isTarget)) {
                    audio_->playAt(sSmall_, hit, 0.35f); hitSoundCooldown_ = 0.1f;
                }
            } else {
                damage(owner, s.damage, hit);
                const float splash = s.splash > 0 ? s.splash : kRocketSplash;
                for (size_t i = 0; i < targets_.size(); i++) {
                    if (int(i) == owner || !targets_[i].isTarget || !targets_[i].alive) continue;
                    Renderer::Instance* in = renderer_->instanceForWorldObject(int(i));
                    if (!in) continue;
                    Vec3 d = in->pos - hit;
                    float dist = std::sqrt(dot(d, d)) - targets_[i].radius * 0.5f;
                    if (dist < splash) damage(int(i), s.damage * 0.5f * (1.f - std::max(0.f, dist) / splash), in->pos);
                }
                effects_.push_back({SpriteLib::AirExplosion, hit + n * 2.f, 0, 0.8f, 9.f});
                if (audio_) audio_->playAt(sAir_, hit, 0.9f);
            }
            s.life = -1;
            continue;
        }
        s.pos = next;
        s.life -= dt;
        if (s.kind == Rocket || s.kind == EnemyRocket) {
            effects_.push_back({SpriteLib::Smoke, s.pos, 0, 0.9f, 1.6f});
            int m = s.kind == Rocket ? s.model : enemyRocketModel_, c = s.kind == Rocket ? s.cls : enemyRocketClass_;
            if (m >= 0) extras.push_back({m, c, s.pos, orientAlong(s.vel)});
        } else {
            int m = s.kind == Bullet ? s.model : enemyBulletModel_, c = s.kind == Bullet ? s.cls : enemyBulletClass_;
            if (m >= 0) extras.push_back({m, c, s.pos, orientAlong(s.vel)});
            else effects_.push_back({SpriteLib::SmallExplosion, s.pos, 0, 0.05f, 0.8f});
        }
    }
    shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Projectile& p) { return p.life <= 0; }), shots_.end());

    sprites.clear();
    for (auto& e : effects_) {
        e.age += dt;
        if (e.age < 0) continue;
        const SpriteAnim& a = sprites_->anim(SpriteLib::Kind(e.anim));
        if (a.frames == 0 || e.age >= e.duration) continue;
        int f = std::min(a.frames - 1, int(e.age / e.duration * float(a.frames)));
        float rise = e.anim == SpriteLib::Smoke ? e.age * 3.f : 0.f;
        sprites.push_back({e.pos + Vec3{0, 0, rise}, e.size * (e.anim == SpriteLib::Smoke ? 0.6f + e.age / e.duration : 1.f),
                           a.firstLayer + f, e.anim == SpriteLib::Smoke ? 0.8f : 1.f});
    }
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.age >= e.duration; }), effects_.end());
}

} // namespace xa
