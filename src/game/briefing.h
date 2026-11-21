#pragma once
#include "../core/fs.h"
#include "../render/math.h"
#include "mission.h"
#include <functional>
#include <string>
#include <vector>

namespace xa {

// Plays the [BRIEFING] script of a mission: camera flights (*.MOT), typed texts
// (MISSION.CHR ids), target highlighting. Time is measured in game ticks (70/s).
class Briefing {
public:
    struct Shown { int id; float x, y; int widthChars; float typed; bool always; bool done; bool voiced; };
    std::function<bool(const std::string& typName, Vec3& pos, float& radius)> findObject;
    std::function<int(int id)> textLength;
    void start(const MissionFile& m, const GameFS& fs, const std::string& levelDir, float unitScale);
    void update(float dt);
    void skip();
    bool onMonitor = false;           // between ZOOM_FROM_COCKPIT_TO_BRIEFING and ZOOM_FROM_BRIEFING_TO_COCKPIT; otherwise a cutscene seen from outside
    bool inCockpit = false;           // after ZOOM_FROM_BRIEFING_TO_COCKPIT (remaining commands still run)
    bool active() const { return active_; }
    bool hasCamera = false;
    Vec3 camPos, camTarget;
    std::vector<Shown> texts;
    std::vector<std::string> selected;
    float fade = 0;           // 0 = clear, 1 = black
    // HELIMOTION *.HLM: the player's helicopter is flown along a path (e.g. through the tunnel
    // between the two halves of a level). heliMoving while it runs; heliArrived once at the end.
    bool heliMoving = false, heliArrived = false;
    Vec3 heliPos; float heliYaw = 0, heliPitch = 0, heliRoll = 0;
    float charsPerTick = 0.7f;
    // ZOOM_FROM_COCKPIT_TO_BRIEFING / ZOOM_FROM_BRIEFING_TO_COCKPIT: the view moves between the pilot's eye (0)
    // and the cockpit monitor (1) that shows the briefing
    float zoom = 0;
    bool zooming() const { return zoomDir_ != 0; }
    bool zoomingIn() const { return zoomDir_ > 0; }
    float zoomInTicks = 80.f, zoomOutTicks = 45.f;   // in: chase camera -> eye -> monitor; out: 45 back to the eye (80 to the chase camera)
    float distort = 0;        // DISTORT n (0x578e4): ticks of screen noise left; the script waits meanwhile
    std::function<void(const char* wav)> sound;   // OPENTEXT / ENDTEXT / CLOSETXT
    std::function<bool(int textId)> speakText;  // true when a voice plays (BRIEFING 2 shows the texts without one)    // MISSIONS\SPEAK.CHR voice of a text (0x3d2c9)
    std::function<bool()> voiceBusy;              // TEXT_WAIT_FOR_END also waits for the voice
    bool showText = true;                         // BRIEFING setting: 0 text, 1 text and voice, 2 voice only
private:
    struct Path { std::vector<Vec3> src, dst; std::vector<float> ticks; float total = 0; };
    bool loadPath(const std::string& name, Path& p);
    Vec3 samplePath(const std::vector<Vec3>& pts, float t) const;
    float heliYawAt(size_t i) const;
    std::vector<Vec3> loadHeliPath(const std::string& f, std::vector<Vec3>* angles = nullptr) const;
    std::vector<Vec3> heliAng_;       // ANGLE a b g per point (4096 = full turn): pitch, roll, -yaw
    void heliPose(float t);
    std::vector<Vec3> heli_; float heliT_ = -1;
    void finish() { onMonitor = false; distort = 0; zoom = 0; zoomDir_ = 0; heliMoving = false; heli_.clear(); heliT_ = -1; active_ = false; texts.clear(); selected.clear(); hasCamera = false; fade = 0; }
    const GameFS* fs_ = nullptr;
    std::string dir_; int pilot_ = 0;
    float unit_ = 0.001f;
    std::vector<std::string> lines_;
    size_t pc_ = 0;
    bool active_ = false;
    float wait_ = 0;          // ticks
    enum Wait { None, Delay, Text, Camera, Select, Heli } waitFor_ = None;
    Path path_; float pathT_ = -1;
    // SETCAMERATO
    bool orbit_ = false; Vec3 orbitTarget_; float orbitDist_ = 50, orbitAngle_ = 0.8f, orbitT_ = 0, orbitTime_ = 50;
    Vec3 orbitFrom_, orbitFromTarget_;
    float fadeDir_ = 0, zoomDir_ = 0;
};

} // namespace xa
