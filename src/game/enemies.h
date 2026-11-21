#pragma once
#include "../assets/world.h"
#include "../assets/typ.h"
#include "../assets/def.h"
#include "../assets/path.h"
#include "../render/renderer.h"
#include "collision.h"
#include "combat.h"
#include "../audio/audio.h"
#include <vector>
#include <map>
#include <functional>
#include <string>

namespace xa {

// Enemy behaviour. Parameters come from OBJECT.TYP (PATH, *ACTIVATIONAREA) and ENEMY.DEF
// (speed, rotatespeed, shoot_*, *_good_r ...). The behaviours follow the reverse engineered code of
// the original (dispatcher 0x88184).
class EnemySystem {
public:
    void init(const World& world, const TypeDB& types, const DefDB& defs, PathDB& paths,
              Renderer& renderer, Combat& combat, CollisionWorld& coll);
    void setActive(int worldIndex, bool active);   // mission control; activation also resets the unit
    void update(float dt, Vec3 playerPos, bool playerAlive);
    bool pathEndReached(int worldIndex) const;
    bool isMover(int worldIndex) const { return worldIndex >= 0 && size_t(worldIndex) < byWorld_.size() && byWorld_[size_t(worldIndex)] >= 0 && enemies_[size_t(byWorld_[size_t(worldIndex)])].kind == KMover; }
    int activeCount() const;
    bool nearest(Vec3 from, Vec3& out) const;   // closest active, alive enemy
    bool startCrash(int worldIndex);             // air units fall when destroyed
    void contacts(std::vector<std::pair<Vec3, Vec3>>& out, std::vector<int>* ids = nullptr) const;   // hostile units: position, velocity
    void friends(std::vector<Vec3>& out, std::vector<int>& ids) const;                              // ADV_FRIEND units (blue marks)
    void sounds(Audio& audio, Vec3 listener, float dt, bool on);   // looping unit sounds (DEF soundeffekt)
    // is any object of this TYP left in the active lists (main.cpp, 0x57704)? Drives ACTIVATION_WHEN_DESTROYED.
    std::function<bool(const std::string&)> typePresent;
    bool dying(int worldIndex) const { return worldIndex >= 0 && size_t(worldIndex) < byWorld_.size() && byWorld_[size_t(worldIndex)] >= 0 && enemies_[size_t(byWorld_[size_t(worldIndex)])].dState > 0 && enemies_[size_t(byWorld_[size_t(worldIndex)])].dState < 3; }   // long death sequence running (PLUTO)
    std::vector<Renderer::Extra> extras;   // effect objects for this frame (PLUTO death: FLASH2, FINALFACE)
    bool anyCrashing() const { for (const auto& e : enemies_) if (e.crashing) return true; return false; }
private:
    enum Cat { Static, Ground, Air };
    enum State { Idle, OnPath, Attack };
    enum WState { WNone, WApproach, WClimb, WHover, WAim, WStrafe, WRetreat };
    enum Kind { KOther, KWespe, KMosquito, KBlitz, KPanther, KBuggy, KDeathstorm, KFloh, KKiller, KAmbush, KTunnel, KMars, KSpider, KTerror, KImperator, KMover, KVehicle, KPluto };
    struct Attach { int wi = -1; Vec3 home; Mat4 baseRot; float baseYaw = 0; };   // CONNECT_TO_FLYING child at rest
    struct Enemy {
        int wi = -1; Cat cat = Static; State state = Idle;
        bool active = false, armed = false, friendly = false, pathEnd = false, moving = false;
        Vec3 home, pos; float homeYaw = 0, yaw = 0;
        Mat4 baseRot;
        const std::vector<PathPoint>* path = nullptr; size_t pathIdx = 0;
        float speed = 0, turn = 0, goodR = 60, activation = 250, pathActivation = 600, shootRange = 220;
        int salvo = 1, salvoLeft = 0; float salvoPause = 3, shotTimer = 0, shotSpeed = 60; bool rockets = false; float shotDamage = 300; float anim = 0, animRate = 1; bool groundSnap = true, enclosed = false;
        float circle = 0, hover = 15, groundOffset = 0;
        bool groundMeasured = false;   // groundSnap / groundOffset (spiders: spH) come from a surface actually found
        bool hasTurret = false; float aimYaw = 0, aimPitch = 0, turretSpeed = 1.2f;   // world yaw / pitch of the gun
        float pitch = 0, strafeDir = 1, strafeT = 0; Vec3 drift;
        WState ws = WNone; float wTimer = 0, wTarget = 0, wDir = 1; int wCount = 0; bool wasHit = false;
        float maxR = 120, minR = 32, deltaR = 16, highOffset = 0, rotRate = 20; int shootT = 100, flyLeftT = 80, waitShootT = 60, hitReaction = 40;
        int interval = 15, shootNumber = 0, notShootT = 0, shootRandom = 0, burst = 0; float pauseLeft = 0, intervalLeft = 0, randAcc = 0, turretRate = 0, alerted = 0, lastHp = 0;
        bool crashing = false; Vec3 vel, prevPos; float spin = 0, roll = 0, smokeT = 0;
        // ground vehicle AI (BLITZ 0x7f99c, PANTHER 0x818bc, BUGGY 0x82950); gs uses the original state bits
        Kind kind = KOther; int gs = 0, gNext = 0, gTurnCase = 0, gRandDir = 1; float gTimer = 0, gDist = 0, gTurnDiv = 32, gTargetYaw = 0;
        bool collided = false, pathStarted = false, pathDone = false, hitLatched = false, cantAim = false, noShootPoint = false, gInactive = true;
        float alertT = 0, waitT = 0; Vec3 gVel, gWant;
        float rotSpeed = 0, rawSpeed = 0, accel = 0, brake = 0, actBox = -1, otherActR = 0, radius = 3;
        float pGood = 0, pEsc = 0, pMax = 0, pMin = 0; int pShootT = 300, pEscapeT = 450, rocketT = 500; float minAlpha = -256, maxAlpha = 768;
        float rocketLeft = 0;
        // MOSQUITO (0x7ec44): glider that always flies forward; mYawT/mPitchT/mRollT are its target angles
        int mState = 0, mDir = 1; float mTimer = 0, mYawT = 0, mPitchT = 0, mRollT = 0; Vec3 mHome; bool mInit = false, mOnPath = false;
        // AMBUSH (0x839c8) / TUNNELPATH (0x84b4c)
        int aState = 0, aIdx = 1, tNode = -1; float aTimer = 0; bool tBack = false, goOnGround = true, strictPath = false, noWaitShoot = false;
        int hiShootT = 400, hiWaitT = 300, hiReactionT = 0, tuFreeGo = 0, tuShootT = 400; float tuTurnBefore = 200, tuMinR = 30; bool tuTurnToEnemy = true;
        bool hitAct = false; float hitActR = 0;   // 0x7281c: a hit keeps the unit active while the player stays within 1.5x the distance at the hit
        // TERROR_GOD (dispatcher case 0x5f) / IMPERATOR (case 100)
        float tgT = 0, tgBob = 200, tgRocket = 0, startZ = 0, tgVz = 0; int tgRocketT = 600, tgTransT = 200, tgNotTransT = 800; bool cloaked = false;
        float imC = 0; int imSmokeT = 50, imBeamT = 150, imBeamEndT = 300, imEndT = 400; bool beamOn = false;
        int mvState = 0, mvIdx = 0, mvDir = 1;   // MOVINGWALL / BLOCK (0x85854)
        int spFlags = 0; float spSpeed = 0;
        float spFrame = 0, spFrameSpeed = 0;   // leg animation frame [0x94] (0..60) and DEF spider_frame_speed
        // SPIDER surface walking (0x77534): spUp = smoothed surface normal (its body's up), spUpT = target normal,
        // spH = height of the origin above the surface, spAir = ballistic jump ([0x7c]), spLastZ = height of the
        // last surface it stood on (falls of more than 20 radii destroy it)
        bool spAir = false; Vec3 spUp{0, 0, 1}, spUpT{0, 0, 1}, spVel, spHome{0, 0, 1}; float spH = 0.4f, spLastZ = 0;
        // body tilt as the original's angle words a0 (x) and a1 (y) of [+0x1c], 4096 per turn; spUp follows from them
        float spA0 = 0, spA1 = 0, spHomeA0 = 0, spHomeA1 = 0;
        // ESEL / ESEL_AS / BARRACUDA (0x7635c): fixed speed, turn rate, decision cycle and flags from the dispatcher
        // (waterZ: height of the flat water surface a boat started on)
        float vSpeed = 0, vTurn = 0, bobPhase = 0, bobZ = 0, waterZ = 0; int vCycle = -1, vFlags = 0, rocketCnt = 0; bool vRockets = false;
        int landModel = -1, waterModel = -1; bool inWater = false;   // WATER_TYP look while in water (speed 2/3)
        // PLUTO (L6 mothership, case 0x57 / 0x877d8): rides between the heights of its first two path points,
        // children (CONNECT_TO_FLYING) move with it; wakeBy = objects of its ACTIVATION_WHEN_DESTROYED TYP
        std::vector<Attach> attached; std::vector<int> wakeBy; int attachedTo = -1; bool dormant = false; std::string wakeType;
        float pzA = 0, pzB = 0, pVz = 0, beamT = -1, flashDmg = 3; int pIdx = 0; Vec3 beamSrc;
        int dState = 0; float dT = 0, dVz = 0, plumeZ = 0; int dRing = 0;
        Vec3 collPos; float collYaw = 0; bool collInit = false;   // pose the collision triangles were last moved to
        float mineOfs = 0, causeDmg = 1000; bool bandit = false;
        float glEndR = 80, glGoHigherT = 50, glMinR = 15, glNeigung = 16, glMaxWait = 50, glDirekt = 100;
    };
    std::vector<Enemy> enemies_;
    // The models face their local -y axis (the vehicle routine moves by M * (0, -speed, 0), 0x76e3d), while the
    // AI here steers a heading whose direction of travel is (-sin, cos). Units that move get pi added to the
    // drawn angle and start with heading homeYaw + pi; units drawn relative to their placement do not.
    static float faceFlip(const Enemy& e) { return (e.kind == KTerror || e.kind == KPluto || e.kind == KMover || e.cat == Static) ? 0.f : 3.14159265f; }
    struct Loop { int wi = -1; std::string name; int snd = -1, voice = 0; bool onlyMoving = false; Vec3 last; float stillT = 1.f; };
    std::vector<Loop> loops_;
    std::vector<int> byWorld_;
    bool hiddenFromPlayer(const struct Enemy& e) const;
    void wespeThink(Enemy& e, Vec3 player, float dist, float dt);
    void wespeMove(Enemy& e, Vec3 player, float dt);
    void groundAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    bool groundPath(Enemy& e, Vec3 player, float ticks, float& speedRaw, float& rot, Vec3& wp);
    bool probe(const Enemy& e, float angleUnits, float factor) const;
    void groundAlert(const Enemy& hit);
    void mosquitoAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void deathstormAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void mineAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void ambushAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void tunnelAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void marsAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void spiderAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void spiderMove(Enemy& e, float speedRaw, float ticks);
    static void spiderLegs(const Enemy& e, int16_t* vars, int& nVars);
public:
    // Measures the height of ground units / spiders above the surface they were placed on. Only the landscape
    // sets of the current area are solid, so main calls it again after every scape switch; units already
    // moving and units measured before keep their values.
    void measureGround();
private:
    // a moving unit (list 0x21c578) within r of p besides self: moving units are out of the collision world
    bool movingUnitNear(const Enemy& self, Vec3 p, float r) const;
    static Mat4 spiderRot(const Enemy& e);
    // tilt angles a0 / a1 approach those of a surface normal (0x73a10 per angle); updates spUp
    static void tiltTowards(Enemy& e, Vec3 normal, float rate, float ticks);
    void vehicleAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void plutoAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void applyRigid(Enemy& ship);
    bool wakeObjectsLeft(const Enemy& e) const;   // any object of the ACTIVATION_WHEN_DESTROYED TYP still there
    void terrorAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    void moverAI(Enemy& e, Vec3 player, float dt);
    void imperatorAI(Enemy& e, Vec3 player, bool playerAlive, float dt);
    struct TunnelGraph { std::vector<Vec3> pos; std::vector<std::vector<int>> nb; std::vector<char> occ; };
    std::map<const std::vector<PathPoint>*, TunnelGraph> graphs_;
    TunnelGraph& graphFor(const std::vector<PathPoint>* path);
    unsigned rnd_ = 0x1234567u;
    unsigned nextRnd() { rnd_ = rnd_ * 1103515245u + 12345u; return (rnd_ >> 16) & 0x7fff; }
    Vec3 lastPlayer_{0, 0, 0}, playerVel_{0, 0, 0};
    Renderer* renderer_ = nullptr; Combat* combat_ = nullptr; CollisionWorld* coll_ = nullptr;
    float unitScale_ = 0.001f;
    float beamDamage_ = 0;
    bool beamTouch_ = false; Vec3 beamTouchAt_; int beamVoice_ = 0;   // player inside an IMPERATOR beam this frame (sound 0x17)
    int flash2Model_ = -1, flash2Cls_ = -1, faceModel_ = -1, faceCls_ = -1;   // PLUTO death effect objects   // IMPERATOR_BEAM DEF cause_damage per original frame (no DEF block sets it)
    const World* world_ = nullptr; const TypeDB* types_ = nullptr;
};

} // namespace xa
