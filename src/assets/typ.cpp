#include "typ.h"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace xa {

static ObjectType::Method methodFromString(const std::string& s) {
    if (s == "NO_ROTATE") return ObjectType::NoRotate;
    if (s == "ROTATE_BE_AL_GA") return ObjectType::RotateBeAlGa;
    if (s == "ROTATE_GA_AL_BE") return ObjectType::RotateGaAlBe;
    if (s == "ROTATE_LIKE_CAMERA") return ObjectType::RotateLikeCamera;
    return ObjectType::TranslateRotate;
}

bool TypeDB::load(const GameFS& fs, const std::string& levelDir) {
    std::vector<uint8_t> raw;
    if (!fs.read(levelDir + "\\OBJECT.TYP", raw)) return false;
    std::istringstream in(decodeText(raw));
    std::string line;
    ObjectType* cur = nullptr;
    while (std::getline(in, line)) {
        if (auto c = line.find(';'); c != std::string::npos) line.resize(c);
        std::istringstream ls(line);
        std::string w;
        if (!(ls >> w)) continue;
        if (w == "[TYP]") { types_.emplace_back(); cur = &types_.back(); ls >> cur->name; continue; }
        if (!cur) continue;
        if (w == "BEHAVIOR") ls >> cur->behavior;
        else if (w == "DESTROYED_TYP" || w == "RELEASE_EXTRA") {
            // both keys may share one line: DESTROYED_TYP x RELEASE_EXTRA y
            std::string key = w, v;
            while (ls >> v) {
                if (key == "DESTROYED_TYP") cur->destroyedType = v;
                else if (key == "RELEASE_EXTRA") cur->releaseExtra = v;
                if (!(ls >> key)) break;
            }
        }
        else if (std::string u = w; (std::transform(u.begin(), u.end(), u.begin(), [](unsigned char ch) { return char(std::toupper(ch)); }), u == "UNSELECTIBLE"))
            cur->unselectible = true;   // spelled in several cases in the data
        else if (w == "PATH") ls >> cur->path;
        else if (w == "PATHACTIVATIONAREA") ls >> cur->pathActivation;
        else if (w == "SHOOTACTIVATIONAREA") ls >> cur->shootActivation;
        else if (w == "ACTIVATIONAREA") ls >> cur->activation;
        else if (w == "DETAIL_METHODE" || w == "AUTO_DETAIL_METHODE") ls >> cur->detailMethod;
        else if (w == "NAME") {
            ObjectType::Part p; std::string method, kw;
            ls >> p.name >> method >> kw >> p.variableOffset;
            p.method = methodFromString(method);
            cur->parts.push_back(p);
        } else if (w == "SCAPESWITCH") {
            std::string v; ls >> v; for (auto& c : v) c = char(std::toupper((unsigned char)c));
            cur->scapeSets.push_back(v);
        } else if (w == "FOGCOLOR") {
            ls >> cur->fogColor[0] >> cur->fogColor[1] >> cur->fogColor[2] >> cur->fogColor[3]; cur->hasFog = true;
        } else if (w == "FOGRANGE") {
            ls >> cur->fogNear >> cur->fogFar;
        } else if (w == "FOGRANGEDETAIL") {           // three near/far pairs (detail levels); the last is the highest
            float a, b; cur->fogDetails = 0;
            while (ls >> a >> b) { cur->fogNear = a; cur->fogFar = b; if (cur->fogDetails < 3) { cur->fogDetail[cur->fogDetails][0] = a; cur->fogDetail[cur->fogDetails][1] = b; cur->fogDetails++; } }
        } else if (w == "MAXHEIGHT") {
            ls >> cur->maxHeight; cur->hasMaxHeight = true;
        } else if (w == "MAGMA_SPEED_AND_TIME") {
            ls >> cur->magmaSpeed >> cur->magmaTime;
        } else if (w == "FLICKER_WHEN_DESTROYED") {
            ls >> cur->flickerWhenDestroyed;
            cur->whenDestroyed.push_back({3, cur->flickerWhenDestroyed, 90});
        } else if (w == "REMOVE_WHEN_DESTROYED" || w == "EXPLODE_WHEN_DESTROYED" || w == "APPEAR_WHEN_DESTROYED") {
            ObjectType::WhenDestroyed e; ls >> e.trigger;
            e.kind = w[0] == 'R' ? 0 : w[0] == 'E' ? 1 : 4;
            cur->whenDestroyed.push_back(e);
        } else if (w == "TIME_EXPLODE_WHEN_DESTROYED") {
            ObjectType::WhenDestroyed e; e.kind = 2; ls >> e.trigger >> e.ticks;
            cur->whenDestroyed.push_back(e);
        } else if (w == "ACTIVATION_WHEN_DESTROYED") {
            ls >> cur->activateWhenDestroyed;
        } else if (w == "CONNECT_TO_FLYING") {
            ls >> cur->connectToFlying;
        } else if (w == "WATER_TYP") {         // 0x503c4: model used in water (NOTHING = none)
            ls >> cur->waterTyp;
            if (cur->waterTyp == "NOTHING") cur->waterTyp.clear();
        } else if (w == "ROTATE_PART") {
            ObjectType::Spin s; std::string kw;
            ls >> s.part >> kw >> s.speed[0] >> s.speed[1] >> s.speed[2];
            cur->spins.push_back(s);
        }
    }
    return !types_.empty();
}

} // namespace xa
