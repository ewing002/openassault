#pragma once
#include "../render/math.h"
#include <string>
#include <vector>
#include <cstdint>

namespace xa {

// Pilots, savegames, top ten and settings.
// Original (RE): four fixed pilots; the pilot index is the difficulty level (0 easy .. 3 extreme,
// GFX.CHR 188..191). It selects GLOBAL\ENEMYn.DEF / LEVELxx\ENEMYn.DEF (0x5d1f4), the score
// multiplier (0x2fed8) and how many operation areas can be played (2 / 4 / 6 / 6, 0x35908).
// At every successful mission the game saves SAVEGAME\PpLllMmm.BIN (pilot record, 0x62 bytes) and
// .DAT (world state) for the mission that follows (0x5a44c); Load Game offers the saved missions
// of the current pilot. HISCORE.BIN keeps ten entries of name[16], score, difficulty (0xa1e58).
// OpenAssault keeps its own (text) format in <exe folder>\SAVEGAME\PpLllMmm.SAV.
struct SaveArms {
    int sel = 0, ammo[3] = {1000, 1000, 0}, level[3] = {0, 0, 0}; bool weapon3 = false;
    int rockets = 2, smartbombs = 0, thunderbolts = 0; float shield = 9900.f, magma = 0;
};

struct SaveGame {
    int pilot = 0, level = 1, mission = 1;      // the mission that starts when the game is loaded
    std::string missionFile = "MISS_01.SCR";
    int score = 0; float playTicks = 0;
    SaveArms arms;
    // world state; only for a mission inside a level (empty at the start of an operation area)
    bool hasWorld = false;
    Vec3 heliPos; float heliYaw = 0;
    std::vector<std::string> activeSets;
    std::vector<uint8_t> alive;                 // per world object: target still alive
    std::vector<int> taken;                     // world extras already collected
    std::vector<uint8_t> shieldDown;
    float missionTimer = 0, missionTimerStart = 0; bool timerShown = false; std::string lastMissionFile;
    std::string scape;                          // landscape area (SCAPESWITCH type) at the save
    bool tank = false;                          // the player drives the tank (vehicle change at a base)
};

struct HiScore { std::string name; int score = 0, diff = 0; };

// Remappable controls (Options > Controls Setup > ADJUST CONTROLS, GFX.CHR 17..36).
// Two keys per function; values are SDL scancodes, mouse buttons are kMouse + button, 0 = none.
enum Control { CForward, CBackward, CTurnLeft, CTurnRight, CUp, CDown, CDriftLeft, CDriftRight,
               CFire, CChangeWeapon, CRocket, CSmartBomb, CThunderbolt, CCockpit, CLock1, CLock2, CControlCount };
constexpr int kMouse = 1000;
constexpr int kControlText[CControlCount] = {20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32, 33, 36, 34, 291};
// menu order of the original list (Lock Target No. 1 / 2 come before Cockpit/External); appended to the enum so
// saved KEY<n> / PAD<n> lines keep their meaning
constexpr int kControlOrder[CControlCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, 13};
void defaultControls(int keys[CControlCount][2]);
void defaultPad(int pad[CControlCount]);
// analog sticks: forward/back, turn, drift, height; value = SDL axis * 2 + inverted, -1 = none
enum StickFn { SForward, STurn, SDrift, SHeight, SCount };
constexpr float kStickNatural[SCount] = {-1.f, -1.f, 1.f, -1.f};   // stick up = forward / up, left = turn left, right = drift right
void defaultSticks(int s[SCount]);   // controller button per function (pad::kPad + virtual button, 0 = none)

struct Settings {
    int pilot = 1;
    int musicVolume = 70, sfxVolume = 100;      // 0..100
    bool music = true;
    int briefing = 1;                           // GFX 170..172: 0 text, 1 text and voice, 2 voice (INIT.CFG BRIEFING)
    bool speech = true;                         // INIT.CFG SPEAK
    int viewRange = 2;                          // INIT.CFG DETAILSIGHT: 0 low .. 2 high
    int rumble = 2;                             // OpenAssault: controller rumble 0 off, 1 low, 2 high
    std::string language = "HU";                // OpenAssault: text language (lang\<code>\ overlays), "EN" = the original
    bool hdTextures = true;                     // OpenAssault: HD texture pack (HDTEX) / 2x textures
    bool fullscreen = false;                    // OpenAssault: Options > Graphic Detail, Alt+Enter
    int windowW = 1280, windowH = 720;          // OpenAssault: window size (WINDOWSIZE=WxH)
    int fullW = 0, fullH = 0;                   // OpenAssault: fullscreen resolution, 0 = the desktop's (FULLSCREENSIZE=WxH)
    int viewDistance = 100;                     // OpenAssault: viewing distance multiplier in percent (50..300)
    int keys[CControlCount][2];
    int pad[CControlCount];
    int stick[SCount];
    Settings() { defaultControls(keys); defaultPad(pad); defaultSticks(stick); }
};

namespace profile {
    std::string folder();                       // <exe folder>/SAVEGAME/ (created on demand)
    bool write(const SaveGame& s);
    bool read(int pilot, int level, int mission, SaveGame& s);
    std::vector<std::pair<int, int>> list(int pilot);     // saved (level, mission), sorted
    bool latest(int pilot, SaveGame& s);        // the most advanced save (pilot screen)
    void erase(int pilot);                      // New Game / delete pilot information
    std::vector<HiScore> loadTop();             // always 10 entries (defaults when there is no file)
    void saveTop(const std::vector<HiScore>& t);
    int topRank(int score);                     // 0..9, -1 = not good enough
    Settings loadSettings();
    void saveSettings(const Settings& s);
    inline int areaLimit(int pilot) { return pilot <= 0 ? 2 : pilot == 1 ? 4 : 6; }
    inline int scorePercent(int pilot) { return pilot >= 3 ? 200 : pilot == 2 ? 150 : 100; }
}

} // namespace xa
