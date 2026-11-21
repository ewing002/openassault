#pragma once
#include "../core/fs.h"
#include <string>
#include <vector>

namespace xa {

// One [TYP] entry of OBJECT.TYP. World objects reference these by index (object class).
struct ObjectType {
    enum Method { NoRotate, RotateBeAlGa, RotateGaAlBe, TranslateRotate, RotateLikeCamera };
    struct Part { std::string name; Method method = TranslateRotate; int variableOffset = 0; };
    struct Spin { int part = 0; int speed[3] = {0, 0, 0}; };   // ROTATE_PART n SPEED a b g
    std::string name, behavior, destroyedType, releaseExtra, path;
    int pathActivation = -1, shootActivation = -1, activation = -1;   // GL units (x1000 game units)
    int detailMethod = 0;
    bool unselectible = false;   // UNSELECTIBLE (TYP flag 0x10): never a lock target (0x3ae99)
    std::vector<Part> parts;
    std::vector<Spin> spins;
    // SCAPESWITCH_START / SCAPESWITCH_TO: visible landscape sets, fog and flight ceiling of an area
    std::vector<std::string> scapeSets;            // upper case
    bool hasFog = false; int fogColor[4] = {0, 0, 0, 100}; float fogNear = 0, fogFar = 0;   // game units
    float fogDetail[3][2] = {{0, 0}, {0, 0}, {0, 0}}; int fogDetails = 0;   // FOGRANGEDETAIL: viewing range low / medium / high
    bool hasMaxHeight = false; float maxHeight = 0;                                       // game units
    int magmaSpeed = -1, magmaTime = -1;            // MAGMA_SPEED_AND_TIME (speed-up pads)
    std::string flickerWhenDestroyed;              // energy barrier: switched off once all objects of this TYP are destroyed
    std::string activateWhenDestroyed;             // switch fires when this TYP is destroyed
    std::string waterTyp;                          // WATER_TYP <TYP> (0x503c4): look in water (vehicles 0x7635c)
    std::string connectToFlying;                 // CONNECT_TO_FLYING <TYP> (0x506f2): rides rigidly on that (flying) object
    // *_WHEN_DESTROYED <TYP> (events 0x4f215, run by 0x582f4): when every object of the named TYP is
    // destroyed, objects of this TYP are removed (0), explode (1), explode after n ticks (2),
    // flicker for 90 ticks and vanish (3, FLICKER_WHEN_DESTROYED) or appear (4).
    struct WhenDestroyed { int kind = 0; std::string trigger; int ticks = 0; };
    std::vector<WhenDestroyed> whenDestroyed;
};

class TypeDB {
public:
    bool load(const GameFS& fs, const std::string& levelDir);
    const ObjectType* get(size_t i) const { return i < types_.size() ? &types_[i] : nullptr; }
    size_t size() const { return types_.size(); }
private:
    std::vector<ObjectType> types_;
};

} // namespace xa
