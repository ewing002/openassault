#pragma once
#include "../assets/world.h"
#include "../assets/model.h"
#include "../assets/typ.h"
#include "../assets/def.h"
#include "../assets/sprites.h"
#include "../render/renderer.h"
#include "../audio/audio.h"
#include "collision.h"
#include <vector>
#include <functional>
#include <map>
#include <algorithm>

namespace xa {

// Player weapons, hits, damage and destruction of world objects.
// Hit points (get_damage) and score come from ENEMY.DEF via the object's BEHAVIOR;
// destroyed objects are replaced by their DESTROYED_TYP model (OBJECT.TYP).
// Player arms (reverse engineered from XA_3DFX.EXE): shield 9900, three weapons with ammo
// (max 6000) and level (0..2), fire mode = ceil(ammo/2000) clamped to 2 selects the cooldown,
// each shot costs 10 ammo; up to 6 guided rockets; extras (pickups) per the original handler table.
class Combat {
public:
    struct Target { bool isTarget = false, alive = true, active = true, dynamic = false; float hp = 0, maxHp = 0; int score = 0;
                    int wreckModel = -1, wreckClass = -1; float radius = 1; bool shielded = false; int origModel = -1, origClass = -1; };
    void init(const World& world, const ModelLib& lib, const TypeDB& types, const DefDB& defs,
              const SpriteLib& sprites, Renderer& renderer, CollisionWorld& coll, Audio* audio, int playerWorld);
    struct Arms {
        int sel = 0;                         // selected weapon 0..2
        int ammo[3] = {1000, 1000, 0};
        int level[3] = {0, 0, 0};
        bool weapon3 = false;                // WEAPON3LEVEL extra collected
        int rockets = 2, smartbombs = 0, thunderbolts = 0, hostages = 0;   // FREEZEBOMB extra = THUNDERBOLT (ROCKET_THUD)
        float shield = 9900.f;
        float immortal = 0;                  // original units: -6 per tick (10000 = ~28 s)
        float magma = 0;                     // MAGMA drive fuel (0x253a5c): 10000, -15 per tick while active
        bool magmaOn = false; float magmaSpeed = 0;   // target extra forward speed (raw units/tick); 0 = default (heli 3780, tank 1800)
    };
    static constexpr float kMaxShield = 9900.f;
    Arms arms;
    void resetArms(int difficulty = 2);
    // origin = gun position, right = heli right vector (barrel offsets). Returns true if a shot left.
    bool fireWeapon(Vec3 origin, Vec3 right, Vec3 dir, Vec3 inherit);
    bool fireRocket(Vec3 origin, Vec3 dir, Vec3 inherit, int targetWorld = -1);
    // Smart bomb (RE 0x3c0a8): one guided SMARTBOMB_SHOOT per tracked target; false = no target.
    bool fireSmartbomb(Vec3 origin, Vec3 inherit, const std::vector<int>& targets);
    bool fireThunderbolt(Vec3 origin, Vec3 dir, Vec3 inherit, int targetWorld = -1);
    void selectWeapon(int w) { if (w >= 0 && w < 3 && (w < 2 || arms.weapon3)) arms.sel = w; }
    int weaponMode(int w) const { return std::clamp((arms.ammo[w] + 1999) / 2000, 0, 2); }
    std::function<void(int extraIndex)> onPickup;   // message hook
    static const char* extraName(int x);
    std::map<int, int> rescued;
    bool playerLanded = false;          // hostages walk to a landed helicopter
    void missionStarted();             // world extras collected from now on come back on a restart
    void missionRestarted();                       // collected hostages per object class (mission goals)
    bool nearestPickup(Vec3 from, Vec3& out) const;
    void hostages(std::vector<Vec3>& out) const;
    void setPickupLife(int worldIndex, float seconds) { for (auto& p : pickups_) if (p.world == worldIndex) p.life = seconds; }
    bool pickupTaken(int worldIndex) const { for (const auto& p : pickups_) if (p.world == worldIndex && p.extra < 0) return true; return false; }
    bool isPickupObject(int worldIndex) const { for (const auto& p : pickups_) if (p.world == worldIndex) return true; return false; }     // hostages still waiting (radar)
    void update(float dt);
    void setGunSound(bool firing);
    // Enemy fire (hits the player and the static world).
    void fireEnemy(Vec3 origin, Vec3 dir, float speed, bool rocket, float damage);
    void setPlayer(Vec3 pos, float radius, bool alive) { playerPos_ = pos; playerRadius_ = radius; playerAlive_ = alive; }
    float takePlayerDamage() { float d = playerDamage_; playerDamage_ = 0; return d; }
    void explodeAt(Vec3 at, float size, bool big) { explode(at, size, big); }
    void ramPlayer(int worldIndex, float playerDamage, Vec3 at);
    void setShielded(int worldIndex, bool on) { if (worldIndex >= 0 && size_t(worldIndex) < targets_.size()) targets_[size_t(worldIndex)].shielded = on; }
    bool incomingMissile() const;      // an enemy rocket is closing in on the player (HUD warning, speech 008)
    void damagePlayer(float amount) { if (arms.immortal <= 0) playerDamage_ += amount; }   // kamikaze hit: damages the player, destroys the unit
    // Mission control: (de)activate objects of a set, restore destroyed ones.
    void setActive(int worldIndex, bool active);
    void resetTarget(int worldIndex);
    std::vector<int> hostageReleasers() const { std::vector<int> v; for (size_t i = 0; i < releaseOf_.size() && i < targets_.size(); i++) if (releaseOf_[i] == 100 + 18 && targets_[i].alive && targets_[i].active) v.push_back(int(i)); return v; }   // testing
    void kill(int worldIndex, Vec3 at) { if (worldIndex >= 0 && size_t(worldIndex) < targets_.size() && targets_[size_t(worldIndex)].alive) damage(worldIndex, 1e9f, at); }   // testing
    // savegames: destroyed targets and collected world extras
    void saveState(std::vector<uint8_t>& alive, std::vector<int>& taken) const;
    void loadState(const std::vector<uint8_t>& alive, const std::vector<int>& taken);
    void setDynamic(int worldIndex, bool dyn) { if (worldIndex >= 0 && size_t(worldIndex) < targets_.size()) targets_[size_t(worldIndex)].dynamic = dyn; }
    float scaleAdvShots = 1.f, scaleEnemyShots = 1.f;
    int pilot = 0;                   // difficulty: dropped extras vanish after 1500 ticks for pilot > 0 (0x735f2)
    int scorePercent = 100;          // 0x21bdbc: difficulty 0/1 100 %, 2 150 %, 3 200 %
    float scoreFrac = 0;             // fractional score of the counting time bonus
    // Called when a target is destroyed; return true to take over (e.g. a helicopter crashing)
    // and call finishWreck() later.
    std::function<bool(int worldIndex)> onDestroyed;
    std::function<void(Vec3 at, float size, bool big)> onExplosion;   // controller rumble
    void finishWreck(int worldIndex, Vec3 at);
    void smokePuff(Vec3 at, float size) { effects_.push_back({SpriteLib::Smoke, at, 0, 1.6f, size}); }
    void firePuff(Vec3 at, float size) { effects_.push_back({SpriteLib::SmallExplosion, at, 0, 0.4f, size}); }

    std::vector<Renderer::Extra> extras;      // projectile models for this frame
    std::vector<Renderer::Sprite> sprites;    // explosion frames for this frame
    int score = 0, kills = 0;
    float rocketCooldown = 0;
    const Target* target(int worldIndex) const {
        return worldIndex >= 0 && size_t(worldIndex) < targets_.size() ? &targets_[size_t(worldIndex)] : nullptr;
    }
private:
    struct Projectile { int kind; Vec3 pos, vel; float life; float damage = 0; int model = -1, cls = -1; int target = -1; float splash = 0; };
    struct Pickup { int extra; Vec3 pos; int model, cls; int world = -1; float age = 0; bool pooled = false; float radius = 4; int taken = -1; float life = -1; };
    void dropExtra(int worldIndex, Vec3 at);
    void spawnExtra(int extra, Vec3 at);
    bool collect(int extra);
    int pickupClass_ = -1;            // object class of the pickup being collected
    struct Effect { int anim; Vec3 pos; float age, duration, size; };
    void damage(int worldIndex, float amount, Vec3 at);
    void explode(Vec3 at, float size, bool big);
public:
    int findModelForClass(int cls) const;   // model of an object class (TYP index), -1 if none
    // Extra drop sources (0x733df): the enemies / extras of the landscape files (list 0x21c5a8; every set
    // that is not a mission enemy set) and those of the template sets (0x21c5a0). Call once the
    // mission enemy sets are known.
    void setDropSources(const std::vector<std::string>& missionSetsUpper);
private:
    struct DropSource { int extra, cls, model; };
    std::vector<DropSource> dropLand_, dropTemplate_;   // in load order
    int dropFirst_ = -1;                                // first record of 0x21c5a8: its extra kind, -1 = no extra
    const ModelLib* lib_ = nullptr;
    const World* world_ = nullptr;
    const TypeDB* types_ = nullptr;
    const SpriteLib* sprites_ = nullptr;
    Renderer* renderer_ = nullptr;
    CollisionWorld* coll_ = nullptr;
    Audio* audio_ = nullptr;
    std::vector<Target> targets_;
    std::vector<Projectile> shots_;
    std::vector<Effect> effects_;
    std::vector<Pickup> pickups_;
    std::vector<int> extraClass_, extraModel_;          // per extra index (0 = SMARTBOMB ... 18 = HOSTAGE)
    std::vector<int> releaseOf_;                        // per world object: RELEASE_EXTRA code (-1 none)
    int releaseCounter_ = 0;
    unsigned rng_ = 12345;
    float wCool_[3] = {0, 0, 0}; int barrel_[3] = {0, 0, 0};
    struct ShotType { int model = -1, cls = -1; float damage = 300; };
    ShotType wType_[3][3];                              // [weapon][level]
    ShotType rocketType_, smartType_, thudType_;
    float enemyBulletDamage_ = 300, enemyRocketDamage_ = 1000;
    int sExtra_ = -1;
    int bulletModel_ = -1, bulletClass_ = -1, rocketModel_ = -1, rocketClass_ = -1;
    int sGun_ = -1, sRocket_ = -1, sHit_ = -1, sSmall_ = -1, sBig_ = -1, sAir_ = -1, sGround_ = -1;
    int gunVoice_ = 0;
    float hitSoundCooldown_ = 0;
    Vec3 playerPos_; float playerRadius_ = 4; bool playerAlive_ = false; float playerDamage_ = 0;
    int enemyBulletModel_ = -1, enemyBulletClass_ = -1, enemyRocketModel_ = -1, enemyRocketClass_ = -1;
    int sEnemyShot_ = -1, sHeliHit_ = -1;
};

} // namespace xa
