#include "enemies.h"
#include "pose.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <cstdio>
#include <cstdlib>

namespace xa {

namespace {
const float kTick = 60.f;            // game ticks per second (RE: mission timer unit = 1/60 s)
const float kShotSpeedScale = 1.0f;  // DEF shoot_speed is units per tick
const float kTwoPi = 6.2831853f;
const float kPiF = 3.14159265f;

std::string base(const std::string& b) { auto d = b.find('.'); return d == std::string::npos ? b : b.substr(0, d); }

float wrapAngle(float a) { while (a > 3.14159265f) a -= kTwoPi; while (a < -3.14159265f) a += kTwoPi; return a; }
}

// Default ACTIVATIONAREA / SHOOTACTIVATIONAREA / PATHACTIVATIONAREA per behaviour (raw units), from the
// behaviour name table at 0x17CF50 (0x56-byte records, + 0x40 / 0x44 / 0x48); 0x50e1d copies them into
// every TYP that leaves the key out. Behaviours not listed default to 0 (never).
namespace {
struct AreaDefaults { float act = 0, shoot = 0, path = 0; };
AreaDefaults areaDefaults(const std::string& b) {
    static const std::map<std::string, AreaDefaults> t = {
        {"BIENEN_ENEMY", {225000, 225000, 0}}, {"SMALL_ROCKET_ENEMY", {750000, 750000, 0}},
        {"BIG_ROCKET_ENEMY", {1250000, 1250000, 0}}, {"M_TRAIL_ROCKET_ENEMY", {1250000, 1250000, 0}},
        {"TRAIL1_ROCKET_ENEMY", {1250000, 1250000, 0}}, {"TRAIL2_ROCKET_ENEMY", {1250000, 1250000, 0}},
        {"TRAIL3_ROCKET_ENEMY", {1250000, 1250000, 0}}, {"TRAIL4_ROCKET_ENEMY", {1250000, 1250000, 0}},
        {"MOVINGWALL", {445000, 345000, 800000}}, {"BIENENSTOCK_ENEMY", {500000, 300000, 800000}},
        {"KOLIBRI_ENEMY", {500000, 300000, 800000}}, {"WESPE_ENEMY", {500000, 300000, 800000}},
        {"MOSQUITO_ENEMY", {600000, 400000, 800000}}, {"DEATHSTORM_ENEMY", {600000, 300000, 800000}},
        {"TERROR_GOD_ENEMY", {750000, 300000, 800000}}, {"SLIDER_ENEMY", {400000, 300000, 800000}},
        {"VIPER_ENEMY", {445000, 345000, 800000}}, {"THUNDER_ENEMY", {400000, 300000, 800000}},
        {"EBOLA_ENEMY", {400000, 300000, 800000}}, {"IMPERATOR_ENEMY", {400000, 300000, 800000}},
        {"FLOH_ENEMY", {300000, 300000, 2000000}}, {"BANDIT_ENEMY", {300000, 300000, 800000}},
        {"KILLER_ENEMY", {300000, 300000, 800000}}, {"ESEL_ENEMY", {500000, 0, 800000}},
        {"ESEL_AS_ENEMY", {500000, 0, 800000}}, {"BLITZ_ENEMY", {450000, 350000, 800000}},
        {"PANTHER_ENEMY", {400000, 300000, 800000}}, {"BUGGY_ENEMY", {400000, 300000, 800000}},
        {"SPIDER_ENEMY", {450000, 350000, 800000}}, {"SPIDER360_ENEMY", {450000, 350000, 800000}},
        {"NERO_ENEMY", {400000, 300000, 800000}}, {"CAMEL_ENEMY", {400000, 300000, 800000}},
        {"SPITFIRE_ENEMY", {400000, 300000, 800000}}, {"BARRACUDA_ENEMY", {450000, 300000, 800000}},
        {"AMBUSH", {445000, 345000, 800000}}, {"TUNNELPATH", {445000, 345000, 800000}},
        {"DRILL", {445000, 345000, 800000}}, {"G_TURM", {445000, 345000, 800000}}, {"R_TURM", {445000, 345000, 800000}},
    };
    auto it = t.find(b);
    return it == t.end() ? AreaDefaults{} : it->second;
}
}

// The original's queries see the landscape of every area (no set check in 0xe108a / 0xe122c), so enemies
// keep walking where the player is not.
struct HiddenScapeScope {
    const CollisionWorld* c;
    explicit HiddenScapeScope(const CollisionWorld* cw) : c(cw) { if (c) c->includeHiddenScape(true); }
    ~HiddenScapeScope() { if (c) c->includeHiddenScape(false); }
};

void EnemySystem::init(const World& world, const TypeDB& types, const DefDB& defs, PathDB& paths,
                       Renderer& renderer, Combat& combat, CollisionWorld& coll) {
    renderer_ = &renderer; combat_ = &combat; coll_ = &coll; unitScale_ = renderer.unitScale; world_ = &world; types_ = &types;
    flash2Model_ = flash2Cls_ = faceModel_ = faceCls_ = -1;
    for (size_t k = 0; k < types.size(); k++) {                   // the level's FLASH2 / FINALFACE types (templates 0x1eb2e8 / 0x1eb328)
        const std::string& tb = types.get(k)->behavior;
        if (tb == "FLASH2" && flash2Cls_ < 0) { flash2Cls_ = int(k); flash2Model_ = combat.findModelForClass(int(k)); }
        if (tb == "FINALFACE" && faceCls_ < 0) { faceCls_ = int(k); faceModel_ = combat.findModelForClass(int(k)); }
    }
    // the templates are the world objects of these types (NOSCAPE set); their model is the one to draw
    for (size_t k = 0; k < world.objects().size(); k++) {
        const int oc = int(world.objects()[k].objClass);
        const Renderer::Instance* in = renderer.instanceForWorldObject(int(k));
        if (!in) continue;
        if (oc == flash2Cls_ && flash2Model_ < 0) flash2Model_ = in->model;
        if (oc == faceCls_ && faceModel_ < 0) faceModel_ = in->model;
    }
    if (std::getenv("XA_DEBUG") && (flash2Cls_ >= 0 || faceCls_ >= 0)) std::printf("PLUTO effects: FLASH2 class %d model %d, FINALFACE class %d model %d\n", flash2Cls_, flash2Model_, faceCls_, faceModel_);
    static const std::set<std::string> air = {"WESPE_ENEMY", "MOSQUITO_ENEMY", "KOLIBRI_ENEMY", "BIENEN_ENEMY",
        "DEATHSTORM_ENEMY", "BIENENSTOCK_ENEMY", "MARS_ENEMY", "TUNNELPATH", "FLOH_ENEMY", "BANDIT_ENEMY", "KILLER_ENEMY", "PLUTO_ENEMY"};
    static const std::set<std::string> ground = {"BLITZ_ENEMY", "BUGGY_ENEMY", "PANTHER_ENEMY", "ESEL_ENEMY", "ESEL_AS_ENEMY",
        "CAMEL_ENEMY", "SPITFIRE_ENEMY", "NERO_ENEMY", "SPIDER_ENEMY", "SPIDER360_ENEMY", "BARRACUDA_ENEMY",
        "AMBUSH", "MOVINGWALL", "BLOCK_ENEMY", "BLOCK_NO_GRND_ENEMY"};
    static const std::set<std::string> turrets = {"G_TURM", "R_TURM", "BUILDING_ENEMY", "ADLER_ENEMY", "IMPERATOR_ENEMY", "TERROR_GOD_ENEMY"};
    const auto& objs = world.objects();
    byWorld_.assign(objs.size(), -1);
    // model of a TYP by name: the model list's entry for that class, else the one its template object
    // (NOSCAPE set) uses - look-only types like WATER_BLITZ have no model entry of their own
    auto modelOfType = [&](const std::string& name) {
        for (size_t k = 0; k < types.size(); k++) {
            if (types.get(k)->name != name) continue;
            int m = combat.findModelForClass(int(k));
            if (m >= 0) return m;
            for (size_t j = 0; j < objs.size(); j++)
                if (int(objs[j].objClass) == int(k)) if (const Renderer::Instance* in = renderer.instanceForWorldObject(int(j))) return in->model;
            return -1;
        }
        return -1;
    };
    for (size_t i = 0; i < objs.size(); i++) {
        const ObjectType* t = types.get(objs[i].objClass);
        if (!t || isTemplateSet(objs[i].set)) continue;
        std::string b = base(t->behavior);
        for (auto& c : b) c = char(std::toupper((unsigned char)c));
        Enemy e;
        if (air.count(b)) e.cat = Air;
        else if (ground.count(b)) e.cat = Ground;
        else if (turrets.count(b)) e.cat = Static;
        else continue;
        const std::string& bh = t->behavior;
        e.wi = int(i);
        e.friendly = defs.get(bh, "adv_friend", 0) != 0;
        e.armed = !e.friendly && (defs.get(bh, "shoot_speed", 0) > 0 || defs.get(bh, "shoot_salven", 0) > 0 || b == "G_TURM");
        if (e.cat == Static && !e.armed) continue;
        e.home = {objs[i].pos[0] * unitScale_, objs[i].pos[1] * unitScale_, objs[i].pos[2] * unitScale_};
        e.baseRot = worldObjectRotation(objs[i].angle);
        e.homeYaw = -objs[i].angle[4] * kTwoPi / 4096.f;                 // drawn angle (see worldObjectRotation)
        e.spHomeA0 = float(objs[i].angle[2]); e.spHomeA1 = float(objs[i].angle[3]);   // spider tilt starts as placed
        e.speed = float(defs.get(bh, "speed", e.cat == Air ? 600 : 250)) * kTick * unitScale_;
        e.turn = float(defs.get(bh, "rotatespeed", 10)) * kTick * kTwoPi / 4096.f;
        const char* goodKey = b == "WESPE_ENEMY" ? "wespe_good_r" : b == "PANTHER_ENEMY" ? "panther_good_r" :
                              b == "BUGGY_ENEMY" ? "buggy_good_r" : "tank_good_r";
        e.goodR = float(defs.get(bh, goodKey, e.cat == Air ? 90000 : 50000)) * unitScale_;
        int act = defs.get(bh, "activation_r", -1);
        // OBJECT.TYP areas are stored * 1600 (RE 0x4fca0), i.e. 1.6 GL units per TYP unit
        const float kArea = 1.6f;
        // a missing area (-1) gets the behaviour's default from the name table (0x50e1d: record + 0x40 / 0x44 / 0x48,
        // raw units); a 0 default never holds (|d| < 0)
        const AreaDefaults ad = areaDefaults(b);
        e.activation = t->activation >= 0 ? float(t->activation) * kArea : act > 0 ? float(act) * unitScale_ : ad.act * unitScale_;
        if (e.activation > 3300.f) e.activation = 3300.f;
        e.actBox = t->activation >= 0 ? float(t->activation) * kArea : ad.act * unitScale_;
        e.pathActivation = t->pathActivation >= 0 ? float(t->pathActivation) * kArea : ad.path * unitScale_;
        e.shootRange = t->shootActivation >= 0 ? float(t->shootActivation) * kArea : ad.shoot * unitScale_;
        e.kind = b == "WESPE_ENEMY" ? KWespe : b == "MOSQUITO_ENEMY" ? KMosquito : b == "BLITZ_ENEMY" ? KBlitz : b == "PANTHER_ENEMY" ? KPanther : b == "BUGGY_ENEMY" ? KBuggy :
                 (b == "DEATHSTORM_ENEMY" || b == "BIENENSTOCK_ENEMY") ? KDeathstorm :
                 (b == "FLOH_ENEMY" || b == "BANDIT_ENEMY") ? KFloh : b == "KILLER_ENEMY" ? KKiller :
                 b == "AMBUSH" ? KAmbush : b == "TUNNELPATH" ? KTunnel : b == "MARS_ENEMY" ? KMars :
                 (b == "SPIDER_ENEMY" || b == "SPIDER360_ENEMY") ? KSpider :
                 b == "TERROR_GOD_ENEMY" ? KTerror : b == "IMPERATOR_ENEMY" ? KImperator :
                 (b == "ESEL_ENEMY" || b == "ESEL_AS_ENEMY" || b == "BARRACUDA_ENEMY") ? KVehicle : b == "PLUTO_ENEMY" ? KPluto :
                 (b == "MOVINGWALL" || b == "BLOCK_ENEMY" || b == "BLOCK_NO_GRND_ENEMY") ? KMover : KOther;
        e.radius = std::max(1.f, float(objs[i].radius) * unitScale_);
        e.rawSpeed = float(defs.get(bh, "speed", 0));
        e.rotSpeed = float(defs.get(bh, "rotatespeed", 0));
        e.accel = float(defs.get(bh, "accelerate", 300));
        e.brake = float(defs.get(bh, "brake", 500));
        e.otherActR = float(defs.get(bh, "other_activation_r", 0)) * unitScale_;
        e.minAlpha = float(defs.get(bh, "minalpha", -256));
        e.maxAlpha = float(defs.get(bh, "maxalpha", 768));
        if (e.kind == KMosquito) {
            e.glEndR = float(defs.get(bh, "gl_end_attack_r", 80000)) * unitScale_;
            e.glGoHigherT = float(defs.get(bh, "gl_go_higher_t", 50));
            e.glMinR = float(defs.get(bh, "gl_min_r", 15000)) * unitScale_;
            e.glNeigung = float(defs.get(bh, "gl_neigung_faktor", 16));
            e.glMaxWait = float(std::max(1, defs.get(bh, "gl_max_wait", 50)));
            e.glDirekt = float(defs.get(bh, "gl_attack_direkt", 0)) * unitScale_;
        }
        if (e.kind == KFloh || e.kind == KKiller) {
            e.bandit = b == "BANDIT_ENEMY";
            e.mineOfs = float(defs.get(bh, "mine_coll_ofset", 0)) * unitScale_;
            e.causeDmg = float(defs.get(bh, "cause_damage", 1000));
            e.armed = false;
        }
        if (e.kind == KAmbush || e.kind == KTunnel) {
            e.goOnGround = defs.get(bh, "go_on_ground", e.kind == KAmbush ? 1 : 0) != 0;
            e.cat = e.goOnGround ? Ground : Air;
            e.strictPath = defs.get(bh, "strict_path", 0) != 0;
            e.hiShootT = defs.get(bh, "hi_shoot_t", 400);
            e.hiWaitT = defs.get(bh, "hi_wait_t", 300);
            e.hiReactionT = defs.get(bh, "hi_reaction_t", 0);
            e.noWaitShoot = defs.get(bh, "hi_no_wait_shoot", 0) != 0;
            e.tuFreeGo = defs.get(bh, "tu_free_go", 0);
            e.tuShootT = defs.get(bh, "tu_shoot_t", 400);
            e.tuTurnBefore = float(defs.get(bh, "tu_turn_before_go", 200));
            e.tuTurnToEnemy = defs.get(bh, "tu_turn_to_enemy", 1) != 0;
            e.tuMinR = float(defs.get(bh, "tu_min_radius", 30000)) * unitScale_;
        }
        if (e.kind == KTerror) {
            e.tgRocketT = defs.get(bh, "terror_rocket_t", 600); e.tgTransT = defs.get(bh, "terror_trans_t", 200); e.tgNotTransT = defs.get(bh, "terror_not_trans_t", 800);
            e.startZ = e.home.z; e.tgBob = 200;
        }
        if (e.kind == KImperator) {
            beamDamage_ = float(defs.get("IMPERATOR_BEAM", "cause_damage", 0));
            e.imSmokeT = defs.get(bh, "imper_smoke_t", 50); e.imBeamT = defs.get(bh, "imper_beam_t", 150);
            e.imBeamEndT = defs.get(bh, "imper_beam_end_t", 300); e.imEndT = defs.get(bh, "imper_end_t", 400);
        }
        if (e.kind == KSpider) {
            // case 0x6d / 0x6e call 0x77534 with DEF speed / rotatespeed, cycle 100 and flags 0xb / 0xc; its
            // decisions match the vehicle routine 0x7635c (spider flag 8 = vehicle flag 2: turn away when hit)
            e.spFlags = b == "SPIDER_ENEMY" ? 0xb : 0xc;
            e.spFrameSpeed = float(defs.get(bh, "spider_frame_speed", 0));
            e.vSpeed = e.rawSpeed; e.vTurn = std::max(e.rotSpeed, 1.f); e.vCycle = 100; e.vFlags = 2; e.gTargetYaw = wrapAngle(e.homeYaw + kPiF);
        }
        if (e.kind == KPluto) e.flashDmg = float(defs.get("FLASH1", "cause_damage", 3));
        if (e.kind == KVehicle) {
            // dispatcher 0x88184 cases 0x68 / 0x69 / 0x72 call 0x7635c with fixed arguments
            // (speed, turn rate, decision cycle, flags); ENEMY.DEF speed/rotatespeed are not used.
            // ESEL and ESEL_AS never shoot; BARRACUDA fires two rockets per 1000 ticks (8b997).
            if (b == "BARRACUDA_ENEMY") {
                e.vSpeed = 350; e.vTurn = 12; e.vCycle = 15; e.vFlags = 9; e.vRockets = !e.friendly;
            }
            else { e.vSpeed = 175; e.vTurn = 6; e.vCycle = -1; e.vFlags = b == "ESEL_AS_ENEMY" ? 4 : 0; }
            e.armed = false; e.gTargetYaw = wrapAngle(e.homeYaw + kPiF);
            // WATER_TYP: the look in water (0x770c2), not for boats
            if (!(e.vFlags & 8) && !t->waterTyp.empty()) e.waterModel = modelOfType(t->waterTyp);
            if (Renderer::Instance* in = renderer.instanceForWorldObject(int(i))) e.landModel = in->model;
            if (std::getenv("XA_DEBUG")) std::printf("vehicle %d %s water typ '%s' model %d\n", int(i), t->name.c_str(), t->waterTyp.c_str(), e.waterModel);
        }
        if (e.kind >= KBlitz && e.kind <= KBuggy) {
            // 0x7abb4: in WATER they take their WATER_TYP look; without one water blocks them
            if (!t->waterTyp.empty()) e.waterModel = modelOfType(t->waterTyp);
            if (Renderer::Instance* in = renderer.instanceForWorldObject(int(i))) e.landModel = in->model;
            if (std::getenv("XA_DEBUG") && !t->waterTyp.empty() && e.waterModel < 0) std::printf("ground unit %d: no model for %s\n", int(i), t->waterTyp.c_str());
        }
        if (e.kind == KDeathstorm) {
            e.pGood = float(defs.get(bh, "tank_good_r", 100000)) * unitScale_;
            e.pEsc = float(defs.get(bh, "tank_escape_ofset", 100000)) * unitScale_;
        }
        if (e.kind == KBlitz) {
            e.pGood = float(defs.get(bh, "tank_good_r", 80000)) * unitScale_;
            e.pEsc = float(defs.get(bh, "tank_escape_ofset", 20000)) * unitScale_;
            e.pShootT = defs.get(bh, "tank_shoot_t", 300);
        } else if (e.kind == KPanther || e.kind == KMars) {
            e.pGood = float(defs.get(bh, "panther_good_r", 80000)) * unitScale_;
            e.pMax = float(defs.get(bh, "panther_max_r", 150000)) * unitScale_;
            e.pMin = float(defs.get(bh, "panther_min_r", 40000)) * unitScale_;
        } else if (e.kind == KBuggy) {
            e.pGood = float(defs.get(bh, "buggy_good_r", 70000)) * unitScale_;
            e.pEsc = float(defs.get(bh, "buggy_escape_ofset", 60000)) * unitScale_;
            e.pShootT = defs.get(bh, "buggy_shoot_t", 300);
            e.pEscapeT = defs.get(bh, "buggy_escape_t", 450);
            e.rocketT = defs.get(bh, "buggy_rocket_t", 500);
        }
        if (e.shootRange <= 0) e.armed = false;
        // WESPE parameters (game units / ticks)
        e.maxR = float(defs.get(bh, "wespe_max_r", 120000)) * unitScale_;
        e.minR = float(defs.get(bh, "wespe_min_r", 32000)) * unitScale_;
        e.deltaR = float(defs.get(bh, "wespe_delta_r", 16000)) * unitScale_;
        e.highOffset = float(defs.get(bh, "wespe_high_offset", 0)) * unitScale_;
        e.shootT = defs.get(bh, "wespe_shoot_t", 100);
        e.flyLeftT = defs.get(bh, "wespe_fly_left_t", 80);
        e.waitShootT = defs.get(bh, "wespe_wait_shoot_t", 60);
        e.hitReaction = defs.get(bh, "hit_reaction", 40);
        e.rotRate = float(defs.get(bh, "rotatespeed", 20));
        e.wDir = (i % 2) ? 1.f : -1.f;
        e.interval = defs.get(bh, "shoot_salven", 15);                 // ticks between shots
        e.shootNumber = defs.get(bh, "shoot_number", 0);               // shots per burst (0: no pause)
        e.notShootT = defs.get(bh, "not_shoot_t", 0);                  // pause after a burst (ticks)
        e.shootRandom = defs.get(bh, "shoot_random", 0);               // per mille per tick when not attacking
        e.turretRate = float(defs.get(bh, "turm_rotate_speed", 0));
        e.salvo = std::max(1, e.interval);
        e.salvoPause = float(defs.get(bh, "not_shoot_t", defs.get(bh, "wespe_shoot_t", defs.get(bh, "tank_shoot_t", 90)))) / kTick;
        e.shotSpeed = std::max(40.f, float(defs.get(bh, "shoot_speed", 900)) * kTick * unitScale_ * kShotSpeedScale);
        // shoot_typ (ENEM_PAR.DEF param 5, RE 0x8915a): 0 ENEMY_SHOOT_SMALL, 1 ENEMY_SHOOT, otherwise the default (ENEMY_SHOOT3);
        // rocket launchers are the behaviours with rocket timing parameters.
        {
            int st = defs.get(bh, "shoot_typ", 0);
            e.rockets = defs.get(bh, "rocket_min_range", -1) >= 0 || defs.get(bh, "buggy_rocket_t", -1) >= 0;   // TERROR_GOD rockets: terrorAI
            const char* shotName = e.rockets ? "SMALL_ROCKET_ENEMY" : st <= 0 ? "ENEMY_SHOOT_SMALL" : st == 1 ? "ENEMY_SHOOT" : "ENEMY_SHOOT3";
            e.shotDamage = float(defs.get(shotName, "cause_damage", e.rockets ? 1000 : st <= 0 ? 150 : 300));
            if (e.vRockets) e.shotDamage = float(defs.get("SMALL_ROCKET_ENEMY", "cause_damage", 1000));   // 0x1eae68
        }
        if (!t->path.empty()) e.path = paths.get(t->path);
        if (e.kind == KBuggy) { e.interval = e.rocketT; e.shootNumber = 0; e.rockets = true; e.armed = !e.friendly; }   // only rockets, every buggy_rocket_t
        for (const auto& part : t->parts) {
            std::string n = part.name; for (auto& c : n) c = char(std::tolower((unsigned char)c));
            if (n.rfind("turm", 0) == 0) e.hasTurret = true;
        }
        if (int ts = defs.get(bh, "turm_rotate_speed", 0); ts > 0) e.turretSpeed = float(ts) * kTick * kTwoPi / 4096.f;
        e.aimYaw = e.homeYaw;
        e.strafeDir = (i % 2) ? 1.f : -1.f;
        e.circle = float(i % 7) * 0.9f;
        e.hover = 12.f + float(i % 5) * 3.f;
        if (std::getenv("XA_DEBUG") && e.kind >= KMosquito) std::printf("RE AI %d %s kind %d act %.0f pathact %.0f path %s at %.0f %.0f\n", int(i), bh.c_str(), int(e.kind), e.actBox, e.pathActivation, t->path.c_str(), e.home.x, e.home.y);
        byWorld_[i] = int(enemies_.size());
        enemies_.push_back(e);
    }
    // PLUTO: the objects of its ACTIVATION_WHEN_DESTROYED TYP keep it awake (event 7, 0x59d1c), and the
    // CONNECT_TO_FLYING children ride on it (0x58cc4 stores their offset, 0x70b12 moves them). The
    // original attaches to the first object of the parent TYP; here each child takes the parent of
    // its own enemy set (else the nearest one), since L6 holds one mothership per set.
    {
        auto up = [](std::string s) { for (auto& c : s) c = char(std::toupper((unsigned char)c)); return s; };
        // every other unit with ACTIVATION_WHEN_DESTROYED <TYP> is dormant while objects of that TYP exist: the
        // event sets [0x84] = 1 each frame, which holds it like a waypoint wait and keeps it from firing (0x7c514).
        // "Exists" is decided by typePresent (main.cpp: the active lists, like IF DESTROYED).
        for (Enemy& u : enemies_) {
            const ObjectType* ut = types.get(objs[size_t(u.wi)].objClass);
            u.wakeType = ut ? up(ut->activateWhenDestroyed) : std::string();
            if (u.kind == KPluto || u.wakeType.empty()) continue;
            u.dormant = true;
            if (std::getenv("XA_DEBUG")) std::printf("dormant %d (%s) until %s destroyed\n", u.wi, ut->name.c_str(), u.wakeType.c_str());
        }
        for (Enemy& ship : enemies_) {
            if (ship.kind != KPluto) continue;
            const ObjectType* st = types.get(objs[size_t(ship.wi)].objClass);
            if (ship.path && ship.path->size() >= 2) { ship.pzA = (*ship.path)[0].pos[2] * unitScale_; ship.pzB = (*ship.path)[1].pos[2] * unitScale_; }
            else { ship.pzA = ship.pzB = ship.home.z; }
            std::string wake = st ? up(st->activateWhenDestroyed) : std::string();
            for (size_t j = 0; j < objs.size(); j++) {
                const ObjectType* ct = types.get(objs[j].objClass);
                if (!ct || isTemplateSet(objs[j].set) || wake.empty() || up(ct->name) != wake) continue;
                if (objs[j].set != objs[size_t(ship.wi)].set) continue;          // its own enemy set only
                ship.wakeBy.push_back(int(j));
            }
        }
        for (size_t j = 0; j < objs.size(); j++) {
            const ObjectType* ct = types.get(objs[j].objClass);
            if (!ct || ct->connectToFlying.empty() || isTemplateSet(objs[j].set)) continue;
            std::string parent = up(ct->connectToFlying);
            Vec3 cp{objs[j].pos[0] * unitScale_, objs[j].pos[1] * unitScale_, objs[j].pos[2] * unitScale_};
            Enemy* best = nullptr; float bestD = 1e30f;
            for (Enemy& ship : enemies_) {
                if (ship.kind != KPluto) continue;
                const ObjectType* st = types.get(objs[size_t(ship.wi)].objClass);
                if (!st || up(st->name) != parent) continue;
                Vec3 d = ship.home - cp;
                float score = dot(d, d) + (objs[size_t(ship.wi)].set == objs[j].set ? 0.f : 1e20f);
                if (score < bestD) { bestD = score; best = &ship; }
            }
            if (!best) continue;
            Attach a; a.wi = int(j); a.home = cp; a.baseRot = worldObjectRotation(objs[j].angle); a.baseYaw = -objs[j].angle[4] * kTwoPi / 4096.f;
            best->attached.push_back(a);
            if (byWorld_[j] >= 0) enemies_[size_t(byWorld_[j])].attachedTo = best->wi;
        }
        if (std::getenv("XA_DEBUG"))
            for (const Enemy& ship : enemies_) if (ship.kind == KPluto)
            {
                std::printf("PLUTO %d (%s) at %.0f %.0f %.0f path z %.0f..%.0f wake %zu attached %zu\n", ship.wi, objs[size_t(ship.wi)].set.c_str(), ship.home.x, ship.home.y, ship.home.z, ship.pzA, ship.pzB, ship.wakeBy.size(), ship.attached.size());
                for (int w : ship.wakeBy) std::printf("  wake %d (%s)\n", w, objs[size_t(w)].set.c_str());
                for (const Attach& a : ship.attached) std::printf("  attached %d enemy %d\n", a.wi, byWorld_[size_t(a.wi)] >= 0);
            }
    }
    // DEF "soundeffekt x.wav" (+ SOUNDEFFEKT_ONLY_WHEN_MOVING): looping unit sounds (0x4d219, played by 0x39757)
    loops_.clear();
    for (size_t i = 0; i < objs.size(); i++) {
        const ObjectType* t = types.get(objs[i].objClass);
        if (!t || isTemplateSet(objs[i].set)) continue;
        std::string snd = defs.str(t->behavior, "soundeffekt");
        if (snd.empty()) continue;
        Loop l; l.wi = int(i); l.name = snd; l.onlyMoving = defs.get(t->behavior, "soundeffekt_only_when_moving", 0) != 0;
        l.last = {objs[i].pos[0] * unitScale_, objs[i].pos[1] * unitScale_, objs[i].pos[2] * unitScale_};
        loops_.push_back(l);
    }
    for (Enemy& e : enemies_) {
        if (e.kind != KSpider) continue;
        Vec3 up = normalize(e.baseRot.transformPoint({0, 0, 1}));
        if (dot(up, up) < 0.5f) up = {0, 0, 1};
        e.spH = 0.4f; e.spHome = e.spUp = e.spUpT = up; e.spLastZ = e.home.z;
    }
    measureGround();
    for (const Enemy& e : enemies_) if (e.cat == Ground || e.kind == KSpider) coll.setOwnerEnabled(e.wi, true);
    // Turrets built into the rock (e.g. inside the hidden base, behind its door) have solid
    // terrain above them; they are left off the radar and the target lists.
    for (Enemy& e : enemies_) {
        if (e.cat != Static) continue;
        coll.setOwnerEnabled(e.wi, false);
        Vec3 hit; int owner = -1;
        e.enclosed = coll.raycast(e.home + Vec3{0, 0, 3}, e.home + Vec3{0, 0, 400}, hit, owner);
        coll.setOwnerEnabled(e.wi, true);
        if (e.enclosed && std::getenv("XA_DEBUG")) std::printf("enclosed turret %d at %.0f %.0f %.0f\n", e.wi, e.home.x, e.home.y, e.home.z);
    }
}

void EnemySystem::setActive(int wi, bool active) {
    if (wi < 0 || size_t(wi) >= byWorld_.size() || byWorld_[size_t(wi)] < 0) return;
    Enemy& e = enemies_[size_t(byWorld_[size_t(wi)])];
    e.active = active;
    if (active && e.kind >= KMosquito && std::getenv("XA_DEBUG")) std::printf("activate ground AI %d kind %d\n", wi, int(e.kind));
    if (e.kind == KMover && dot(e.pos - e.home, e.pos - e.home) > 0) coll_->moveOwner(wi, e.home - e.pos);
    // behaviour state of the reverse engineered units starts over
    e.gs = 0; e.gNext = 0; e.gTimer = 0; e.gVel = {}; e.gWant = {}; e.collided = false; e.pathStarted = false; e.pathDone = false;
    e.hitLatched = false; e.gInactive = true; e.alertT = 0; e.waitT = 0; e.hitAct = false; e.mState = 0; e.mInit = false;
    e.aState = 0; e.aIdx = 1; e.aTimer = 0; e.mvState = 0; e.mvIdx = 0; e.mvDir = 1; e.tgT = 0; e.tgRocket = 0; e.imC = 0; e.spSpeed = 0;
    e.pitch = 0;
    if (e.tNode >= 0 && e.path) { TunnelGraph& g = graphFor(e.path); if (size_t(e.tNode) < g.occ.size()) g.occ[size_t(e.tNode)] = 0; } e.tNode = -1;
    e.state = Idle; e.pos = e.home; e.prevPos = e.home; e.vel = {}; e.crashing = false; e.roll = 0;
    e.yaw = wrapAngle(e.homeYaw + faceFlip(e)); e.pathIdx = 0; e.pathEnd = false; e.salvoLeft = 0; e.burst = 0; e.pauseLeft = 0; e.intervalLeft = float(e.interval); e.ws = WNone; e.drift = {};
    e.shotTimer = e.salvoPause * 0.5f;
    if (e.kind == KVehicle || e.kind == KSpider) { e.gTargetYaw = e.yaw; e.rocketCnt = 0; e.bobPhase = 0; }
    if (e.kind == KSpider) { e.spAir = false; e.spUp = e.spUpT = e.spHome; e.spA0 = e.spHomeA0; e.spA1 = e.spHomeA1; e.spVel = {}; e.spLastZ = e.home.z; }
    if (e.kind == KVehicle || (e.kind >= KBlitz && e.kind <= KBuggy)) {
        e.spUp = e.spUpT = {0, 0, 1}; e.spA0 = e.spA1 = 0; e.spLastZ = e.home.z; e.inWater = false;
        if (Renderer::Instance* in = renderer_->instanceForWorldObject(wi); in && e.landModel >= 0) in->model = e.landModel;
    }
    if (e.kind != KPluto && !e.wakeType.empty()) e.dormant = true;               // re-checked on the next update
    if (e.kind == KPluto) { e.dState = 0; e.dT = 0; e.pVz = 0; e.pIdx = 0; e.beamT = -1; e.collInit = false; if (active) applyRigid(e); }
    if (e.moving) { coll_->setOwnerEnabled(wi, active); combat_->setDynamic(wi, false); e.moving = false; }
    if (Renderer::Instance* in = renderer_->instanceForWorldObject(wi)) { in->pos = e.home; in->rot = e.baseRot; }
}

bool EnemySystem::pathEndReached(int wi) const {
    if (wi < 0 || size_t(wi) >= byWorld_.size() || byWorld_[size_t(wi)] < 0) return false;
    return enemies_[size_t(byWorld_[size_t(wi)])].pathEnd;
}

int EnemySystem::activeCount() const {
    int n = 0;
    for (const auto& e : enemies_) if (e.active) { const auto* t = combat_->target(e.wi); if (t && t->alive) n++; }
    return n;
}

bool EnemySystem::nearest(Vec3 from, Vec3& out) const {
    float best = 1e30f; bool found = false;
    for (const auto& e : enemies_) {
        if (!e.active || hiddenFromPlayer(e)) continue;
        const auto* t = combat_->target(e.wi); if (!t || !t->alive) continue;
        Vec3 d = e.pos - from; float d2 = dot(d, d);
        if (d2 < best) { best = d2; out = e.pos; found = true; }
    }
    return found;
}

void EnemySystem::contacts(std::vector<std::pair<Vec3, Vec3>>& out, std::vector<int>* ids) const {
    out.clear();
    if (ids) ids->clear();
    for (const auto& e : enemies_) {
        if (!e.active || e.friendly || e.crashing || hiddenFromPlayer(e)) continue;
        const auto* t = combat_->target(e.wi);
        if (!t || !t->alive) continue;
        out.push_back({e.pos, e.vel});
        if (ids) ids->push_back(e.wi);
    }
}

void EnemySystem::friends(std::vector<Vec3>& out, std::vector<int>& ids) const {
    out.clear(); ids.clear();
    for (const auto& e : enemies_) {
        if (!e.active || !e.friendly || e.crashing || hiddenFromPlayer(e)) continue;
        const auto* t = combat_->target(e.wi);
        if (t && !t->alive) continue;
        out.push_back(e.pos); ids.push_back(e.wi);
    }
}

bool EnemySystem::startCrash(int wi) {
    if (wi < 0 || size_t(wi) >= byWorld_.size() || byWorld_[size_t(wi)] < 0) return false;
    Enemy& e = enemies_[size_t(byWorld_[size_t(wi)])];
    if (e.kind == KPluto) {                                   // 8947b: the long death of the mothership
        if (!e.active || e.dState != 0) return false;
        e.dState = 1; e.dT = 450; e.dVz = 0; e.beamT = -1;
        if (std::getenv("XA_DEBUG")) std::printf("PLUTO %d destroyed at z %.0f\n", e.wi, e.pos.z);
        return true;
    }
    if (e.cat != Air || !e.active) return false;
    e.crashing = true;
    e.spin = (e.wi % 2 ? 1.f : -1.f) * (2.5f + float(e.wi % 3));
    e.vel.z = std::max(e.vel.z, 4.f);       // short hop from the blast
    e.smokeT = 0;
    e.anim = renderer_->currentTicks; e.animRate = 1;     // own clock: the rotor winds down during the fall
    if (std::getenv("XA_DEBUG")) std::printf("crash start %d at %.1f %.1f %.1f\n", wi, e.pos.x, e.pos.y, e.pos.z);
    return true;
}

// ---------------------------------------------------------------------------------------------
// WESPE_ENEMY attack behaviour, reverse engineered from 0x7d970 (states are the original bit values):
//   0x80  approach  fly at the player (speed*3) until closer than the target distance (starts at max_r)
//   0x400 climb     to the player's height + wespe_high_offset (speed*1), max 300 ticks
//   0x800 hover     face the player for wespe_wait_shoot_t ticks
//   0x100 aim       face the player and shoot for wespe_shoot_t ticks; then strafe (first time)
//                   or approach again with target distance = distance - delta_r in [good_r, max_r]
//   0x1000 strafe   fly sideways around the player for wespe_fly_left_t ticks, alternating sides
//   0x200 retreat   closer than wespe_min_r or hit (hit_reaction %): back off facing the player (speed*-2)
//                   until distance + delta_r is reached
void EnemySystem::wespeThink(Enemy& e, Vec3 player, float dist, float dt) {
    const float ticks = dt * kTick;
    if (e.ws == WNone) { e.ws = WApproach; e.wTimer = 3000; e.wTarget = e.maxR; e.wCount = 0; }
    e.wTimer -= ticks;
    const float dz = (e.pos.z) - (player.z + e.highOffset);
    switch (e.ws) {
    case WApproach:
        if (dist < e.wTarget) { e.ws = WClimb; e.wTimer = 300; e.wCount = 1; }
        else if (e.wTimer < 0) { e.wTimer = 3000; }
        break;
    case WClimb:
        if (std::fabs(dz) < 5.f || e.wTimer < 0) { e.ws = WHover; e.wTimer = float(e.waitShootT); }
        break;
    case WHover:
        if (e.wTimer < 0) { e.ws = WAim; e.wTimer = float(e.shootT); }
        break;
    case WAim:
        if (e.wTimer < 0) {
            if (e.wCount < 2) { e.ws = WStrafe; e.wDir = -e.wDir; e.wTimer = float(e.flyLeftT); e.wCount++; }
            else { e.ws = WApproach; e.wTimer = 3000; e.wTarget = std::min(std::max(dist - e.deltaR, e.goodR), e.maxR); }
        }
        break;
    case WStrafe:
        if (e.wTimer < 0) { e.ws = WHover; e.wTimer = float(e.waitShootT); e.wCount++; }
        break;
    case WRetreat:
        if (dist > e.wTarget || e.wTimer < 0) { e.ws = WApproach; e.wTimer = 3000; e.wTarget = std::min(dist, e.maxR); }
        break;
    default: break;
    }
    // too close: back off
    if (dist < e.minR && e.ws != WRetreat) { e.ws = WRetreat; e.wTimer = 300; e.wTarget = std::min(dist + e.deltaR, e.maxR); }
    // hit reaction: escape with hit_reaction % chance
    if (e.wasHit) {
        e.wasHit = false;
        if (int(nextRnd() % 100) < e.hitReaction && e.ws != WRetreat) {
            e.ws = WRetreat; e.wTimer = 300; e.wTarget = std::min(dist + e.deltaR, e.maxR);
        }
    }
}

void EnemySystem::wespeMove(Enemy& e, Vec3 player, float dt) {
    Vec3 toP = player - e.pos;
    Vec3 flat{toP.x, toP.y, 0}; float fl = std::sqrt(dot(flat, flat));
    Vec3 dirP = fl > 1e-3f ? flat * (1.f / fl) : Vec3{0, 1, 0};
    Vec3 side{-dirP.y, dirP.x, 0};
    const float sp = e.speed;                  // GL units / s
    Vec3 want{0, 0, 0};
    float wantZ = player.z + e.highOffset;
    switch (e.ws) {
    case WApproach: { Vec3 d = player + Vec3{0, 0, e.highOffset} - e.pos; float l = std::sqrt(dot(d, d)); if (l > 1e-3f) want = d * (sp * 3.f / l); break; }
    case WClimb: want = {0, 0, std::clamp((wantZ - e.pos.z) * 2.f, -sp, sp)}; break;
    case WStrafe: want = side * (sp * e.wDir); want.z = std::clamp((wantZ - e.pos.z), -sp * 0.5f, sp * 0.5f); break;
    case WRetreat: want = dirP * (-2.f * sp); want.z = std::clamp((wantZ - e.pos.z), -sp * 0.5f, sp * 0.5f); break;
    default: break;                           // hover / aim: stand still
    }
    // velocity approaches the wanted one with limited acceleration (0x7bb0c)
    Vec3 dv = want - e.drift; float dl = std::sqrt(dot(dv, dv)); float maxA = sp * 2.f * dt;
    if (dl > maxA && dl > 0) dv = dv * (maxA / dl);
    e.drift = e.drift + dv;
    e.pos = e.pos + e.drift * dt;
    // keep above the ground
    float g = coll_->groundBelow(e.pos + Vec3{0, 0, 200});
    if (g > -1e8f && e.pos.z < g + 6.f) { e.pos.z = g + 6.f; if (e.drift.z < 0) e.drift.z = 0; }
    // nose on the player (approach: towards the flight direction, which is the same)
    float wantYaw = std::atan2(-toP.x, toP.y);
    float k = std::min(1.f, std::clamp(e.rotRate * dt * kTick, 2.f, 480.f) / 512.f);
    e.yaw += wrapAngle(wantYaw - e.yaw) * std::max(k, std::min(1.f, dt * 2.f));
}

// ---------------------------------------------------------------------------------------------
// Ground vehicles: BLITZ_ENEMY (0x7f99c), PANTHER_ENEMY (0x818bc), BUGGY_ENEMY (0x82950).
// Angles are in the original 4096-per-turn units, speeds in raw units per tick, distances in
// GL units (raw / 1000). Distance tests against areas are axis boxes like in the original.
namespace {
const float kU = 6.2831853f / 4096.f;                       // one original angle unit in radians
float yawOf(Vec3 d) { return std::atan2(-d.x, d.y); }
bool inBox2(Vec3 a, Vec3 b, float r) { return std::fabs(a.x - b.x) < r && std::fabs(a.y - b.y) < r; }
// 0x7d448: per frame, cur approaches want by clamp(diff, +-16r) * r / 512 with r = clamp(rotatespeed *
// ticks, 2, 480). The diff clamp makes the turn speed depend on the frame length; it is evaluated for
// the original's frame of kOrigFrame ticks and scaled to the real frame.
const float kOrigFrame = 2.f;
float turnToward(float cur, float want, float rot, float ticks) {
    float r = std::clamp(rot * kOrigFrame, 2.f, 480.f);
    float d = wrapAngle(want - cur) / kU;
    float step = std::clamp(d, -16.f * r, 16.f * r) * r / 512.f * (ticks / kOrigFrame);
    if (std::fabs(step) > std::fabs(d)) step = d;
    return wrapAngle(cur + step * kU);
}
// 0x7bb0c: each axis approaches the wanted velocity by at most accelerate (speeding up) or
// brake (slowing down / reversing) * ticks / 60
float approachAxis(float v, float want, float acc, float brk, float ticks) {
    float d = want - v;
    float lim = (v * d < 0 ? brk : acc) * ticks / 60.f;
    return v + std::clamp(d, -lim, lim);
}
}

// 0x7f6b4: is the line from the unit along yaw+angle (length factor * 4096 raw units) blocked?
bool EnemySystem::probe(const Enemy& e, float angleUnits, float factor) const {
    float y = e.yaw + angleUnits * kU;
    Vec3 dir{-std::sin(y), std::cos(y), 0};
    Vec3 a = e.pos + Vec3{0, 0, 3.f};
    Vec3 b = a + dir * (factor * 4096.f * unitScale_);
    Vec3 hit; int owner = -1;
    return coll_->raycast(a, b, hit, owner);
}

// 0x7a860: a hit wakes the unit for 350 ticks and alerts the idle units around it that have the
// hit unit inside their other_activation_r box
void EnemySystem::groundAlert(const Enemy& hit) {
    for (Enemy& f : enemies_) {
        if (&f == &hit || !f.active || f.kind < KBlitz || !f.gInactive || f.otherActR <= 0) continue;
        Vec3 d = f.pos - hit.pos;
        if (std::fabs(d.x) < f.otherActR && std::fabs(d.y) < f.otherActR && std::fabs(d.z) < f.otherActR) {
            f.alertT = 350;
            if (!f.hitLatched) { f.gs = 0; f.hitLatched = true; }
        }
    }
}

// Path part shared by the three behaviours. Returns 0: no path movement (attack logic runs),
// 1: drive to wp, 2: stand still this frame.
bool EnemySystem::groundPath(Enemy& e, Vec3 player, float ticks, float& speed, float& rot, Vec3& wp) {
    (void)ticks;
    if (!e.path || e.path->empty() || e.pathDone) return false;
    const auto& P = *e.path;
    if (!e.pathStarted) {
        Vec3 first{P[0].pos[0] * unitScale_, P[0].pos[1] * unitScale_, 0};
        if (inBox2(first, player, e.pathActivation)) e.pathStarted = true;
        else if (!e.hitAct) return false;
    }
    if (e.pathIdx >= P.size()) {
        if (P.back().flag & 8) e.pathIdx = 0;                                // loop
        else { e.pathDone = true; e.pathEnd = true; return false; }
    }
    const PathPoint& pp = P[e.pathIdx];
    if ((pp.flag & 0x20) && inBox2(e.pos, player, e.actBox) && pp.time == 0) return false;   // player near: fight
    rot *= 2;
    e.noShootPoint = (pp.flag & 4) != 0;
    if (pp.flag & 1) speed /= 2;
    if (pp.flag & 0x10) speed = float(pp.speed);
    if (pp.flag & 2) speed *= 2;
    wp = {pp.pos[0] * unitScale_, pp.pos[1] * unitScale_, pp.pos[2] * unitScale_};
    if (std::fabs(e.pos.x - wp.x) < 2 * e.radius && std::fabs(e.pos.y - wp.y) < 2 * e.radius) {
        e.pathIdx++;
        if (pp.time > 0) { e.waitT = float(pp.time); }
    }
    return true;
}

void EnemySystem::groundAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    if (e.alertT > 0) e.alertT -= ticks;
    float speed = e.rawSpeed, rot = e.rotSpeed;
    bool hit = e.wasHit; e.wasHit = false;
    if (hit) {
        e.alertT = 350;
        if (!e.hitLatched) { e.gs = 0; e.hitLatched = true; }
        groundAlert(e);
    }
    Vec3 fwd{-std::sin(e.yaw), std::cos(e.yaw), 0};
    bool onPath = false;
    if (e.waitT > 0) {                                     // waiting at a waypoint: no movement at all
        e.waitT -= ticks;
        if (e.waitT <= 0) e.waitT = 0;
        e.gVel = {};
        return;
    }
    Vec3 wp;
    // a path that has not been started yet keeps the unit where it is
    if (e.path && !e.path->empty() && !e.pathDone && !e.pathStarted) {
        Vec3 first{(*e.path)[0].pos[0] * unitScale_, (*e.path)[0].pos[1] * unitScale_, 0};
        if (!inBox2(first, player, e.pathActivation) && !e.hitAct) { e.gVel = {}; return; }
    }
    if (groundPath(e, player, ticks, speed, rot, wp)) {
        onPath = true;
        e.state = OnPath;
        if (e.waitT > 0) { e.gVel = {}; return; }
        if (!e.collided) e.yaw = turnToward(e.yaw, yawOf(wp - e.pos), rot, ticks);
        else { e.yaw = wrapAngle(e.yaw + rot * ticks * kU); e.collided = false; }
        fwd = {-std::sin(e.yaw), std::cos(e.yaw), 0};
        e.gVel = fwd * speed;
    } else {
        e.noShootPoint = false;
        bool woke = (playerAlive && inBox2(e.pos, player, e.actBox)) || e.alertT > 0;
        if (woke) { if (e.gs == 0) { e.gs = 8; e.gDist = e.kind == KPanther ? e.pGood : e.pGood; e.gInactive = false; } }
        else { e.gs = 0; e.gInactive = true; e.hitLatched = false; }
        if (e.gs == 0) e.gVel = {};
        e.state = e.gInactive ? Idle : Attack;
        if (!e.gInactive) {
            Vec3 d = player - e.pos; d.z = 0;
            const float dist = std::sqrt(dot(d, d));
            Vec3 steerDir = d;                       // direction the steering heads for
            bool steer = false, useTargetYaw = false;
            e.gWant = e.gVel;
            auto faceP = [&]() { e.yaw = turnToward(e.yaw, yawOf(d), rot, ticks); fwd = {-std::sin(e.yaw), std::cos(e.yaw), 0}; };
            auto randYaw = [&]() { e.gTargetYaw = wrapAngle(e.yaw + (float(int(nextRnd() % 2000)) - 1000.f) * kU); };
            if (e.kind == KBlitz) {
                if (hit && int(nextRnd() % 100) < e.hitReaction) { e.gTimer = 1000; e.gs = 0x10; e.gDist = e.pEsc + dist; }
                if ((e.gs & 8) && dist < e.gDist) { e.gs = 2; e.gTimer = float(e.pShootT); }
                if (e.gs & 2) {
                    if (e.gTimer < 0) { e.gTimer = 1000; e.gs = 0x10; e.gDist = e.pGood + e.pEsc; }
                    else { e.gWant = {}; e.gTimer -= ticks; }
                }
                if (e.gs & 0x10) {
                    if (e.gDist < dist || e.gTimer < 0) { e.gs = 8; e.gDist = e.pGood; }
                    else {
                        e.gTimer -= ticks; steerDir = d * -1.f;               // turn round and drive away
                        if (e.collided) { e.collided = false; e.gs = 2; e.gTimer = float(e.pShootT); }
                    }
                }
                steer = (e.gs & 0x18) != 0;
            } else if (e.kind == KPanther) {
                if ((e.gs & 8) && dist < e.gDist) e.gs = 2;
                if (e.gs & 0x10) {                                           // back off facing the player
                    if (e.gDist < dist) e.gs = 2;
                    else { faceP(); e.gWant = fwd * -speed; }
                }
                if (e.gs & 2) {
                    if (dist > e.pMax) { e.gs = 8; e.gDist = e.pGood; }
                    else if (dist < e.pMin) { e.gs = 0x10; e.gDist = e.pGood; }
                    else {
                        e.gWant = {};
                        if (e.cantAim) { e.gs = (nextRnd() % 128) < 64 ? 0x80 : 0x100; e.gTimer = 200; }   // gun cannot reach: move
                    }
                }
                if (e.gs & 0x180) {
                    if (e.gTimer < 0) e.gs = 2;
                    else { e.gTimer -= ticks; faceP(); e.gWant = fwd * ((e.gs & 0x80) ? speed : -speed); }
                }
                steer = (e.gs & 8) != 0;
            } else {                                                          // BUGGY
                if (hit && int(nextRnd() % 100) < e.hitReaction) {
                    if (!(e.gs & 2)) { e.gs = 0x40; e.gTimer = float(e.pEscapeT); randYaw(); }
                    else { e.gs = 0x20; e.gNext = 0x40; e.gTimer = 1400; }
                }
                if ((e.gs & 8) && dist < e.gDist) { e.gs = 2; e.gNext = 0x20; e.gTimer = float(e.pShootT); }
                if (e.gs & 2) {
                    if (e.gTimer < 0) { e.gs = 0x20; e.gTimer = 1400; e.gNext = 0x10; e.gDist += e.pEsc; }
                    else { e.gWant = {}; e.gTimer -= ticks; }
                }
                if (e.gs & 0x20) {                                           // reverse while turning round
                    if (e.gTimer < 0) {
                        e.gs = e.gNext;
                        if (e.gs & 0x40) { e.gTimer = float(e.pEscapeT); randYaw(); }
                        if (e.gs & 0x10) e.gTimer = 1000;
                    } else {
                        e.gWant = fwd * -speed;
                        e.yaw = wrapAngle(e.yaw - rot * ticks * kU);
                        e.gTimer -= rot * ticks;
                    }
                }
                if (e.gs & 0x40) {                                           // zig-zag escape after a hit
                    if (e.gTimer < 0 || e.collided) { e.gs = 0x80; e.gTimer = 150; }
                    else { e.gTimer -= ticks; if (float(nextRnd() % 16384) < ticks * 128.f) randYaw(); }
                }
                if (e.gs & 0x80) {                                           // creep towards the player
                    if (e.gTimer < 0) { e.gNext = 0x20; e.gs = 2; e.gTimer = float(e.pShootT); }
                    else { e.gTimer -= ticks; e.gWant = fwd * (speed / 4); faceP(); }
                }
                if (e.gs & 0x10) {
                    if (e.gDist < dist || e.gTimer < 0) { e.gs = 8; e.gDist = e.pGood; }
                    else { e.gTimer -= ticks; steerDir = d * -1.f; }
                }
                steer = (e.gs & 0x58) != 0;
                useTargetYaw = (e.gs & 0x40) != 0;
            }
            if (steer) {
                // 0x7f8b0: obstacle feelers ahead (0, +-280) and to the sides (+-500)
                const bool front = probe(e, 0, 7);
                const bool l9d = front && probe(e, -280, 9), r9e = front && probe(e, 280, 11);
                const bool l9f = probe(e, -500, 4), ra0 = probe(e, 500, 4);
                fwd = {-std::sin(e.yaw), std::cos(e.yaw), 0};
                e.gWant = fwd * speed;
                auto div = [&](int c) { if (e.gTurnCase == c) e.gTurnDiv *= 0.5f; else { e.gTurnDiv = 32; e.gTurnCase = c; } if (e.gTurnDiv < 1) e.gTurnDiv = 1; };
                if (!front && !e.collided) {
                    e.gRandDir = (nextRnd() % 128) < 64 ? 1 : -1;
                    if (!l9f && !ra0) {
                        e.gTurnCase = 0;
                        e.yaw = turnToward(e.yaw, useTargetYaw ? e.gTargetYaw : yawOf(steerDir), rot, ticks);
                    } else {
                        if (l9f) { div(1); e.yaw = wrapAngle(e.yaw + rot * ticks / e.gTurnDiv * kU); }
                        if (ra0) { div(2); e.yaw = wrapAngle(e.yaw - rot * ticks / e.gTurnDiv * kU); }
                    }
                } else {
                    e.gWant = e.gWant * 0.5f;
                    if (!e.collided) {
                        if (!l9d) {
                            div(3); e.yaw = wrapAngle(e.yaw - rot * ticks / e.gTurnDiv * kU);
                            if (!r9e && float(nextRnd() % 1024) < ticks) e.gRandDir = (nextRnd() % 128) < 64 ? 1 : -1;
                        } else if (!r9e) { div(4); e.yaw = wrapAngle(e.yaw + rot * ticks / e.gTurnDiv * kU); }
                        else { div(5); e.yaw = wrapAngle(e.yaw + float(e.gRandDir) * rot * ticks / e.gTurnDiv * kU); }
                    } else { e.yaw = wrapAngle(e.yaw + float(e.gRandDir) * rot * ticks * kU); e.collided = false; }
                }
            }
            e.gVel.x = approachAxis(e.gVel.x, e.gWant.x, e.accel, e.brake, ticks);
            e.gVel.y = approachAxis(e.gVel.y, e.gWant.y, e.accel, e.brake, ticks);
        }
    }
    (void)onPath;
    // move; a blocked move is undone (0x7afb8) and remembered for the next frame's steering
    Vec3 step = e.gVel * (ticks * unitScale_);
    if (dot(step, step) < 1e-10f) return;
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    Vec3 next = e.pos + step;
    Vec3 dir = normalize(Vec3{step.x, step.y, 0});
    Vec3 a = e.pos + Vec3{0, 0, 3.f}, b = next + Vec3{0, 0, 3.f} + dir * (e.radius * 0.7f);
    Vec3 hp; int owner = -1;
    if (coll_->raycast(a, b, hp, owner)) { e.collided = true; return; }
    // 0x7abb4: touching WATER - a unit with a WATER_TYP look switches to it (and drives on the river bed),
    // one without is blocked like by an object (0x807e1)
    {
        float w = coll_->surfaceBelow(next + Vec3{0, 0, e.radius}, CollisionWorld::KWater, nullptr);
        float g = coll_->surfaceBelow(next + Vec3{0, 0, 5.f}, CollisionWorld::KGround, nullptr);
        bool wet = w > -1e8f && w >= (g > -1e8f ? g : next.z - e.groundOffset);
        if (wet && e.waterModel < 0) {
            if (std::getenv("XA_DEBUG") && !e.collided) std::printf("ground unit %d kind %d stops at water %.0f %.0f z %.1f (water %.1f ground %.1f any %.1f)\n", e.wi, int(e.kind), next.x, next.y, next.z, w, g, coll_->groundBelow(next + Vec3{0, 0, 5.f}));
            e.collided = true; return;
        }
        if (wet != e.inWater && std::getenv("XA_DEBUG")) std::printf("ground unit %d kind %d %s water at %.0f %.0f\n", e.wi, int(e.kind), wet ? "enters" : "leaves", next.x, next.y);
        e.inWater = wet;
    }
    e.pos = next;
}

// ---------------------------------------------------------------------------------------------
// MOSQUITO_ENEMY (0x7ec44): a glider that always flies forward at 'speed' along its nose; only
// the target angles change. First it flies its PATH; the last path point is its home.
//   2    loiter: turn back to the home point when farther than gl_min_r
//   4    player in the ACTIVATIONAREA box: wait rand(gl_max_wait) ticks, then
//   0x10 attack run at the player (fires only here) until closer than gl_end_attack_r (2D), then
//   0x20 pull up (pitch 150) and turn for gl_go_higher_t ticks, then
//   0x40 fly back home (zig-zag for 100 ticks after a hit) until within gl_min_r / 40 units -> 2
//   0x80 player within gl_attack_direkt of the home point: ram the player (1500 damage, unit destroyed)
// Yaw / roll / pitch follow their targets via 0x7d4d8 at rotatespeed; the roll target is
// gl_neigung_faktor * remaining yaw error per tick. Velocity approaches the nose direction * speed
// with accelerate / brake (0x7bb0c).
void EnemySystem::mosquitoAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    const float rot = e.rotSpeed, speed = e.rawSpeed;
    const auto* P = e.path && !e.path->empty() ? e.path : nullptr;
    if (!e.mInit) {
        e.mInit = true;
        e.pathIdx = (P && P->size() >= 2) ? 0 : 1;
        e.mYawT = e.yaw; e.mPitchT = 0; e.mRollT = 0; e.pitch = 0; e.roll = 0; e.gVel = {};
        e.mState = 0;
        e.mHome = P ? Vec3{P->back().pos[0] * unitScale_, P->back().pos[1] * unitScale_, P->back().pos[2] * unitScale_} : e.home;
    }
    auto aimAt = [&](Vec3 to) {               // 0x7d568: target yaw / pitch towards a point
        Vec3 d = to - e.pos;
        e.mYawT = yawOf(d);
        float p = std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y));
        e.mPitchT = std::clamp(p, -1023.f * kU, 1023.f * kU);
        e.mRollT = 0;
    };
    auto dist2 = [](Vec3 a, Vec3 b) { float x = a.x - b.x, y = a.y - b.y; return std::sqrt(x * x + y * y); };
    bool hit = e.wasHit; e.wasHit = false;
    bool ram = false;
    e.mOnPath = P && e.pathIdx < P->size();
    if (e.mOnPath) {
        const PathPoint& pp = (*P)[e.pathIdx];
        Vec3 wp{pp.pos[0] * unitScale_, pp.pos[1] * unitScale_, pp.pos[2] * unitScale_};
        aimAt(wp);
        Vec3 d = e.pos - wp;
        if (std::fabs(d.x) < 2 * e.radius && std::fabs(d.y) < 2 * e.radius && std::fabs(d.z) < 2 * e.radius) e.pathIdx++;
        e.state = OnPath;
        if (e.pathIdx >= P->size()) e.pathEnd = true;
    } else {
        e.state = Attack;
        if (e.mState == 0) e.mState = 2;
        if (hit && !e.hitLatched) { e.hitLatched = true; if (e.mState != 2) { e.mTimer = 0; e.mState = 0x40; } }
        const bool inBox = playerAlive && inBox2(e.pos, player, e.actBox);
        if (inBox) {
            if (e.mState == 2) {
                e.mState = 4;
                e.mDir = (nextRnd() % 128) < 64 ? 1 : -1;
                e.mTimer = float(int(nextRnd()) % int(e.glMaxWait));
                e.gInactive = false;
            }
        } else {
            if (e.mState != 2) { e.mTimer = 0; e.mState = 0x40; }
            e.gInactive = true; e.hitLatched = false;
        }
        if (playerAlive && e.glDirekt > 0 && dist2(player, e.mHome) < e.glDirekt) { if (!e.gInactive) e.mState = 0x80; }
        else if (e.mState == 0x80) { e.mState = 0x40; e.mTimer = 0; }
        if (e.mState == 0x80) {
            // straight at the player; the nose itself turns fast (0x73a10, rate 16 per tick)
            aimAt(player);
            float k = std::min(1.f, std::clamp(16.f * kOrigFrame, 2.f, 480.f) / 512.f * (ticks / kOrigFrame));
            e.yaw = wrapAngle(e.yaw + wrapAngle(e.mYawT - e.yaw) * k);
            e.pitch += (e.mPitchT - e.pitch) * k;
            Vec3 d = player - e.pos;
            if (dot(d, d) < std::pow(e.radius + 4.f, 2.f)) ram = true;
        }
        if (e.mState == 2 || e.mState == 4) {
            if (e.mState == 4) { if (e.mTimer < 0) e.mState = 8; else e.mTimer -= ticks; }
            if (dist2(e.pos, e.mHome) > e.glMinR) aimAt(e.mHome);
        }
        if (e.mState == 8) e.mState = 0x10;
        if (e.mState == 0x10) {
            if (dist2(e.pos, player) < e.glEndR) { e.mState = 0x20; e.mPitchT = 150.f * kU; e.mTimer = e.glGoHigherT; }
            else aimAt(player);
        }
        if (e.mState == 0x20) {
            if (e.mTimer < 0) { e.mState = 0x40; e.mTimer = 0; }
            else { e.mTimer -= ticks; e.mYawT = wrapAngle(e.mYawT + float(e.mDir) * rot * ticks * kU); }
        }
        if (e.mState == 0x40) {
            float hd = dist2(e.pos, e.mHome);
            if (hit) { e.mDir = (nextRnd() % 128) < 64 ? 1 : -1; e.mTimer = 100; }
            if (e.mTimer > 0) {
                if (e.mTimer < 50) { e.mYawT = wrapAngle(e.mYawT + float(e.mDir) * rot * ticks * kU); e.mTimer -= ticks; if (e.mTimer < 0) e.mTimer = 0; }
                else { e.mYawT = wrapAngle(e.mYawT - float(e.mDir) * rot * ticks * kU); e.mTimer -= ticks; }
            }
            if (hd < e.glMinR || hd < 40.f) e.mState = 2;
            else if (e.mTimer == 0) aimAt(e.mHome);
        }
    }
    if (ram) {
        if (std::getenv("XA_DEBUG")) std::printf("mosquito %d rams the player\n", e.wi);
        combat_->ramPlayer(e.wi, 1500.f, e.pos);
        return;
    }
    // 0x7d4d8 smoothing of yaw, then roll (banking into the turn), then pitch
    if (e.mState != 0x80 || e.mOnPath) e.yaw = turnToward(e.yaw, e.mYawT, rot, ticks);
    float err = wrapAngle(e.mYawT - e.yaw) / kU;
    e.mRollT = -std::clamp(e.glNeigung * err / kOrigFrame, -768.f, 768.f) * kU;   // left turn: left wing down
    e.roll = turnToward(e.roll, e.mRollT, rot, ticks);
    if (e.mState != 0x80 || e.mOnPath) e.pitch = turnToward(e.pitch, e.mPitchT, rot, ticks);
    Vec3 nose{-std::sin(e.yaw) * std::cos(e.pitch), std::cos(e.yaw) * std::cos(e.pitch), std::sin(e.pitch)};
    Vec3 want = nose * speed;
    e.gVel.x = approachAxis(e.gVel.x, want.x, e.accel, e.brake, ticks);
    e.gVel.y = approachAxis(e.gVel.y, want.y, e.accel, e.brake, ticks);
    e.gVel.z = approachAxis(e.gVel.z, want.z, e.accel, e.brake, ticks);
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    e.pos = e.pos + e.gVel * (ticks * unitScale_);
    // only the surface right below counts (paths may lead under overhangs and bridges)
    float g = coll_->groundBelow(e.pos + Vec3{0, 0, 3});
    if (g > -1e8f && e.pos.z < g + 1.f) { e.pos.z = g + 1.f; if (e.gVel.z < 0) e.gVel.z = 0; }
    e.drift = e.gVel * (kTick * unitScale_);
}

// ---------------------------------------------------------------------------------------------
// DEATHSTORM_ENEMY / BIENENSTOCK_ENEMY (0x8093c): the flying BLITZ. Path like the ground units (3D
// boxes, z half size; a point counts as reached within 1.5 radius). Attack: 8 approach in 3D until
// tank_good_r (2D), then - if tank_escape_ofset > 0 - 0x10 turn away for 1000 ticks or until
// distance + escape_ofset, climbing/diving with a random slope (+-16/32), else hover there.
// Same feelers as the ground units; pitch is limited to +-400 units; fires whenever awake.
void EnemySystem::deathstormAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    if (e.alertT > 0) e.alertT -= ticks;
    float speed = e.rawSpeed, rot = e.rotSpeed;
    bool hit = e.wasHit; e.wasHit = false;
    if (hit) { e.alertT = 350; if (!e.hitLatched) { e.gs = 0; e.hitLatched = true; } groundAlert(e); }
    auto box3 = [](Vec3 a, Vec3 b, float r) { return std::fabs(a.x - b.x) < r && std::fabs(a.y - b.y) < r && std::fabs(a.z - b.z) < r / 2; };
    auto nose = [&]() { return Vec3{-std::sin(e.yaw) * std::cos(e.pitch), std::cos(e.yaw) * std::cos(e.pitch), std::sin(e.pitch)}; };
    auto pitchOf = [](Vec3 d) { return std::clamp(std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)), -400.f * kU, 400.f * kU); };
    if (e.waitT > 0) { e.waitT -= ticks; if (e.waitT < 0) e.waitT = 0; e.gVel = {}; return; }
    const auto* P = e.path && !e.path->empty() && !e.pathDone ? e.path : nullptr;
    bool onPath = false;
    if (P) {
        if (!e.pathStarted) {
            Vec3 first{(*P)[0].pos[0] * unitScale_, (*P)[0].pos[1] * unitScale_, (*P)[0].pos[2] * unitScale_};
            if (box3(first, player, e.pathActivation)) e.pathStarted = true;
            else if (!e.hitAct) { e.gVel = {}; return; }
        }
        if (e.pathIdx >= P->size()) {
            if (P->back().flag & 8) e.pathIdx = 0; else { e.pathDone = true; e.pathEnd = true; }
        }
        if (!e.pathDone) {
            const PathPoint& pp = (*P)[e.pathIdx];
            bool fight = (pp.flag & 0x20) && box3(e.pos, player, e.actBox) && pp.time == 0;
            if (!fight) {
                onPath = true; e.state = OnPath;
                rot *= 2;
                e.noShootPoint = (pp.flag & 4) != 0;
                if (pp.flag & 1) speed /= 2;
                if (pp.flag & 0x10) speed = float(pp.speed);
                if (pp.flag & 2) speed *= 2;
                Vec3 wp{pp.pos[0] * unitScale_, pp.pos[1] * unitScale_, pp.pos[2] * unitScale_};
                Vec3 d = e.pos - wp; float r = e.radius * 1.5f;
                if (std::fabs(d.x) < r && std::fabs(d.y) < r && std::fabs(d.z) < r) { e.pathIdx++; if (pp.time > 0) e.waitT = float(pp.time); }
                if (!e.collided) { e.yaw = turnToward(e.yaw, yawOf(wp - e.pos), rot, ticks); e.pitch = turnToward(e.pitch, pitchOf(wp - e.pos), rot, ticks); }
                else { e.yaw = wrapAngle(e.yaw + rot * ticks * kU); e.collided = false; }
                e.roll = 0;
                e.gVel = nose() * speed;
            }
        }
    }
    if (!onPath) {
        e.noShootPoint = false;
        bool woke = (playerAlive && inBox2(e.pos, player, e.actBox)) || e.alertT > 0;
        if (woke) { if (e.gs == 0) { e.gs = 8; e.gDist = e.pGood; e.gInactive = false; } }
        else { e.gs = 0; e.gInactive = true; e.hitLatched = false; }
        if (e.gs == 0) e.gVel = {};
        e.state = e.gInactive ? Idle : Attack;
        if (!e.gInactive) {
            Vec3 d = player - e.pos;
            const float dist = std::sqrt(d.x * d.x + d.y * d.y);
            Vec3 steerDir = d;
            if ((e.gs & 8) && dist < e.gDist) {
                if (e.pEsc < 1e-3f) speed = 0;                                // hover at good_r
                else { e.gs = 0x10; e.gTimer = 1000; e.gDist = e.pEsc + dist; e.gTargetYaw = float(int(nextRnd() % 32) - 16); }
            }
            if (e.gs & 0x10) {
                if (e.gDist < dist || e.gTimer < 0) { e.gs = 8; e.gDist = e.pGood; }
                else {
                    e.gTimer -= ticks;
                    if (float(nextRnd() % 256) < ticks) e.gTargetYaw = float(int(nextRnd() % 32) - 16);
                    steerDir = Vec3{-d.x, -d.y, e.gTargetYaw * dist / 32.f};      // away, random climb / dive
                }
            }
            e.gWant = {};
            if (e.gs & 0x18) {
                const bool front = probe(e, 0, 7);
                const bool l9d = front && probe(e, -280, 9), r9e = front && probe(e, 280, 11);
                const bool l9f = probe(e, -500, 4), ra0 = probe(e, 500, 4);
                e.gWant = nose() * speed;
                auto div = [&](int c) { if (e.gTurnCase == c) e.gTurnDiv *= 0.5f; else { e.gTurnDiv = 32; e.gTurnCase = c; } if (e.gTurnDiv < 1) e.gTurnDiv = 1; };
                if (!front && !e.collided) {
                    e.gRandDir = (nextRnd() % 128) < 64 ? 1 : -1;
                    if (!l9f && !ra0) {
                        e.gTurnCase = 0;
                        e.yaw = turnToward(e.yaw, yawOf(steerDir), rot, ticks);
                        e.pitch = turnToward(e.pitch, pitchOf(steerDir), rot, ticks);
                    } else {
                        if (l9f) { div(1); e.yaw = wrapAngle(e.yaw + rot * ticks / e.gTurnDiv * kU); }
                        if (ra0) { div(2); e.yaw = wrapAngle(e.yaw - rot * ticks / e.gTurnDiv * kU); }
                    }
                } else {
                    e.gWant = e.gWant * 0.5f;
                    if (!e.collided) {
                        if (!l9d) {
                            div(3); e.yaw = wrapAngle(e.yaw - rot * ticks / e.gTurnDiv * kU);
                            if (!r9e && float(nextRnd() % 1024) < ticks) e.gRandDir = (nextRnd() % 128) < 64 ? 1 : -1;
                        } else if (!r9e) { div(4); e.yaw = wrapAngle(e.yaw + rot * ticks / e.gTurnDiv * kU); }
                        else { div(5); e.yaw = wrapAngle(e.yaw + float(e.gRandDir) * rot * ticks / e.gTurnDiv * kU); }
                    } else { e.yaw = wrapAngle(e.yaw + float(e.gRandDir) * rot * ticks * kU); e.collided = false; }
                }
            }
            e.gVel.x = approachAxis(e.gVel.x, e.gWant.x, e.accel, e.brake, ticks);
            e.gVel.y = approachAxis(e.gVel.y, e.gWant.y, e.accel, e.brake, ticks);
            e.gVel.z = approachAxis(e.gVel.z, e.gWant.z, e.accel, e.brake, ticks);
        }
    }
    e.pitch = std::clamp(e.pitch, -400.f * kU, 400.f * kU);
    Vec3 step = e.gVel * (ticks * unitScale_);
    if (dot(step, step) < 1e-10f) return;
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    Vec3 next = e.pos + step;
    Vec3 hp; int owner = -1;
    Vec3 dir = normalize(step);
    if (coll_->raycast(e.pos, next + dir * (e.radius * 0.7f), hp, owner)) { e.collided = true; return; }
    float g = coll_->groundBelow(next + Vec3{0, 0, 3});
    if (g > -1e8f && next.z < g + 2.f) { e.collided = true; return; }
    e.pos = next;
    e.drift = e.gVel * (kTick * unitScale_);
}

// ---------------------------------------------------------------------------------------------
// Mines. FLOH_ENEMY / BANDIT_ENEMY (0x78f00): only act while the player is inside the
// ACTIVATIONAREA box (z half). FLOH stays put; BANDIT drifts at 100 units per tick towards the
// player, re-aiming every 20 ticks (pitch = height difference / 50, max +-256). Touching the
// player (player radius * 7/8) blows the mine up. KILLER_ENEMY (0x85b0c): wakes for good when the
// player enters the ACTIVATIONAREA box around its first path point (or itself), then homes in at
// 'speed' with accelerate / brake; contact (player radius + mine_coll_ofset) deals cause_damage;
// hitting anything else destroys it too.
void EnemySystem::mineAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    Vec3 d = player - e.pos;
    const float playerR = 4.f;
    if (e.kind == KFloh) {
        bool in = playerAlive && ((std::fabs(d.x) < e.actBox && std::fabs(d.y) < e.actBox && std::fabs(d.z) < e.actBox / 2) || e.hitAct);
        e.state = in ? Attack : Idle;
        if (e.bandit && in) {
            if (e.mTimer <= 0) {
                e.yaw = yawOf(d);
                e.pitch = std::clamp(d.z * 1000.f / 50.f, -256.f, 256.f) * kU;
                e.mTimer = 20;
            }
            e.mTimer -= ticks;
            if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
            Vec3 nose{-std::sin(e.yaw) * std::cos(e.pitch), std::cos(e.yaw) * std::cos(e.pitch), std::sin(e.pitch)};
            Vec3 next = e.pos + nose * (100.f * ticks * unitScale_);
            Vec3 hp; int owner = -1;
            if (!coll_->raycast(e.pos, next, hp, owner)) e.pos = next;
            d = player - e.pos;
        }
        // contact check also without an ACTIVATIONAREA (touching a mine always sets it off)
        float r = playerR * 7.f / 8.f + e.radius + e.mineOfs;
        if (playerAlive && dot(d, d) < r * r) {
            if (std::getenv("XA_DEBUG")) std::printf("mine %d explodes on the player\n", e.wi);
            combat_->ramPlayer(e.wi, e.causeDmg, e.pos);
        }
        return;
    }
    // KILLER
    if (e.gs == 0) {
        Vec3 c = e.pos;
        if (e.path && !e.path->empty()) c = {(*e.path)[0].pos[0] * unitScale_, (*e.path)[0].pos[1] * unitScale_, 0};
        if (playerAlive && inBox2(c, player, e.actBox)) { e.gs = 8; e.gVel = {}; }
    }
    e.state = e.gs ? Attack : Idle;
    if (!(e.gs & 8)) return;
    float l = std::sqrt(dot(d, d));
    Vec3 want = l > 1e-3f ? d * (e.rawSpeed / l) : Vec3{};
    e.gVel.x = approachAxis(e.gVel.x, want.x, e.accel, e.brake, ticks);
    e.gVel.y = approachAxis(e.gVel.y, want.y, e.accel, e.brake, ticks);
    e.gVel.z = approachAxis(e.gVel.z, want.z, e.accel, e.brake, ticks);
    if (dot(e.gVel, e.gVel) > 1e-6f) e.yaw = yawOf(e.gVel);
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    Vec3 next = e.pos + e.gVel * (ticks * unitScale_);
    float r = playerR + e.mineOfs + e.radius;
    Vec3 dn = player - next;
    if (dot(dn, dn) < r * r) {
        if (std::getenv("XA_DEBUG")) std::printf("killer %d hits the player\n", e.wi);
        combat_->ramPlayer(e.wi, e.causeDmg, next);
        return;
    }
    Vec3 hp; int owner = -1;
    float g = coll_->groundBelow(next + Vec3{0, 0, 2});
    if (coll_->raycast(e.pos, next, hp, owner) || (g > -1e8f && next.z < g + 0.5f)) {   // crashed into something
        if (std::getenv("XA_DEBUG")) { const char* nm = "?"; if (owner >= 0 && size_t(owner) < world_->objects().size()) { const auto* t = types_->get(world_->objects()[size_t(owner)].objClass); if (t) nm = t->name.c_str(); }
            std::printf("killer %d crashed (owner %d %s, ground %.1f z %.1f)\n", e.wi, owner, nm, g, next.z); }
        combat_->ramPlayer(e.wi, 0.f, e.pos);
        return;
    }
    e.pos = next;
}

// ---------------------------------------------------------------------------------------------
// AMBUSH (0x839c8): hides at the first point of its PATH. 2: when the player is inside the
// ACTIVATIONAREA box (or after an alert) and the wait timer is over, 8: drive the path out to its
// last point at 'speed', 0x10: stay there hi_shoot_t ticks (a hit cuts it to hi_reaction_t), then
// back along the path (strict_path: first turn its back to the path, 0x40) reversing (0x20) to
// the start, 2 again with hi_wait_t. go_on_ground 0: flies (vertical speed max 200, pitch +-400).
void EnemySystem::ambushAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    const float speed = e.rawSpeed, rot = std::max(e.rotSpeed, 1.f);
    if (e.alertT > 0) e.alertT -= ticks;
    bool hit = e.wasHit; e.wasHit = false;
    if (hit) { e.alertT = 350; groundAlert(e); }
    if (!e.path || e.path->size() < 2) { e.state = Idle; return; }
    const auto& P = *e.path;
    auto pt = [&](int i) { i = std::clamp(i, 0, int(P.size()) - 1); return Vec3{P[i].pos[0] * unitScale_, P[i].pos[1] * unitScale_, P[i].pos[2] * unitScale_}; };
    if (e.aState == 0) { e.aState = 2; e.aIdx = 1; e.aTimer = 0; }
    Vec3 want{};
    float wantYaw = e.yaw, wantPitch = 0;
    if (e.aState == 2) {
        e.aTimer -= ticks;
        if ((playerAlive && inBox2(e.pos, player, e.actBox) && e.aTimer < 0) || e.alertT > 0) e.aState = 8;
    }
    if (e.aState == 0x10) {
        if (hit && e.hiReactionT < e.aTimer) e.aTimer = float(e.hiReactionT);
        if (e.aTimer < 0) e.aState = e.strictPath ? 0x40 : 0x20;
        else {
            e.aTimer -= ticks;
            Vec3 d = player - e.pos;
            wantYaw = yawOf(d); wantPitch = std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y));
        }
    }
    if (e.aState == 0x40) {
        Vec3 d = e.pos - pt(e.aIdx);
        wantYaw = yawOf(d);
        if (std::fabs(wrapAngle(wantYaw - e.yaw)) / kU < 200.f) e.aState = 0x20;
    }
    auto drive = [&](int dirStep) {
        Vec3 wp = pt(e.aIdx);
        Vec3 d = wp - e.pos;
        if (e.goOnGround) d.z = 0;
        float l = std::sqrt(dot(d, d));
        float stepLen = speed * ticks * unitScale_;
        want = l > 1e-4f ? d * (speed / l) : Vec3{};
        wantYaw = dirStep > 0 ? yawOf(d) : yawOf(d * -1.f);
        if (l <= stepLen) {
            want = ticks > 0 ? d * (1.f / (ticks * unitScale_)) : Vec3{};
            e.aIdx += dirStep;
            return true;
        }
        return false;
    };
    if (e.aState == 8) {
        if (drive(1) && e.aIdx >= int(P.size())) { e.aState = 0x10; e.aTimer = float(e.hiShootT); e.aIdx = int(P.size()) - 2; }
    } else if (e.aState == 0x20) {
        if (drive(-1) && e.aIdx < 0) { e.aState = 2; e.aIdx = 1; e.aTimer = float(e.hiWaitT); }
    }
    e.state = e.aState == 2 ? Idle : Attack;
    if (!e.goOnGround) {
        want.z = std::clamp(want.z, -200.f, 200.f);
        wantPitch = (e.aState == 0x10) ? std::clamp(wantPitch, -400.f * kU, 400.f * kU) : 0.f;
        e.pitch = turnToward(e.pitch, wantPitch, rot, ticks);
    }
    e.yaw = turnToward(e.yaw, wantYaw, rot, ticks);
    e.gVel = want;
    Vec3 step = want * (ticks * unitScale_);
    if (dot(step, step) < 1e-10f) return;
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    e.pos = e.pos + step;
    e.drift = want * (kTick * unitScale_);
}

// TUNNELPATH (0x84b4c): the points of its PATH form a graph (neighbours along the path; points
// within 7 units are merged, 0x88a88). A unit walks node to node: 4 picks the next free node -
// with tu_free_go % a random one, otherwise the neighbour that gets closer to the player but not
// within tu_min_radius, or, when the player is already that close, one that leads away from him
// (cos < 0.4; tu_escape_back: driving backwards). 2 drives there (turning on the spot while the
// heading is off by more than tu_turn_before_go; strict_path: exact straight line), 8 stays
// tu_shoot_t ticks facing the player (tu_turn_to_enemy). Woken by the player inside the
// PATHACTIVATIONAREA box or an alert. Always fires.
EnemySystem::TunnelGraph& EnemySystem::graphFor(const std::vector<PathPoint>* path) {
    auto it = graphs_.find(path);
    if (it != graphs_.end()) return it->second;
    TunnelGraph& g = graphs_[path];
    const size_t n = path->size();
    std::vector<int> canon(n);
    std::vector<Vec3> pts(n);
    for (size_t i = 0; i < n; i++) pts[i] = {(*path)[i].pos[0] * unitScale_, (*path)[i].pos[1] * unitScale_, (*path)[i].pos[2] * unitScale_};
    for (size_t i = 0; i < n; i++) {
        canon[i] = int(i);
        for (size_t j = 0; j < i; j++) { if (canon[j] != int(j)) continue; Vec3 d = pts[i] - pts[j]; if (dot(d, d) < 7.f * 7.f) { canon[i] = int(j); break; } }
    }
    std::vector<int> id(n, -1);
    for (size_t i = 0; i < n; i++) if (canon[i] == int(i)) { id[i] = int(g.pos.size()); g.pos.push_back(pts[i]); }
    g.nb.resize(g.pos.size()); g.occ.assign(g.pos.size(), 0);
    auto link = [&](int a, int b) { if (a == b) return; for (int x : g.nb[size_t(a)]) if (x == b) return; g.nb[size_t(a)].push_back(b); };
    for (size_t i = 0; i + 1 < n; i++) { int a = id[size_t(canon[i])], b = id[size_t(canon[i + 1])]; link(a, b); link(b, a); }
    return g;
}

void EnemySystem::tunnelAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    const float rot = std::max(e.rotSpeed, 1.f);
    if (e.alertT > 0) e.alertT -= ticks;
    bool hit = e.wasHit; e.wasHit = false;
    if (hit) { e.alertT = 350; groundAlert(e); }
    if (!e.path || e.path->empty()) { e.state = Idle; return; }
    TunnelGraph& g = graphFor(e.path);
    if (e.tNode < 0) {       // start at the nearest node
        float best = 1e30f;
        for (size_t i = 0; i < g.pos.size(); i++) { Vec3 d = g.pos[i] - e.pos; float d2 = dot(d, d); if (d2 < best && !g.occ[i]) { best = d2; e.tNode = int(i); } }
        if (e.tNode < 0) e.tNode = 0;
        g.occ[size_t(e.tNode)] = 1;
        e.aState = 0;
    }
    const bool woke = (playerAlive && inBox2(e.pos, player, e.pathActivation)) || e.alertT > 0;
    float speed = e.rawSpeed;
    Vec3 want{};
    if (e.aState == 0 && woke) e.aState = 4;
    if (e.aState == 2) {
        Vec3 node = g.pos[size_t(e.tNode)];
        Vec3 d = node - e.pos;
        if (e.goOnGround) d.z = 0;                       // height follows the ground
        float dist = std::sqrt(dot(d, d));
        Vec3 face = e.tBack ? d * -1.f : d;
        e.yaw = turnToward(e.yaw, yawOf(face), rot, ticks);
        if (!e.goOnGround) e.pitch = turnToward(e.pitch, std::clamp(std::atan2(face.z, std::sqrt(face.x * face.x + face.y * face.y)), -1023.f * kU, 1023.f * kU), rot, ticks);
        if (!e.strictPath) {
            Vec3 nose{-std::sin(e.yaw) * std::cos(e.pitch), std::cos(e.yaw) * std::cos(e.pitch), std::sin(e.pitch)};
            want = nose * (e.tBack ? -speed : speed);
        } else if (dist > 1e-4f) want = d * (speed / dist);
        if (std::fabs(wrapAngle(yawOf(face) - e.yaw)) / kU > e.tuTurnBefore) want = {};
        bool arrived = e.strictPath ? dist <= speed * ticks * unitScale_ : dist < 10.f;
        if (arrived) {
            if (e.strictPath && ticks > 0) want = d * (1.f / (ticks * unitScale_));
            e.aState = 8; e.aTimer = float(e.tuShootT);
        }
    }
    if (e.aState == 8 && woke) {
        if (e.aTimer < 0) e.aState = 4;
        else {
            if (e.tuTurnToEnemy) {
                Vec3 d = player - e.pos;
                e.yaw = turnToward(e.yaw, yawOf(d), rot, ticks);
                if (!e.goOnGround) e.pitch = turnToward(e.pitch, std::clamp(std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)), -512.f * kU, 512.f * kU), rot, ticks);
            }
            want = {};
            e.aTimer -= ticks;
        }
    }
    if (e.aState == 4) {
        const auto& nbs = g.nb[size_t(e.tNode)];
        Vec3 dp = player - e.pos;
        float pd = std::sqrt(dot(dp, dp));
        int pick = -1; bool back = false;
        if (int(nextRnd() % 100) < e.tuFreeGo) {
            if (!nbs.empty()) { int c = nbs[nextRnd() % nbs.size()]; if (!g.occ[size_t(c)]) pick = c; }
        } else {
            float bestD = pd, bestCos = 1.f;
            for (int c : nbs) {
                if (g.occ[size_t(c)]) continue;
                Vec3 dn = g.pos[size_t(c)] - player;
                float nd = std::sqrt(dot(dn, dn));
                if (e.tuMinR < pd) { if (nd < bestD && nd > e.tuMinR) { pick = c; bestD = nd; } }
                else if (nd > bestD) {
                    float cs = nd > 0 && pd > 0 ? dot(dp, dn) / (nd * pd) : 1.f;
                    if (cs < bestCos && cs < 0.4f) { pick = c; bestD = nd; bestCos = cs; back = e.strictPath; }
                }
            }
        }
        if (pick < 0) { e.aState = 8; e.aTimer = 50; }
        else { g.occ[size_t(e.tNode)] = 0; e.tNode = pick; g.occ[size_t(pick)] = 1; e.aState = 2; e.tBack = back; }
    }
    e.state = e.aState == 0 ? Idle : Attack;
    if (!e.strictPath) {
        e.gVel.x = approachAxis(e.gVel.x, want.x, e.accel, e.brake, ticks);
        e.gVel.y = approachAxis(e.gVel.y, want.y, e.accel, e.brake, ticks);
        e.gVel.z = approachAxis(e.gVel.z, want.z, e.accel, e.brake, ticks);
    } else {
        e.gVel = want;
        if (e.goOnGround) e.gVel.z = std::clamp(e.gVel.z, -200.f, 200.f);
    }
    Vec3 step = e.gVel * (ticks * unitScale_);
    if (dot(step, step) < 1e-10f) return;
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    e.pos = e.pos + step;
    e.drift = e.gVel * (kTick * unitScale_);
}

// MARS_ENEMY (0x85e44): flies its PATH straight from point to point (reached within 3 radius),
// then hovers (8, fires only here). Closer than panther_min_r or hit: 2 dash in a random
// direction (vertical +-speed/8) for 50 (hit: 100) ticks; farther than panther_max_r: 0x10 dash
// at the player for 50 ticks. Stays between 10 below and 50 above the player's height.
void EnemySystem::marsAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    float speed = e.rawSpeed;
    bool hit = e.wasHit; e.wasHit = false;
    if (e.waitT > 0) { e.waitT -= ticks; if (e.waitT < 0) e.waitT = 0; e.gVel = {}; return; }
    const auto* P = e.path && !e.path->empty() && !e.pathDone ? e.path : nullptr;
    bool onPath = false;
    Vec3 want{};
    if (P) {
        if (!e.pathStarted) {
            Vec3 f{(*P)[0].pos[0] * unitScale_, (*P)[0].pos[1] * unitScale_, (*P)[0].pos[2] * unitScale_};
            Vec3 d = f - player;
            if (std::fabs(d.x) < e.pathActivation && std::fabs(d.y) < e.pathActivation && std::fabs(d.z) < e.pathActivation / 2) e.pathStarted = true;
            else if (!e.hitAct) return;
        }
        if (e.pathIdx >= P->size()) { if (P->back().flag & 8) e.pathIdx = 0; else { e.pathDone = true; e.pathEnd = true; } }
        if (!e.pathDone) {
            const PathPoint& pp = (*P)[e.pathIdx];
            if (pp.flag & 1) speed /= 2;
            if (pp.flag & 0x10) speed = float(pp.speed);
            if (pp.flag & 2) speed *= 2;
            Vec3 wp{pp.pos[0] * unitScale_, pp.pos[1] * unitScale_, pp.pos[2] * unitScale_};
            Vec3 d = wp - e.pos; float l = std::sqrt(dot(d, d));
            if (l > 1e-4f) want = d * (speed / l);
            float r = 3 * e.radius;
            if (std::fabs(d.x) < r && std::fabs(d.y) < r && std::fabs(d.z) < r) { e.pathIdx++; if (pp.time > 0) e.waitT = float(pp.time); }
            onPath = true; e.state = OnPath; e.gs = 8;
        }
    }
    if (!onPath) {
        e.state = Attack;
        Vec3 dp = player - e.pos; float dist = std::sqrt(dot(dp, dp));
        if (e.gs == 0) e.gs = 8;
        want = e.gWant;
        if (e.gs == 8) {
            want = {};
            if (dist < e.pMin) { e.gs = 2; e.gTimer = 50; }
            if (dist > e.pMax) { e.gs = 0x10; e.gTimer = 50; }
            if (hit) { e.gs = 2; e.gTimer = 100; }
        }
        if (e.gs & 0x10) { if (dist > 0) want = dp * (speed / dist); e.gs = 4; }
        if (e.gs & 2) {
            float a = float(nextRnd() % 4096) * kU;
            want = {-std::sin(a) * speed, std::cos(a) * speed, speed > 0 ? speed / 8 - float(int(nextRnd()) % int(std::max(1.f, speed))) / 4 : 0};
            e.gs = 4;
        }
        if (e.gs & 4) {
            if (e.gTimer < 0) e.gs = 0;
            e.gTimer -= ticks;
            if (e.collided) { e.gs = 2; e.collided = false; }
        }
        e.gWant = want;
        if (dot(dp, dp) > 0) e.yaw = turnToward(e.yaw, yawOf(dp), std::max(e.rotSpeed, 10.f), ticks);
    }
    e.gVel.x = approachAxis(e.gVel.x, want.x, e.accel, e.brake, ticks);
    e.gVel.y = approachAxis(e.gVel.y, want.y, e.accel, e.brake, ticks);
    e.gVel.z = approachAxis(e.gVel.z, want.z, e.accel, e.brake, ticks);
    Vec3 next = e.pos + e.gVel * (ticks * unitScale_);
    if (!onPath) {
        if (next.z < player.z - 10.f) { next.z = player.z - 10.f; if (e.gs & 8) { e.gs = 2; e.gTimer = 50; } }
        if (next.z > player.z + 50.f) { e.gVel.z = 0; next.z = e.pos.z; }
    }
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    Vec3 hp; int owner = -1;
    float g = coll_->groundBelow(next + Vec3{0, 0, 3});
    if (coll_->raycast(e.pos, next, hp, owner) || (g > -1e8f && next.z < g + 2.f)) { e.collided = true; e.gVel = {}; e.gWant = {}; return; }
    e.pos = next;
    e.drift = e.gVel * (kTick * unitScale_);
}

// SPIDER_ENEMY (dispatcher case 0x6d) / SPIDER360_ENEMY (0x6e): 0x77534 with DEF speed / rotatespeed,
// a third DEF value (leg frame speed), cycle 100 and flags 0xb / 0xc. Its decisions are those of the
// vehicle routine 0x7635c (path, return home, flee at <= 2000 HP, back off when close, the player as the
// exact target, flag 8: a hit forces a decision and turns it by +-1000..3000 for rand%193+64 ticks),
// so vehicleAI runs it and hands the movement to spiderMove. Flags 2 / 4 animate the legs (spiderLegs,
// 0x7623c). Always fires.
void EnemySystem::spiderAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    if (e.spAir) { spiderMove(e, e.spSpeed, dt * kTick); return; }   // a jump runs to its end
    vehicleAI(e, player, playerAlive, dt);
}

bool EnemySystem::movingUnitNear(const Enemy& self, Vec3 p, float r) const {
    for (const Enemy& o : enemies_) {
        if (&o == &self || !o.moving || !o.active || o.kind == KPluto || o.kind == KMover || o.crashing) continue;
        const Combat::Target* t = combat_->target(o.wi);
        if (t && !t->alive) continue;
        Vec3 d = o.pos - p;
        float rr = r + o.radius * 0.5f;
        if (dot(d, d) < rr * rr) return true;
    }
    return false;
}

// Body orientation: the object's angle words a0, a1 (tilt) and a2 (heading), drawn like a placed object
// (worldObjectRotation: Rx(-a0) * Ry(a1) * Rz(-a2)); the heading is innermost, so it turns about the body's up.
Mat4 EnemySystem::spiderRot(const Enemy& e) {
    return Mat4::rotX(-e.spA0 * kU) * Mat4::rotY(e.spA1 * kU) * Mat4::rotZ(e.yaw) * Mat4::rotZ(kPiF);   // model front is -y (see faceFlip)
}

// Target tilt angles for a surface normal (0x784ef..0x78588): a1 from the normal's lean towards x, a0 from
// the lean towards y of the normal turned back by a1, so that Rx(-a0) * Ry(a1) * (0, 0, 1) is the normal.
// A ceiling (0, 0, -1) gives a0 0x800, a1 0, the upside-down pose of a jump.
static void tiltAngles(Vec3 n, float& a0, float& a1) {
    const float k = 4096.f / 6.2831853f;
    a1 = std::asin(std::clamp(n.x, -1.f, 1.f)) * k;
    a0 = std::atan2(n.y, n.z) * k;
}

// 0x73a10 for one angle word: angle += difference (the shorter way round) * clamp(rate * ticks, 2, 480) / 512 per
// original frame.
static void approachAngle(float& a, float target, float rate, float ticks) {
    float k = std::clamp(rate * kOrigFrame, 2.f, 480.f) / 512.f;
    float f = 1.f - std::pow(1.f - std::min(k, 1.f), ticks / kOrigFrame);
    float d = std::fmod(target - a, 4096.f);
    if (d > 2048.f) d -= 4096.f; else if (d < -2048.f) d += 4096.f;
    a = std::fmod(a + d * f, 4096.f);
}

void EnemySystem::tiltTowards(Enemy& e, Vec3 normal, float rate, float ticks) {
    float t0, t1;
    if (e.kind == KSpider) tiltAngles(normal, t0, t1);
    else {
        // vehicles (0x77015..0x77064): each angle from its own plane, a0 = atan2(n.y, n.z), a1 = atan2(n.x, n.z)
        const float k = 4096.f / 6.2831853f;
        t0 = std::atan2(normal.y, normal.z) * k;
        t1 = std::atan2(normal.x, normal.z) * k;
    }
    approachAngle(e.spA0, t0, rate, ticks);
    approachAngle(e.spA1, t1, rate, ticks);
    e.spUp = (Mat4::rotX(-e.spA0 * kU) * Mat4::rotY(e.spA1 * kU)).transformPoint({0, 0, 1});
}

// SPIDER movement (0x77534 from 0x77f39 on):
// - Walking: the tilt angles approach the surface normal by 20 per tick and the heading approaches the
//   decision target by rotatespeed per tick (0x73a10); the body moves forward along its own heading.
//   Then all surfaces within half the radius are taken (0xe108a / 0xe1948): none - it falls 175 per tick
//   and a fall of more than 20 radii below the last surface destroys it (0x7281c); otherwise the
//   nearest plane holds it (0x7132c: origin = projection + normal * (400 + model height)) and its normal
//   becomes the new tilt target. SPIDER (flag 1) does not walk on ceilings: touching a plane whose
//   normal points down (z < -32/16384) undoes the step and starts a jump ([0x7c] = 1, speed -100 * the
//   normal, so the fall starts slowly).
// - Jump: position += speed per tick, z -= 175 per tick, speed * 62/64 per frame, the body turns over
//   (angle 0 towards 0x800, 1 towards 0, by 16 per tick). Landing on a plane with normal z > 10000/16384
//   ends it; a steeper plane bounces it off at twice the walking speed.
void EnemySystem::spiderMove(Enemy& e, float speedRaw, float ticks) {
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    static thread_local std::vector<CollisionWorld::Touch> touch;
    const float qr = std::max(e.radius * 0.5f, e.spH + 0.3f);
    auto turnUp = [&](Vec3 target, float rate) { tiltTowards(e, target, rate, ticks); };
    auto nearest = [&]() -> const CollisionWorld::Touch* {
        const CollisionWorld::Touch* b = nullptr;
        for (const auto& t : touch) if (t.ground && (!b || t.dist < b->dist)) b = &t;
        return b;
    };
    auto anyGround = [&]() { for (const auto& t : touch) if (t.ground) return true; return false; };
    // leg frame [0x94] += spider_frame_speed * tick / div, wraps above 60: /512 on a surface (0x7857c), /0x500 while
    // falling (0x785ef) and when dropping off a ceiling (0x783e8); it stands still during a jump
    auto legStep = [&](float div) {
        e.spFrame += e.spFrameSpeed * ticks / div;
        while (e.spFrame > 60.f) e.spFrame -= 60.f;
    };
    // 0x78665: other objects within half the radius undo the move; the heading target becomes the old one
    // +12 per tick (objects, 0x21c57c) or -12 per tick (friendly buildings 0x21c5a4, units 0x21c578), and no
    // decision is taken for 200 ticks. Trees (0x21c580) are walked through.
    auto objectBlock = [&](Vec3 oldPos, Vec3 oldUpT, float oldYawT) {
        coll_->touching(e.pos, e.radius * 0.5f, touch);
        for (const auto& t : touch) {
            if (t.ground || t.owner < 0 || !world_ || size_t(t.owner) >= world_->objects().size()) continue;
            const ObjectType* ty = types_->get(world_->objects()[size_t(t.owner)].objClass);
            const std::string beh = ty ? ty->behavior : std::string();
            if (beh == "TREE" || beh == "WATER" || beh == "SKY") continue;
            const float sign = (beh == "BUILDING_FRIENDLY" || byWorld_[size_t(t.owner)] >= 0) ? -1.f : 1.f;   // units: 0x21c578
            e.pos = oldPos; e.spUpT = oldUpT;
            e.gTargetYaw = wrapAngle(oldYawT + sign * 12.f * ticks * kU);
            e.gTimer = 200;
            return true;
        }
        if (movingUnitNear(e, e.pos, e.radius * 0.5f)) {                                  // moving units (0x21c578): -12
            if (std::getenv("XA_DEBUG")) std::printf("spider %d blocked by a unit\n", e.wi);
            e.pos = oldPos; e.spUpT = oldUpT;
            e.gTargetYaw = wrapAngle(oldYawT - 12.f * ticks * kU);
            e.gTimer = 200;
            return true;
        }
        return false;
    };
    if (e.spAir) {
        const Vec3 oldPos = e.pos, oldUpT = e.spUpT; const float oldYawT = e.gTargetYaw;
        turnUp({0, 0, -1}, 16.f);
        e.pos = e.pos + e.spVel * (ticks * unitScale_);
        e.pos.z -= 175.f * ticks * unitScale_;
        e.spVel = e.spVel * std::pow(62.f / 64.f, ticks / kOrigFrame);
        coll_->touching(e.pos, qr, touch);
        if (const CollisionWorld::Touch* t = nearest()) {
            e.pos = t->onPlane + t->n * e.spH;
            if (t->n.z > 10000.f / 16384.f) { e.spAir = false; e.spUpT = t->n; e.spLastZ = e.pos.z; }
            else e.spVel = t->n * (2.f * speedRaw);
        }
        e.anim += ticks;
        objectBlock(oldPos, oldUpT, oldYawT);
        return;
    }
    const Vec3 oldPos = e.pos, oldUpT = e.spUpT; const float oldYawT = e.gTargetYaw;
    if (std::getenv("XA_SPIDER") && int(e.anim / 60.f) != int((e.anim + ticks) / 60.f))
        std::printf("spider %d pos %.1f %.1f %.1f up %.2f %.2f %.2f (target %.2f %.2f %.2f, a0 %.0f a1 %.0f) speed %.0f ground %.2f\n", e.wi, e.pos.x, e.pos.y, e.pos.z,
                    e.spUp.x, e.spUp.y, e.spUp.z, e.spUpT.x, e.spUpT.y, e.spUpT.z, e.spA0, e.spA1, speedRaw, e.pos.z - coll_->groundBelow(e.pos + Vec3{0, 0, 0.5f}));
    turnUp(e.spUpT, 20.f);
    {
        float k = std::clamp(e.vTurn * kOrigFrame, 2.f, 480.f) / 512.f;
        float f = 1.f - std::pow(1.f - std::min(k, 1.f), ticks / kOrigFrame);
        e.yaw = wrapAngle(e.yaw + wrapAngle(e.gTargetYaw - e.yaw) * f);
    }
    Vec3 fwd = spiderRot(e).transformPoint({0, 1, 0});
    e.pos = e.pos + fwd * (speedRaw * ticks * unitScale_);
    if (speedRaw > 0) e.anim += ticks;
    coll_->touching(e.pos, qr, touch);
    if (!anyGround()) {
        e.pos.z -= 175.f * ticks * unitScale_;
        legStep(1280.f);
        if (std::fabs(e.pos.z - e.spLastZ) > 20.f * e.radius) {
            if (std::getenv("XA_DEBUG")) std::printf("spider %d fell to death at %.1f %.1f %.1f\n", e.wi, e.pos.x, e.pos.y, e.pos.z);
            combat_->kill(e.wi, e.pos);
            return;
        }
        objectBlock(oldPos, oldUpT, oldYawT);
        return;
    }
    if (e.spFlags & 1) {
        for (const auto& t : touch) {
            if (!t.ground || t.n.z >= -32.f / 16384.f) continue;
            e.pos = oldPos; e.spUpT = oldUpT; e.gTargetYaw = oldYawT;
            const CollisionWorld::Touch* b = nearest();
            e.pos = oldPos - b->n * (dot(oldPos - b->onPlane, b->n) - e.spH);
            e.spVel = b->n * -100.f;                                                    // 0x78454: -100 * normal (gravity wins)
            e.spAir = true;
            legStep(1280.f);
            if (std::getenv("XA_DEBUG")) std::printf("spider %d drops at %.1f %.1f %.1f\n", e.wi, e.pos.x, e.pos.y, e.pos.z);
            return;
        }
    }
    const CollisionWorld::Touch* t = nearest();
    e.pos = t->onPlane + t->n * e.spH;
    e.spUpT = t->n;
    e.spLastZ = e.pos.z;
    legStep(512.f);
    objectBlock(oldPos, oldUpT, oldYawT);
}

// Spider legs (0x78898 -> 0x7623c): with flag 2 (SPIDER) four, with flag 4 (SPIDER360) six keyframe tables give
// the angle words of the parts at VARIABLE_OFFSET 6, 12, ... (FussB2, FussA1, FussA2, FussB1, FussB3, FussA3).
// Records: {frame, alpha, beta, gamma}; between two records the angles are interpolated linearly along the
// shorter way round (0x73b74), a frame on a record takes it exactly.
void EnemySystem::spiderLegs(const Enemy& e, int16_t* vars, int& nVars) {
    struct Key { int f; int16_t a[3]; };
    static const Key kLegs[10][5] = {
        {{0, {0, 0, 284}}, {5, {-170, 0, 284}}, {30, {-170, 0, -284}}, {35, {0, 0, -284}}, {60, {0, 0, 284}}},     // 0x70f00
        {{0, {-170, 0, 284}}, {5, {0, 0, 284}}, {30, {0, 0, -284}}, {35, {-170, 0, -284}}, {60, {-170, 0, 284}}},  // 0x70f3c
        {{0, {170, 0, -284}}, {5, {0, 0, -284}}, {30, {0, 0, 284}}, {35, {170, 0, 284}}, {60, {170, 0, -284}}},    // 0x70f78
        {{0, {0, 0, -284}}, {5, {170, 0, -284}}, {30, {170, 0, 284}}, {35, {0, 0, 284}}, {60, {0, 0, -284}}},      // 0x70fb4
        {{0, {0, 0, 284}}, {5, {-170, 0, 284}}, {30, {-170, 0, -284}}, {35, {0, 0, -284}}, {60, {0, 0, 284}}},     // 0x70ff0
        {{0, {-170, 0, 284}}, {5, {0, 0, 284}}, {30, {0, 0, -284}}, {35, {-170, 0, -284}}, {60, {-170, 0, 284}}},  // 0x7102c
        {{0, {170, 0, -284}}, {5, {0, 0, -284}}, {30, {0, 0, 284}}, {35, {170, 0, 284}}, {60, {170, 0, -284}}},    // 0x71068
        {{0, {0, 0, -284}}, {5, {170, 0, -284}}, {30, {170, 0, 284}}, {35, {0, 0, 284}}, {60, {0, 0, -284}}},      // 0x710a4
        {{0, {0, 113, -284}}, {5, {0, 0, -284}}, {30, {0, 0, 284}}, {35, {0, 113, 284}}, {60, {0, 113, -284}}},    // 0x710e0
        {{0, {0, 0, -284}}, {5, {0, -113, -284}}, {30, {0, -113, 284}}, {35, {0, 0, 284}}, {60, {0, 0, -284}}},    // 0x7111c
    };
    nVars = 0;
    int first, count;
    if (e.spFlags & 2) { first = 0; count = 4; }
    else if (e.spFlags & 4) { first = 4; count = 6; }
    else return;
    const float f = std::clamp(e.spFrame, 0.f, 60.f);
    for (int l = 0; l < count; l++) {
        const Key* k = kLegs[first + l];
        int i = 0;
        while (i < 3 && f > float(k[i + 1].f)) i++;
        const float u = (f - float(k[i].f)) / float(k[i + 1].f - k[i].f);
        for (int c = 0; c < 3; c++) {
            int d = (int(k[i + 1].a[c]) - int(k[i].a[c])) & 0xfff;
            if (d > 0x800) d -= 0x1000;
            vars[3 * (l + 1) + c] = int16_t(int(std::lround(float(k[i].a[c]) + float(d) * u)) & 0xfff);
        }
    }
    nVars = 3 * (count + 1);
}

void EnemySystem::measureGround() {
    if (!coll_) return;
    CollisionWorld& coll = *coll_;
    HiddenScapeScope scapeScope(coll_);
    // All units are taken out of the collision world while measuring: alternative enemy sets place twins at
    // the same spot, whose roofs would otherwise count as ground (moving units then sank into the terrain).
    std::vector<std::pair<int, bool>> was;
    for (const Enemy& e : enemies_)
        if (e.cat == Ground || e.kind == KSpider) { was.push_back({e.wi, coll.ownerEnabled(e.wi)}); coll.setOwnerEnabled(e.wi, false); }
    for (Enemy& e : enemies_) {
        if (e.groundMeasured || e.moving) continue;
        if (e.kind == KSpider) {
            // spiders stand on any surface: the one below their feet (along the placed body's -z)
            const Vec3 up = e.spHome;
            Vec3 hit; int owner = -1;
            if (coll.raycast(e.home + up * 0.5f, e.home - up * (e.radius * 2.f + 2.f), hit, owner)) {
                e.spH = std::clamp(dot(e.home - hit, up), 0.05f, e.radius);
                e.groundMeasured = true;
            }
            if (std::getenv("XA_DEBUG")) std::printf("spider %d up %.2f %.2f %.2f height %.2f radius %.2f found %d\n", e.wi, up.x, up.y, up.z, e.spH, e.radius, int(e.groundMeasured));
            continue;
        }
        if (e.cat != Ground || e.kind == KMover) continue;
        float g = coll.groundBelow(e.home + Vec3{0, 0, 30});
        e.groundMeasured = g > -1e8f;
        e.groundSnap = g > -1e8f && g <= e.home.z + 1.f;      // boats / units under bridges keep their height
        e.groundOffset = e.groundSnap ? std::min(e.home.z - g, 10.f) : 0.f;
        e.waterZ = g > -1e8f ? g : e.home.z;
        if (std::getenv("XA_DEBUG")) std::printf("ground unit %d offset %.2f (z %.1f ground %.1f)\n", e.wi, e.groundOffset, e.home.z, g);
    }
    for (auto [wi, en] : was) coll.setOwnerEnabled(wi, en);
}

// ESEL_ENEMY (dispatcher case 0x68), ESEL_AS_ENEMY (0x69), BARRACUDA_ENEMY (0x72): the shared vehicle
// routine 0x7635c with fixed arguments: speed 175 / 175 / 350, turn rate 6 / 6 / 12, decision cycle
// -1 / -1 / 15 ticks, flags 0 / 4 / 9. Flags: 1 bobs on the water, 2 turns away randomly when hit,
// 4 moves only along its path, 8 must stay in the water (boat).
// - With a path it waits until the player enters the PATHACTIVATIONAREA box of the first point (or a
//   hit wakes it) and drives it like the ground vehicles (decision cycle 8, double turn rate).
// - Off the path: player outside the ACTIVATIONAREA box and not woken by a hit: back to the start
//   position (it stops within 2 radii of it); otherwise the player is the target.
// - Every cycle ticks a new heading: at most 2000 HP it flees (target = 3 pos - 2 player, 50 ticks);
//   closer than ~32 units it backs away at extra speed (5 ticks). A cycle of -1 (ESEL) never picks a
//   new heading: it keeps driving straight on and only turns at obstacles.
// - Speed approaches the wanted value by 24/512 per tick; the heading turns by
//   diff * clamp(turn * ticks, 2, 480) / 512 per frame (0x73a10).
// - Blocked (object, steep ground, a boat leaving the water): the move is undone, the heading target
//   becomes the current heading + 12 per tick, and no new decision is taken for 200 (250) ticks.
// - BARRACUDA (8b997): while the player is in its SHOOTACTIVATIONAREA box (|dz| < half), a counter
//   runs per tick; at 5 and 15 a SMALL_ROCKET_ENEMY leaves launcher 1 and 2, the counter wraps after
//   1000 ticks. ESEL and ESEL_AS never shoot.
void EnemySystem::vehicleAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    bool hit = e.wasHit; e.wasHit = false;
    if (e.waitT > 0) { e.waitT -= ticks; if (e.waitT < 0) e.waitT = 0; return; }   // waypoint TIME
    float speed = e.vSpeed, rot = e.vTurn;
    Vec3 wp;
    const bool onPath = groundPath(e, player, ticks, speed, rot, wp);
    if (onPath && e.waitT > 0) return;
    if (!onPath && e.path && !e.path->empty() && !e.pathStarted && !e.hitAct) { e.state = Idle; return; }   // 0x7652e
    int cycle = onPath ? 8 : e.vCycle;
    Vec3 target = onPath ? wp : player;
    if (!onPath && !(playerAlive && inBox2(e.pos, player, e.actBox)) && !e.hitAct) {
        if (std::fabs(e.pos.x - e.home.x) < 2 * e.radius && std::fabs(e.pos.y - e.home.y) < 2 * e.radius) { e.state = Idle; return; }   // 0x7699c
        target = e.home;
    }
    e.state = onPath ? OnPath : Attack;
    if (!onPath && (e.vFlags & 2) && hit) e.gTimer = 0;                    // 0x76a4f / 0x77c0b: a hit forces a decision
    if (e.gTimer <= 0 && cycle != -1) {
        float t = float(cycle);
        if (!onPath) {
            const Combat::Target* tg = combat_->target(e.wi);
            if (tg && tg->hp <= 2000.f) { target = e.pos * 3.f - player * 2.f; t = 50; }             // run away
            Vec3 d = e.pos - player; d.z = 0;
            float qx = d.x / unitScale_ / 32.f, qy = d.y / unitScale_ / 32.f, q = (qx * qx + qy * qy) / 256.f;
            if (q < 4000.f) { target = e.pos + d * 2.f; speed += (4000.f - q) * 2.f * speed / 4000.f; t = 5; }   // too close
        }
        e.gTimer = t;
        Vec3 dd = target - e.pos; dd.z = 0;
        if (dot(dd, dd) > 1e-6f) e.gTargetYaw = yawOf(dd);
        if (!onPath && (e.vFlags & 2) && hit) {
            float off = float(int(nextRnd() % 2001) + 1000) * ((nextRnd() & 1) ? 1.f : -1.f);
            e.gTargetYaw = wrapAngle(e.gTargetYaw + off * kU);
            e.gTimer = float(int(nextRnd() % 193) + 64);
        }
    }
    e.spSpeed += (speed - e.spSpeed) * std::min(1.f, ticks * 24.f / 512.f);
    if (e.kind == KSpider) {
        if (cycle != -1) { e.gTimer -= ticks; if (e.gTimer < 0) e.gTimer = 0; }
        spiderMove(e, e.spSpeed, ticks);
        return;
    }
    {
        float k = std::clamp(rot * kOrigFrame, 2.f, 480.f) / 512.f;                                     // 0x73a10 per original frame
        float f = 1.f - std::pow(1.f - std::min(k, 1.f), ticks / kOrigFrame);
        e.yaw = wrapAngle(e.yaw + wrapAngle(e.gTargetYaw - e.yaw) * f);
    }
    if (cycle != -1) { e.gTimer -= ticks; if (e.gTimer < 0) e.gTimer = 0; }
    Vec3 fwd{-std::sin(e.yaw), std::cos(e.yaw), 0};
    const float moveSpeed = e.inWater ? e.spSpeed * 2.f / 3.f : e.spSpeed;                // 0x76d7c: water look -> 2/3
    Vec3 step = (!(e.vFlags & 4) || onPath) ? fwd * (moveSpeed * ticks * unitScale_) : Vec3{};
    const bool land = !(e.vFlags & 8) && e.groundSnap;                                     // stands on GROUND objects
    auto waterUnder = [&](Vec3 p) { return coll_->surfaceBelow(p + Vec3{0, 0, e.radius}, CollisionWorld::KWater, nullptr) > -1e8f; };   // WATER list 0x21c56c
    // blocked (0x771e1 / 0x77296 / 0x77422): the move is undone, the heading target becomes the old one +-12 per
    // tick and no decision is taken for 200 (objects, units) or 250 ticks (steep ground, a boat leaving the water)
    const float oldTarget = e.gTargetYaw;
    auto blocked = [&](float waitTicks, float sign = 1.f) {
        e.gTargetYaw = wrapAngle(oldTarget + sign * 12.f * ticks * kU);
        e.gTimer = waitTicks;
    };
    if (dot(step, step) > 1e-10f) {
        if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
        Vec3 next = e.pos + step;
        // everything within half the radius of the new position (0x771a0): objects (0x21c57c) +12, friendly
        // buildings (0x21c5a4) and units (0x21c578) -12; a tree (0x21c580) is knocked down (its hit points
        // become 0, 0x7732e) and does not stop the vehicle. GROUND and WATER are what it drives on.
        static thread_local std::vector<CollisionWorld::Touch> vt;
        coll_->touching(next, e.radius * 0.5f, vt);
        bool object = false, unit = false; int tree = -1;
        for (const auto& t : vt) {
            const int k = coll_->ownerKind(t.owner);
            if (t.owner < 0 || k == CollisionWorld::KGround || k == CollisionWorld::KWater) continue;
            if (k == CollisionWorld::KTree) { tree = t.owner; continue; }
            const ObjectType* ty = types_->get(world_->objects()[size_t(t.owner)].objClass);
            if ((ty && ty->behavior == "BUILDING_FRIENDLY") || byWorld_[size_t(t.owner)] >= 0) unit = true;
            else object = true;
        }
        if (std::getenv("XA_VEH") && (object || unit)) std::printf("vehicle %d blocked by %s\n", e.wi, object ? "object" : "unit");
        if (!object && !unit && movingUnitNear(e, next, e.radius * 0.5f)) unit = true;          // moving units are out of the collision world
        if (object) blocked(200);
        else if (unit) blocked(200, -1.f);
        else if ((e.vFlags & 8) && !waterUnder(next)) blocked(250);                          // boat: no water ahead (0x77104)
        else if (Vec3 gn; land && coll_->surfaceBelow(next + Vec3{0, 0, 5.f}, CollisionWorld::KGround, &gn) > -1e8f && gn.z < 10000.f / 16384.f) blocked(250);   // 0x77067: too steep
        else { e.pos = next; e.anim += ticks; }
        if (tree >= 0 && !object && !unit) {
            if (const Combat::Target* tt = combat_->target(tree); tt && tt->alive) {
                if (std::getenv("XA_DEBUG")) std::printf("vehicle %d knocks down tree %d\n", e.wi, tree);
                combat_->kill(tree, next);
            }
        }
    }
    if (land && e.moving) {
        // 0x76ee6: the highest GROUND plane holds it, its normal is the tilt target (16 per tick); none: it
        // falls 100 per tick and dies after 20 radii (0x77172). In WATER (0x770c2) it takes its WATER_TYP look.
        Vec3 gn{0, 0, 1};
        float g = coll_->surfaceBelow(e.pos + Vec3{0, 0, 5.f}, CollisionWorld::KGround, &gn);
        if (g > -1e8f) { e.spUpT = gn; e.spLastZ = g; }
        else {
            e.pos.z -= 100.f * ticks * unitScale_;
            if (e.spLastZ - e.pos.z > 20.f * e.radius) {
                if (std::getenv("XA_DEBUG")) std::printf("vehicle %d fell to death\n", e.wi);
                combat_->kill(e.wi, e.pos);
                return;
            }
        }
        tiltTowards(e, e.spUpT, 16.f, ticks);
        float w = coll_->surfaceBelow(e.pos + Vec3{0, 0, e.radius}, CollisionWorld::KWater, nullptr);
        const bool wet = w > -1e8f && w >= (g > -1e8f ? g : e.pos.z - e.groundOffset);
        if (wet != e.inWater && std::getenv("XA_DEBUG")) std::printf("vehicle %d %s water\n", e.wi, wet ? "enters" : "leaves");
        e.inWater = wet;
        if (std::getenv("XA_VEH") && int(e.anim / 120.f) != int((e.anim + ticks) / 120.f))
            std::printf("vehicle %d pos %.1f %.1f %.1f ground %.1f water %.1f up %.2f %.2f %.2f\n", e.wi, e.pos.x, e.pos.y, e.pos.z, g, w, e.spUp.x, e.spUp.y, e.spUp.z);
    }
    if (e.vFlags & 1) {                                                                                  // bob (sin / rand(600..800) per tick)
        e.bobPhase += 12.f * ticks;
        float s = std::sin(e.bobPhase * kU) * 16384.f / float(600 + int(nextRnd() % 201));
        e.bobZ = s * ticks * unitScale_;
        e.pos.z = e.home.z + e.bobZ;
        if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    }
    // BARRACUDA rockets (8ba03)
    if (e.vRockets && playerAlive && e.shootRange > 0 && !e.noShootPoint) {
        Vec3 d = player - e.pos;
        if (std::fabs(d.x) < e.shootRange && std::fabs(d.y) < e.shootRange && std::fabs(d.z) < e.shootRange * 0.5f) {
            int before = e.rocketCnt;
            e.rocketCnt += std::max(1, int(ticks + 0.5f));
            for (int n : {5, 15}) {
                if (before < n && e.rocketCnt >= n) {
                    Vec3 side{std::cos(e.yaw), std::sin(e.yaw), 0};
                    Vec3 muzzle = e.pos + Vec3{0, 0, 2.f} + side * (n == 5 ? -1.f : 1.f);
                    combat_->fireEnemy(muzzle, normalize(player - muzzle), e.shotSpeed * 0.7f, true, e.shotDamage);
                }
            }
            if (e.rocketCnt > 1000) e.rocketCnt = 0;
        }
    }
}

// 0x57704: an object of the TYP is alive, or still waiting to appear (APPEAR_WHEN_DESTROYED lists
// 0x1a5da8 / 0x1a5168); objects that cannot be destroyed never go away
bool EnemySystem::wakeObjectsLeft(const Enemy& e) const {
    if (typePresent && !e.wakeType.empty()) return typePresent(e.wakeType);
    for (int w : e.wakeBy) {
        const Combat::Target* t = combat_->target(w);
        if (t && t->isTarget) { if (t->alive) return true; }
        else if (renderer_->instanceForWorldObject(w)) return true;
    }
    return false;
}

// CONNECT_TO_FLYING (0x70b12): every child keeps its rest offset to the parent, turned by the parent's
// change of angles, and adds that change to its own angles. Only the yaw of the mothership changes.
void EnemySystem::applyRigid(Enemy& ship) {
    const float dYaw = wrapAngle(ship.yaw - ship.homeYaw);
    const Mat4 R = Mat4::rotZ(dYaw);
    const Mat4 M = Mat4::translate(ship.pos) * R * Mat4::translate(ship.home * -1.f);
    // collision triangles follow in steps (re-bucketing a big hull every frame is costly)
    Vec3 dp = ship.pos - ship.collPos;
    bool moveColl = !ship.collInit || dot(dp, dp) > 0.25f * 0.25f || std::fabs(wrapAngle(ship.yaw - ship.collYaw)) > 0.002f;
    if (moveColl) { ship.collInit = true; ship.collPos = ship.pos; ship.collYaw = ship.yaw; }
    if (Renderer::Instance* in = renderer_->instanceForWorldObject(ship.wi)) { in->pos = ship.pos; in->rot = R * ship.baseRot; }
    if (moveColl) coll_->transformOwner(ship.wi, M);
    for (const Attach& a : ship.attached) {
        Vec3 p = M.transformPoint(a.home);
        if (Renderer::Instance* in = renderer_->instanceForWorldObject(a.wi)) { in->pos = p; in->rot = R * a.baseRot; }
        if (moveColl) coll_->transformOwner(a.wi, M);
        if (byWorld_[size_t(a.wi)] >= 0) {
            Enemy& c = enemies_[size_t(byWorld_[size_t(a.wi)])];
            c.pos = p; c.prevPos = p; c.homeYaw = wrapAngle(a.baseYaw + dYaw);
        }
    }
}

// PLUTO_ENEMY (dispatcher case 0x57, LEVEL06 BIG_END_SHIP, the mothership). The objects of its
// ACTIVATION_WHEN_DESTROYED TYP (FUEHLER) keep it "dormant" (event 7 sets [0x84] = 1 while any of
// them exists, 0x59d1c); only then does it act (0x877d8): it turns by ticks/2 (4096 per turn),
// rides between the heights of its first two path points (vertical speed +-100 per tick, accelerate /
// brake 50; the target point switches within 1000 units) and strikes the player with lightning:
// each tick a random ray from the player (up to +-0x40000 per axis) that hits the hull starts a
// 32-tick strike from that point (jittered by +-2048 per frame); within 128000 units the player takes
// FLASH1 cause_damage per tick and FLASH1 sprites mark the beam every 12800 units.
// Destroyed (8947b): above 0x27100 it shakes (position +-0x400 per frame) under random explosions
// (chance (rand%450 > t), t counting down from 450), then falls (vertical speed towards -10000 per
// frame, accelerate 300). Below that height t counts up: from 1024 four FLASH2 columns rise at
// 0x5dc0 units around it, explosions grow, FINALFACE sprites appear every 30 ticks; at 2048 it is gone.
void EnemySystem::plutoAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    const int steps = std::max(1, int(ticks + 0.5f));
    auto rr = [&](int mod, int mul, int sub) { return float(int(nextRnd() % unsigned(mod)) * mul - sub) * unitScale_; };
    e.state = Attack;
    if (e.dState == 0) {
        if (wakeObjectsLeft(e)) {
            e.yaw = wrapAngle(e.yaw - ticks * 0.5f * kU);                    // its angle grows: clockwise
            float tz = e.pIdx ? e.pzB : e.pzA;
            float want = tz > e.pos.z ? 100.f : -100.f;
            if (std::fabs(tz - e.pos.z) < 1000.f * unitScale_) e.pIdx ^= 1;
            e.pVz = approachAxis(e.pVz, want, 50.f, 50.f, ticks);
            e.pos.z += e.pVz * ticks * unitScale_;
            // lightning
            if (e.beamT < 0 && playerAlive) {
                for (int k = 0; k < steps && e.beamT < 0; k++) {
                    Vec3 off{rr(1024, 512, 0x40000), rr(1024, 512, 0x40000), rr(1024, 512, 0x40000)};
                    Vec3 hp; int owner = -1;
                    if (coll_->raycast(player, player + off, hp, owner) && owner == e.wi) { e.beamSrc = hp; e.beamT = 32; }
                }
            } else if (e.beamT >= 0) {
                Vec3 j = e.beamSrc + Vec3{rr(4096, 1, 2048), rr(4096, 1, 2048), rr(4096, 1, 2048)};
                Vec3 hp; int owner = -1;
                if (coll_->raycast(player, player + (j - player) * 2.f, hp, owner) && owner == e.wi) e.beamSrc = hp;
                e.beamT -= ticks;
            }
            if (e.beamT >= 0 && playerAlive) {
                Vec3 d = e.beamSrc - player; float len = std::sqrt(dot(d, d));
                if (len > 128.f) e.beamT = -1;
                else {
                    combat_->damagePlayer(e.flashDmg * ticks);
                    for (float s = 12.8f; s < len; s += 12.8f) combat_->firePuff(player + d * (s / len), 2.f);
                }
            }
        } else e.beamT = -1;
    } else if (e.dState == 1 || e.dState == 2) {
        if (e.dState == 1 && e.pos.z <= 160.f) { e.dState = 2; e.dT = 0; e.plumeZ = e.pos.z; e.dRing = 0; }
        if (e.dState == 1) {
            for (int k = 0; k < steps; k++)
                if (float(int(nextRnd() % 450)) > e.dT)
                    combat_->explodeAt(e.pos + Vec3{rr(0x4002, 35, 0x46023), rr(0x4002, 35, 0x46023), rr(0x4002, 10, 0x1400a)},
                                       std::clamp((50000.f - 100.f * e.dT) * unitScale_ * 0.5f, 3.f, 40.f), true);
            e.pos = e.pos + Vec3{rr(0x800, 1, 0x400), rr(0x800, 1, 0x400), rr(0x800, 1, 0x400)};
            e.dT -= ticks;
            float want = 0;
            if (e.dT < 0) { e.dT = 0; want = -10000.f; }
            e.dVz = approachAxis(e.dVz, want, 300.f, 300.f, ticks);
            e.pos.z += e.dVz * (ticks / kOrigFrame) * unitScale_;          // per original frame
            if (e.pos.z <= 160.f) { e.pos.z = 160.f; e.dState = 2; e.dT = 0; e.plumeZ = e.pos.z; e.dRing = 0; }
        } else {
            e.dT += ticks;
            // 0x89792: from t = 1024 four FLASH2 columns stand at 0x5dc0 * 16 units (384) around it, a quarter turn
            // apart from its heading, at a height rising 1250 per tick; angles 0
            if (e.dT > 1024.f) {
                e.plumeZ += 1250.f * ticks * unitScale_;
                for (int k = 0; k < 4; k++) {
                    float a = e.yaw + float(k) * kTwoPi / 4.f;
                    Vec3 at{e.pos.x - std::sin(a) * 384.f, e.pos.y + std::cos(a) * 384.f, e.plumeZ};
                    if (flash2Model_ >= 0) extras.push_back({flash2Model_, flash2Cls_, at, Mat4()});
                    if (k == 0 && std::getenv("XA_DEBUG") && int((e.dT - ticks) / 120.f) != int(e.dT / 120.f))
                        std::printf("PLUTO t %.0f: FLASH2 at %.0f %.0f %.0f (ship %.0f %.0f %.0f), %zu effect objects\n", e.dT, at.x, at.y, at.z, e.pos.x, e.pos.y, e.pos.z, extras.size());
                    else combat_->firePuff(at, 10.f);
                }
            }
            for (int k = 0; k < steps; k++)
                if (float(int(nextRnd() % 1024)) <= e.dT || int(nextRnd() % 1024) < 100)
                    combat_->explodeAt(e.pos + Vec3{rr(0x4002, 35, 0x46023), rr(0x4002, 35, 0x46023), rr(0x4002, 10, 0x1400a)},
                                       std::clamp((50000.f + 100.f * e.dT) * unitScale_ * 0.5f, 3.f, 60.f), true);
            // 0x89a96: piece i (0..31) of FINALFACE shows while 30 * (i + 1) < t, at the ship's centre, turned by
            // angles that step by 0x33b / 0xa43 / 0xe2b per piece (angle 0: mod 1300 - 400) plus rand % 20 each frame
            for (int i = 0; i < 32 && float(30 * (i + 1)) < e.dT; i++) {
                const int n = i + 1;
                int16_t ang[5] = {0, 0, int16_t((0x33b * n) % 1300 - 400 + int(nextRnd() % 20)), int16_t((0xa43 * n) % 4096 + int(nextRnd() % 20)),
                                  int16_t((0xe2b * n) % 4096 + int(nextRnd() % 20))};
                if (faceModel_ >= 0) extras.push_back({faceModel_, faceCls_, e.pos, worldObjectRotation(ang)});
                else if (n > e.dRing) { e.dRing = n; combat_->explodeAt(e.pos, 20.f, true); }
            }
            if (e.dT > 2048.f) { e.dState = 3; combat_->finishWreck(e.wi, e.pos); }
        }
    }
    applyRigid(e);
}

// TERROR_GOD_ENEMY (dispatcher case 0x5f): counter tgT runs 0..terror_not_trans_t, then jumps to
// -terror_trans_t; below 0 the alternative (cloaked) model is shown - drawn here as a flicker and
// immune to hits. Near both ends of the visible phase it flickers randomly. The gun fires only
// while 0 < tgT < not_trans_t - 100. It bobs: sinks (-100/tick target) for 200 ticks, then rises
// until above its start height. Rockets: a counter runs to terror_rocket_t + 200 and fires at
// rocket_t - 70, rocket_t and rocket_t + 70 when the player is inside the SHOOTACTIVATIONAREA box.
void EnemySystem::terrorAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    e.state = Attack;
    e.tgT += ticks;
    if (e.tgT > float(e.tgNotTransT)) e.tgT = -float(e.tgTransT);
    bool cloak = e.tgT < 0;
    bool look = cloak;
    if (e.tgT > 0 && e.tgT < 100 && float(nextRnd() % 100) > e.tgT) look = true;
    if (e.tgT > float(e.tgNotTransT) - 100 && float(nextRnd() % 100) < e.tgT - float(e.tgNotTransT - 100)) look = true;
    e.cloaked = cloak;
    combat_->setShielded(e.wi, cloak);
    {   // the whole body turns to the player (it has no separate turret)
        Vec3 dp = player - e.pos;
        if (playerAlive && !cloak) e.yaw = turnToward(e.yaw, yawOf(dp), std::max(e.turretRate, 4.f), ticks);
    }
    if (Renderer::Instance* in = renderer_->instanceForWorldObject(e.wi)) in->hidden = look && (int(e.tgT * 0.5f) & 1);
    // bobbing
    if (e.tgBob < 0 && e.pos.z > e.startZ) e.tgBob = 200;
    float wantVz = e.tgBob < 0 ? 100.f : -100.f;
    e.tgVz = approachAxis(e.tgVz, wantVz, std::max(e.accel, 1.f), std::max(e.brake, 1.f), ticks);
    e.tgBob -= ticks;
    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
    e.pos.z += e.tgVz * ticks * unitScale_;
    // rockets
    const float prev = e.tgRocket;
    if (!cloak || e.tgRocket > float(e.tgRocketT)) e.tgRocket += ticks;
    if (e.tgRocket > float(e.tgRocketT + 200)) e.tgRocket = 0;
    Vec3 d = player - e.pos;
    bool inShoot = std::fabs(d.x) < e.shootRange && std::fabs(d.y) < e.shootRange && std::fabs(d.z) < e.shootRange / 2;
    for (int k = -1; k <= 1; k++) {
        float at = float(e.tgRocketT + 70 * k);
        if (prev < at && e.tgRocket >= at && playerAlive && inShoot) {
            Vec3 mz = e.pos + Vec3{0, 0, e.radius * 0.5f};
            Vec3 hp; int owner = -1;
            if (!coll_->raycast(mz, player, hp, owner)) combat_->fireEnemy(mz, normalize(player - mz), e.shotSpeed * 0.7f, true, 1000.f);
        }
    }
}

// IMPERATOR_ENEMY (dispatcher case 100): while the player is inside the SHOOTACTIVATIONAREA box a
// counter runs; imper_smoke_t..imper_beam_t: smoke at the barrel; imper_beam_t..imper_beam_end_t:
// the beam (IMPERATOR_BEAM) along the gun; after imper_end_t it starts again. The normal gun only
// fires before imper_beam_t.
void EnemySystem::imperatorAI(Enemy& e, Vec3 player, bool playerAlive, float dt) {
    const float ticks = dt * kTick;
    e.state = Attack;
    e.beamOn = false;
    Vec3 d = player - e.pos;
    bool inShoot = playerAlive && std::fabs(d.x) < e.shootRange && std::fabs(d.y) < e.shootRange && std::fabs(d.z) < e.shootRange / 2;
    if (!inShoot) return;
    const float prev = e.imC;
    e.imC += ticks;
    Vec3 dir{-std::sin(e.aimYaw) * std::cos(e.aimPitch), std::cos(e.aimYaw) * std::cos(e.aimPitch), std::sin(e.aimPitch)};
    Vec3 mz = e.pos + Vec3{0, 0, std::max(1.5f, e.radius * 0.3f)} + dir * std::max(2.f, e.radius * 0.5f);
    if (e.imC > float(e.imSmokeT) && e.imC < float(e.imBeamT)) {
        if (int(prev / 8) != int(e.imC / 8)) combat_->smokePuff(mz, 2.f);
    } else if (e.imC >= float(e.imBeamT) && e.imC < float(e.imBeamEndT)) {
        // 0x8d505: the beam object (IMPERATOR_BEAM) is put into the effect list each frame, but only while
        // nothing (friendly building, unit, ground) blocks the line from the barrel to the player (0x7a714).
        // The player's sphere is tested against the effect list once per frame (0x8ea6f); touching the
        // beam costs the beam type's DEF cause_damage (0x1ece74 + 33*4, 0x56e28) per frame.
        Vec3 hp; int owner = -1;
        {
            Vec3 a = mz, toP = normalize(player - mz);
            for (int k = 0; k < 16 && coll_->raycast(a, player, hp, owner); k++) {
                if (owner >= 0 && owner != e.wi) {
                    const ObjectType* ty = types_->get(world_->objects()[size_t(owner)].objClass);
                    if (coll_->isGround(owner) || byWorld_[size_t(owner)] >= 0 || (ty && ty->behavior == "BUILDING_FRIENDLY")) return;
                }
                a = hp + toP * 0.05f;   // other objects do not block it
            }
        }
        e.beamOn = true;
        float len = std::min(e.shootRange * 1.5f, 600.f);
        Vec3 end = mz + dir * len;
        if (coll_->raycast(mz, end, hp, owner)) end = hp;
        Vec3 seg = end - mz; float sl = std::sqrt(dot(seg, seg));
        for (float t = 0; t < sl; t += 6.f) combat_->firePuff(mz + seg * (t / std::max(sl, 1e-3f)), 1.5f);
        float tproj = std::clamp(dot(player - mz, seg) / std::max(sl * sl, 1e-3f), 0.f, 1.f);
        Vec3 closest = mz + seg * tproj, off = player - closest;
        if (dot(off, off) < 5.f * 5.f) {
            combat_->damagePlayer(beamDamage_ * ticks / kOrigFrame);
            beamTouch_ = true; beamTouchAt_ = player;           // 0x8eade: sound 0x17 (BMBLITZ.WAV) at the hit
        }
    }
    if (e.imC > float(e.imEndT)) e.imC = 0;
}

// MOVINGWALL / BLOCK_ENEMY (0x85854): when the player enters the PATHACTIVATIONAREA box (path of
// at least 2 points) the object moves along its path at 'speed' units per tick, point to point;
// at the end it turns back if the last point has flag 8 (ping-pong), otherwise it stays. The
// collision triangles move with it (lifts, gates, trains, crushing ceilings).
void EnemySystem::moverAI(Enemy& e, Vec3 player, float dt) {
    const float ticks = dt * kTick;
    if (!e.path || e.path->size() < 2) return;
    const auto& P = *e.path;
    if (e.mvState == 0 && inBox2(e.pos, player, e.pathActivation)) { e.mvState = 1; e.mvIdx = 0; e.mvDir = 1; }
    if (e.mvState != 1) return;
    Vec3 wp{P[size_t(e.mvIdx)].pos[0] * unitScale_, P[size_t(e.mvIdx)].pos[1] * unitScale_, P[size_t(e.mvIdx)].pos[2] * unitScale_};
    Vec3 d = wp - e.pos;
    float dist = std::max(std::sqrt(dot(d, d)), 1e-4f);
    float step = e.rawSpeed * ticks * unitScale_;
    Vec3 next;
    if (dist < step) {
        next = wp;
        bool atEnd = (e.mvDir == 1 && e.mvIdx == int(P.size()) - 1) || (e.mvDir == -1 && e.mvIdx == 0);
        if (!atEnd) e.mvIdx += e.mvDir;
        else if (P.back().flag & 8) e.mvDir = -e.mvDir;
        else e.mvState = 2;
    } else next = e.pos + d * (step / dist);
    Vec3 delta = next - e.pos;
    if (dot(delta, delta) > 0) {
        coll_->moveOwner(e.wi, delta);
        e.pos = next;
        if (Renderer::Instance* in = renderer_->instanceForWorldObject(e.wi)) in->pos = e.pos;
    }
    e.state = Attack;
}

void EnemySystem::sounds(Audio& audio, Vec3 listener, float dt, bool on) {
    // 0x571cc: a sound id already in a slot only moves, so the beam hit sound runs on while the player touches it
    if (on && beamTouch_) {
        if (!beamVoice_ || !audio.playing(beamVoice_)) beamVoice_ = audio.playAt(audio.sound("BMBLITZ.WAV"), beamTouchAt_, 1.f);
    }
    // 0x39757: units within the view range (per axis) whose sound is set; at most 16 loops run (0x571cc)
    const float range = 450.f;
    int running = 0;
    for (Loop& l : loops_) {
        int ei = byWorld_[size_t(l.wi)];
        Vec3 pos = ei >= 0 ? enemies_[size_t(ei)].pos : l.last;
        bool moving = dt > 0 && dot(pos - l.last, pos - l.last) > 1e-6f;
        l.last = pos;
        bool want = on && audio.ok();
        if (want) {
            const auto* t = combat_->target(l.wi);
            if (t && !t->alive) want = false;
            if (ei >= 0 && hiddenFromPlayer(enemies_[size_t(ei)])) want = false;
            if (auto* in = renderer_->instanceForWorldObject(l.wi); in && in->hidden) want = false;
            if (std::fabs(pos.x - listener.x) > range || std::fabs(pos.y - listener.y) > range || std::fabs(pos.z - listener.z) > range) want = false;
            if (l.onlyMoving) { if (moving) l.stillT = 0; else l.stillT += dt; if (l.stillT > 0.2f) want = false; }
            if (running >= 16) want = false;
        }
        if (!want) { if (l.voice) { audio.stop(l.voice); l.voice = 0; } continue; }
        running++;
        if (l.snd < 0) l.snd = audio.sound(l.name);
        if (l.snd < 0) continue;
        if (!l.voice) { l.voice = audio.playLoopAt(l.snd, pos, 1.f); if (std::getenv("XA_SNDDEBUG")) std::printf("loop sound %s for %d (voice %d)\n", l.name.c_str(), l.wi, l.voice); }
        else audio.setVoicePos(l.voice, pos, 1.f);
    }
}

bool EnemySystem::hiddenFromPlayer(const Enemy& e) const {
    if (!e.enclosed) return false;                           // inside the rock: only shown when the player is in there too
    Vec3 d = e.pos - lastPlayer_;
    return dot(d, d) > 150.f * 150.f;
}

void EnemySystem::update(float dt, Vec3 player, bool playerAlive) {
    HiddenScapeScope scapeScope(coll_);
    extras.clear();
    beamTouch_ = false;
    if (dt > 0) { Vec3 v = (player - lastPlayer_) * (1.f / dt); if (dot(v, v) < 1e6f) playerVel_ = playerVel_ + (v - playerVel_) * std::min(1.f, dt * 15.f); }
    lastPlayer_ = player;
    static const char* trace = std::getenv("XA_TRACE");          // testing: XA_TRACE=worldIndex prints that unit once per second
    static float traceT = 0;
    if (trace && (traceT += dt) > 1.f) {
        traceT = 0;
        int wi = std::atoi(trace);
        if (wi >= 0 && size_t(wi) < byWorld_.size() && byWorld_[size_t(wi)] >= 0) {
            const Enemy& e = enemies_[size_t(byWorld_[size_t(wi)])];
            Renderer::Instance* in = renderer_->instanceForWorldObject(wi);
            float g = coll_->groundBelow(e.pos + Vec3{0, 0, 5});
            std::printf("trace as 0x%x idx %d node %d ms 0x%x pitch %.2f roll %.2f kind %d gs 0x%x yaw %.2f vel %.0f,%.0f alert %.0f inact %d ws %d wi %d active %d moving %d state %d pos %.1f %.1f %.1f ground %.1f hidden %d path %zu/%zu end %d\n", e.aState, e.aIdx, e.tNode, e.mState, e.pitch, e.roll, int(e.kind), e.gs, e.yaw, e.gVel.x, e.gVel.y, e.alertT, int(e.gInactive), int(e.ws), wi, int(e.active), int(e.moving), int(e.state),
                        e.pos.x, e.pos.y, e.pos.z, g, in ? int(in->hidden) : -1, e.pathIdx, e.path ? e.path->size() : 0, int(e.pathEnd));
        }
    }
    for (Enemy& e : enemies_) {
        if (!e.active) continue;
        if (e.crashing) {
            // ballistic fall with spin and a smoke trail until it hits the ground
            e.vel.z -= 30.f * dt;
            e.vel.x *= 1.f - 0.3f * dt; e.vel.y *= 1.f - 0.3f * dt;
            e.yaw += e.spin * dt;
            e.roll = std::min(e.roll + dt * 0.8f, 0.9f);
            Vec3 next = e.pos + e.vel * dt;
            Vec3 hit; int owner;
            float g = coll_->groundBelow(e.pos + Vec3{0, 0, 2});
            bool impact = coll_->raycast(e.pos, next, hit, owner) || (g > -1e8f && next.z <= g + 0.5f);
            e.smokeT -= dt;
            if (e.smokeT <= 0) { combat_->smokePuff(e.pos, 3.f); combat_->firePuff(e.pos, 2.f); e.smokeT = 0.06f; }
            if (impact) {
                e.crashing = false;
                Vec3 at = (g > -1e8f) ? Vec3{next.x, next.y, std::max(g, std::min(next.z, e.pos.z))} : next;
                if (coll_->raycast(e.pos, next, hit, owner)) at = hit;
                if (Renderer::Instance* in = renderer_->instanceForWorldObject(e.wi)) in->rot = Mat4::rotZ(e.yaw) * Mat4::rotZ(faceFlip(e));
                if (std::getenv("XA_DEBUG")) std::printf("crash impact %d at %.1f %.1f %.1f\n", e.wi, at.x, at.y, at.z);
                combat_->finishWreck(e.wi, at);
                continue;
            }
            e.pos = next;
            e.animRate = std::max(0.15f, e.animRate - 0.35f * dt);
            e.anim += dt * kTick * e.animRate;
            if (Renderer::Instance* in = renderer_->instanceForWorldObject(e.wi)) {
                in->animTicks = e.anim;
                in->pos = e.pos;
                in->rot = Mat4::rotZ(e.yaw) * Mat4::rotY(e.roll) * Mat4::rotX(-e.roll * 0.5f) * Mat4::rotZ(faceFlip(e));
            }
            continue;
        }
        if (e.kind == KPluto) { plutoAI(e, player, playerAlive, dt); continue; }   // also runs its death sequence
        if (e.dormant) {                                                            // ACTIVATION_WHEN_DESTROYED (0x59d1c)
            if (wakeObjectsLeft(e)) { e.state = Idle; continue; }
            e.dormant = false;
            if (std::getenv("XA_DEBUG")) std::printf("dormant %d wakes up\n", e.wi);
        }
        if (e.kind == KMover && e.attachedTo >= 0) continue;                        // rides on the mothership
        const Combat::Target* tg = combat_->target(e.wi);
        if (tg && (!tg->alive || !tg->active)) continue;
        // being hit puts the unit into attack mode for a while (it then always shoots)
        if (tg) { if (e.lastHp > 0 && tg->hp < e.lastHp) { e.alerted = e.cat == Air ? 0.f : 5.f; e.wasHit = true; Vec3 hd = player - e.pos; e.hitAct = true; e.hitActR = 1.5f * std::sqrt(dot(hd, hd)); } e.lastHp = tg->hp; }
        if (e.hitAct && !inBox2(e.pos, player, e.hitActR)) e.hitAct = false;
        if (e.alerted > 0) e.alerted -= dt;
        Vec3 toP = player - e.pos;
        float dist = std::sqrt(dot(toP, toP));

        const bool attacking = playerAlive && dist < e.shootRange * 1.5f;      // gun follows the player
        if (e.kind >= KBlitz && e.kind <= KBuggy) {
            groundAI(e, player, playerAlive, dt);
        } else if (e.kind == KDeathstorm) {
            deathstormAI(e, player, playerAlive, dt);
        } else if (e.kind == KMover) {
            moverAI(e, player, dt);
            continue;
        } else if (e.kind == KTerror) {
            terrorAI(e, player, playerAlive, dt);
        } else if (e.kind == KImperator) {
            imperatorAI(e, player, playerAlive, dt);
        } else if (e.kind == KSpider) {
            spiderAI(e, player, playerAlive, dt);
        } else if (e.kind == KVehicle) {
            vehicleAI(e, player, playerAlive, dt);
        } else if (e.kind == KMars) {
            marsAI(e, player, playerAlive, dt);
        } else if (e.kind == KAmbush) {
            ambushAI(e, player, playerAlive, dt);
        } else if (e.kind == KTunnel) {
            tunnelAI(e, player, playerAlive, dt);
        } else if (e.kind == KFloh || e.kind == KKiller) {
            mineAI(e, player, playerAlive, dt);
            if (!e.active || (tg && !tg->alive)) continue;
        } else if (e.kind == KMosquito) {
            mosquitoAI(e, player, playerAlive, dt);
            if (!e.active || (tg && !tg->alive)) continue;
        } else {
            // --- state changes
            if (e.state == Idle && e.cat != Static) {
                if (e.path && dist < std::max(e.pathActivation, e.activation)) e.state = OnPath;
                else if (!e.path && dist < e.activation && e.speed > 0) e.state = Attack;
            }
            if (e.state == OnPath && e.pathEnd && e.cat == Air) e.state = Attack;

            // --- movement
            Vec3 target = e.pos; float speed = 0;
            if (e.state == OnPath && e.path) {
                const PathPoint& pp = (*e.path)[e.pathIdx];
                target = {pp.pos[0] * unitScale_, pp.pos[1] * unitScale_, pp.pos[2] * unitScale_};
                speed = (pp.flag & 0x10) ? float(pp.speed) * kTick * unitScale_ : e.speed;   // SPEED only counts with flag 0x10 (RE)
                if (pp.flag & 1) speed *= 0.5f;
                if (pp.flag & 2) speed *= 2.f;
                Vec3 d = target - e.pos; if (e.cat == Ground) d.z = 0;
                if (dot(d, d) < 9.f) {
                    if (e.pathIdx + 1 < e.path->size()) e.pathIdx++;
                    else { e.pathEnd = true; speed = 0; }
                }
                if (e.pathEnd) speed = 0;
            } else if (e.state == Attack && playerAlive) {
                if (e.cat == Air) {
                    wespeThink(e, player, dist, dt);
                    speed = e.speed;                       // movement is done by wespeMove below
                } else if (dist > e.goodR) { target = player; speed = e.speed; }
            }
            if (e.cat == Air && e.state == Attack && playerAlive) {
                if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
                wespeMove(e, player, dt);
            } else if (speed > 0) {
                if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
                Vec3 d = target - e.pos;
                float wantYaw = std::atan2(-d.x, d.y);
                float dy = wrapAngle(wantYaw - e.yaw);
                float maxTurn = std::max(e.turn, 0.6f) * dt;
                e.yaw += std::clamp(dy, -maxTurn, maxTurn);
                Vec3 fwd{-std::sin(e.yaw), std::cos(e.yaw), 0};
                e.pos = e.pos + fwd * (speed * dt * std::max(0.2f, std::cos(dy)));
                e.drift = fwd * speed;
                if (e.cat == Air) {
                    float want = target.z;
                    float g = coll_->groundBelow(e.pos + Vec3{0, 0, 200});
                    if (g > -1e8f) want = std::max(want, g + e.hover);
                    e.pos.z += std::clamp(want - e.pos.z, -speed * dt, speed * dt);
                }
            } else if (attacking && e.state == Attack && e.cat != Static && !e.hasTurret) {
                // standing still: turn the hull towards the player
                float wantYaw = std::atan2(-toP.x, toP.y);
                e.yaw += std::clamp(wrapAngle(wantYaw - e.yaw), -std::max(e.turn, 0.6f) * dt, std::max(e.turn, 0.6f) * dt);
                if (e.moving || e.cat == Air) {
                    if (!e.moving) { e.moving = true; coll_->setOwnerEnabled(e.wi, false); combat_->setDynamic(e.wi, true); }
                }
            }
        }
        if (e.cat == Ground && e.moving && e.kind != KSpider) {
            // probe from just above the unit so bridges and overhangs are not taken as ground
            const bool landVehicle = (e.kind == KVehicle && !(e.vFlags & 8)) || (e.kind >= KBlitz && e.kind <= KBuggy);   // only GROUND objects carry it (0x21c568, 0x7ac34)
            float g = !e.groundSnap ? -1e9f : landVehicle ? coll_->surfaceBelow(e.pos + Vec3{0, 0, 5}, CollisionWorld::KGround, nullptr)
                                                          : coll_->groundBelow(e.pos + Vec3{0, 0, 5});
            if (g > -1e8f) e.pos.z = g + e.groundOffset + e.bobZ;
        }
        if (dt > 0) e.vel = (e.pos - e.prevPos) * (1.f / dt);
        e.prevPos = e.pos;
        // --- gun aim (RE 0x7c4d0). The gun is aimed at the intercept point of a shot with the
        // player's current velocity (0x7c1fc). Turrets ("turm"/"rohr") approach that direction by
        // diff * clamp(turm_rotate_speed * ticks, 2, 480) / 512 per frame (0x73a10); units without a
        // turret aim with their hull.
        // hullYaw: where the hull points (shots of units without a turret); modelYaw: the angle the model is drawn
        // with (turrets aim relative to it)
        const float hullYaw = (e.moving || e.cat == Air) ? e.yaw : e.homeYaw + faceFlip(e);
        const float modelYaw = (e.moving || e.cat == Air) ? e.yaw + faceFlip(e) : e.homeYaw;
        Vec3 aimPoint = player;
        {
            Vec3 mz = e.pos + Vec3{0, 0, 2.f};
            float sp = std::max(e.shotSpeed, 1.f);
            Vec3 rel = player - mz;
            // solve |rel + v t| = sp t
            float a = dot(playerVel_, playerVel_) - sp * sp, b = 2.f * dot(rel, playerVel_), c = dot(rel, rel);
            float t = -1;
            if (std::fabs(a) < 1e-4f) t = b != 0 ? -c / b : -1;
            else { float disc = b * b - 4 * a * c; if (disc >= 0) { float r1 = (-b - std::sqrt(disc)) / (2 * a), r2 = (-b + std::sqrt(disc)) / (2 * a); t = r1 > 0 ? (r2 > 0 ? std::min(r1, r2) : r1) : r2; } }
            if (t > 0 && t < 10.f) aimPoint = player + playerVel_ * t;
        }
        if (attacking) {
            Vec3 mz = e.pos + Vec3{0, 0, 2.f};
            Vec3 tp = aimPoint - mz;
            float wantYaw = std::atan2(-tp.x, tp.y);
            float wantPitch = std::atan2(tp.z, std::sqrt(tp.x * tp.x + tp.y * tp.y));
            if (e.hasTurret) {
                float k = std::min(1.f, std::clamp(e.turretRate * dt * kTick, 2.f, 480.f) / 512.f);
                e.aimYaw += wrapAngle(wantYaw - e.aimYaw) * k;
                // minalpha / maxalpha limit the gun; out of reach sets 0x1000 (0x7cbf6), which makes the PANTHER move
                e.cantAim = wantPitch < e.minAlpha * kU || wantPitch > e.maxAlpha * kU;
                e.aimPitch += (std::clamp(wantPitch, e.minAlpha * kU, e.maxAlpha * kU) - e.aimPitch) * k;
            } else {
                e.aimYaw = hullYaw;
                e.aimPitch += (wantPitch - e.aimPitch) * std::min(1.f, dt * 4.f);
            }
        }
        if (e.cat == Air && e.kind != KMosquito && e.kind != KDeathstorm && e.kind != KFloh && e.kind != KKiller && e.kind != KAmbush && e.kind != KTunnel && e.kind != KMars) {
            // bank into the sideways drift, nose down when flying forward
            Vec3 fwd{-std::sin(e.yaw), std::cos(e.yaw), 0}, right{std::cos(e.yaw), std::sin(e.yaw), 0};
            float wantRoll = std::clamp(dot(e.drift, right) / std::max(e.speed, 1.f) * 0.35f, -0.4f, 0.4f);
            float wantPitchB = std::clamp(-dot(e.drift, fwd) / std::max(e.speed, 1.f) * 0.25f, -0.3f, 0.3f);
            e.roll += (wantRoll - e.roll) * std::min(1.f, dt * 3.f);
            e.pitch += (wantPitchB - e.pitch) * std::min(1.f, dt * 3.f);
        }
        if (Renderer::Instance* in = renderer_->instanceForWorldObject(e.wi)) {
            if (e.moving) {
                in->pos = e.pos;
                if ((e.kind == KVehicle || (e.kind >= KBlitz && e.kind <= KBuggy)) && e.landModel >= 0) in->model = e.inWater && e.waterModel >= 0 ? e.waterModel : e.landModel;
                in->rot = (e.kind == KSpider || (e.kind == KVehicle && !(e.vFlags & 8))) ? spiderRot(e) : e.kind == KTerror ? Mat4::rotZ(e.yaw - e.homeYaw) * e.baseRot : (e.cat == Air ? Mat4::rotZ(e.yaw) * Mat4::rotX(e.pitch) * Mat4::rotY(e.roll) : Mat4::rotZ(e.yaw)) * Mat4::rotZ(faceFlip(e));
            } else if (e.cat == Static) in->pos = e.pos;
            if (e.kind == KSpider) spiderLegs(e, in->vars, in->nVars);
            if (e.hasTurret) { in->aim = true; in->aimYaw = wrapAngle(e.aimYaw - modelYaw); in->aimPitch = e.aimPitch; }
        }

        // --- shooting (RE 0x7c4d0): a shot every shoot_salven ticks while the gun points at the
        // aim point (yaw within 64/4096, pitch within 128/4096 of a turn); after shoot_number shots
        // a pause of not_shoot_t ticks. Turrets/buildings fire whenever the player is in range;
        // vehicles when attacking or, otherwise, with a chance of shoot_random/1000 per tick.
        if (!e.armed || !playerAlive || dist > e.shootRange) continue;
        const float ticks = dt * kTick;
        if (e.pauseLeft > 0) { e.pauseLeft -= ticks; if (e.pauseLeft > 0) continue; e.burst = 0; }
        // WESPE (dispatcher 0x88184 case 0x5c): always shoots while aiming (0x100) or backing off (0x200)
        bool allowed = e.cat == Static || e.alerted > 0 ||
                       (e.cat == Air ? (e.ws == WAim || e.ws == WRetreat) : e.state == Attack);
        if (e.kind == KMosquito) allowed = e.mState == 0x10 && !e.mOnPath;
        if (e.kind == KDeathstorm) { if (e.noShootPoint || e.waitT > 0) continue; allowed = !e.gInactive || e.state == OnPath; }   // dispatcher case 0x5d: only on the attack run
        if (e.kind == KMars) allowed = e.gs == 8 && e.state != OnPath;           // case 0x73: only while hovering
        if (e.kind == KTerror) allowed = e.tgT > 0 && e.tgT < float(e.tgNotTransT - 100);
        if (e.kind == KImperator) allowed = e.imC <= float(e.imBeamT);
        if (e.kind == KTunnel || e.kind == KSpider) { if (e.noShootPoint) continue; allowed = true; }                                   // case 0x79: always
        if (e.kind == KAmbush) allowed = e.aState == 0x10 || ((e.aState & 0x22) && e.alertT > 0 && !e.noWaitShoot);   // case 0x78
        if (e.kind >= KBlitz && e.kind <= KBuggy) {
            // dispatcher 0x88184: no fire at waypoints with flag 4 or while waiting; PANTHER always fires,
            // BLITZ / BUGGY on their path, when alerted, while standing (state 2) or by shoot_random
            if (e.noShootPoint || e.waitT > 0) continue;
            allowed = e.kind == KPanther ? !e.gInactive || e.state == OnPath
                                         : (e.pathStarted && !e.pathDone) || e.alertT > 0 || (e.gs & 2) != 0;
            if (e.kind == KBuggy && e.gInactive && e.state != OnPath) continue;
        }
        if (!allowed) { e.randAcc += float(e.shootRandom) * ticks; if (e.randAcc >= 1000.f) { e.randAcc = 0; allowed = true; } }
        if (!allowed) continue;
        e.intervalLeft -= ticks;
        if (e.intervalLeft > 0) continue;
        {
            Vec3 mz = e.pos + Vec3{0, 0, 2.f};
            Vec3 tp = aimPoint - mz;
            float yawErr = std::fabs(wrapAngle(std::atan2(-tp.x, tp.y) - e.aimYaw));
            float pitchErr = std::fabs(std::atan2(tp.z, std::sqrt(tp.x * tp.x + tp.y * tp.y)) - e.aimPitch);
            const float unit = kTwoPi / 4096.f;
            static const int dbgWi = std::getenv("XA_TRACE") ? std::atoi(std::getenv("XA_TRACE")) : -1;
            if (e.wi == dbgWi) std::printf("aimgate yawErr %.0f pitchErr %.0f\n", yawErr / unit, pitchErr / unit);
            if (e.kind != KBuggy && (yawErr > 64.f * unit || pitchErr > 128.f * unit)) continue;
        }
        e.intervalLeft = float(std::max(1, e.interval));
        Vec3 aimDir{-std::sin(e.aimYaw) * std::cos(e.aimPitch), std::cos(e.aimYaw) * std::cos(e.aimPitch), std::sin(e.aimPitch)};
        Vec3 muzzle = e.pos + Vec3{0, 0, std::max(1.5f, (tg ? tg->radius : 4.f) * 0.3f)} + aimDir * std::max(2.f, (tg ? tg->radius : 4.f) * 0.5f);
        Vec3 hit; int owner;
        if (coll_->raycast(muzzle, aimPoint, hit, owner)) { static const int dw = std::getenv("XA_TRACE") ? std::atoi(std::getenv("XA_TRACE")) : -1; if (e.wi == dw) std::printf("los blocked owner %d at %.0f %.0f %.0f\n", owner, hit.x, hit.y, hit.z); continue; }            // no line of sight
        Vec3 dir = normalize(aimPoint - muzzle);
        combat_->fireEnemy(muzzle + dir * 1.f, dir, e.rockets ? e.shotSpeed * 0.7f : e.shotSpeed, e.rockets, e.shotDamage);   // ENEMY.DEF cause_damage
        if (std::getenv("XA_DEBUG")) std::printf("shot %d interval %d burst %d/%d pause %d dmg %.0f\n", e.wi, e.interval, e.burst + 1, e.shootNumber, e.notShootT, e.shotDamage);
        if (e.shootNumber > 0 && ++e.burst >= e.shootNumber) { e.pauseLeft = float(e.notShootT); e.burst = 0; }
    }
}

} // namespace xa
