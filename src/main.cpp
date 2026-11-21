// OpenAssault - native reimplementation of Blue Byte's Extreme Assault (1997).
// Milestone 1: loads original level data and renders it with OpenGL 3.3.
#include <SDL.h>
#include "core/fs.h"
#include "core/lang.h"
#include "assets/textures.h"
#include "assets/model.h"
#include "assets/world.h"
#include "assets/typ.h"
#include "render/glapi.h"
#include "render/renderer.h"
#include "game/collision.h"
#include "game/heli.h"
#include "game/combat.h"
#include "game/enemies.h"
#include "game/mission.h"
#include "game/briefing.h"
#include "game/profile.h"
#include "game/pad.h"
#include "assets/hdtex.h"
#include "assets/font.h"
#include "ui/text.h"
#include "ui/hud.h"
#include "assets/path.h"
#include <set>
#include <array>
#include <algorithm>
#include <map>
#include <filesystem>
#include <functional>
#include <cctype>
#include "assets/def.h"
#include "assets/sprites.h"
#include "audio/audio.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <limits>
#include <algorithm>

using namespace xa;

struct Options {
    std::string game = ".";
    int level = 1;
    int carryScore = 0; float carryTicks = 0;
    int difficulty = 1;                          // 0..3 like the original (score: 100/100/150/200 %)   // carried from one operation area to the next
    std::string shot;            // render one frame to this BMP and quit
    bool haveCam = false;
    float cam[5] = {0, 0, 0, 0, 0};   // x y z (GL units) yaw pitch (degrees)
    std::string hide;
    std::string extract;         // dump textures as TGA into this folder and quit
    std::string exportLang;      // write a translation template lang/<code>/ (original English texts) and quit
    int width = 1280, height = 720;
    bool sizeGiven = false;      // --size on the command line wins over the saved window size
    std::string model;           // show a single model (orbit view)
    float time = -1;             // fixed animation time for --shot
    float flySeconds = 0;        // --fly fwd,strafe,lift,yaw,seconds: scripted flight (testing)
    float flyInput[4] = {0, 0, 0, 0};
    int fire = 0;                // --fire 1|2|3: hold gun / rockets during --fly (testing)
    bool mute = false;
    std::string mission = "MISS_01.SCR";
    bool autoAim = false;
    bool cockpit = true;         // start in the cockpit view (like the original); --chase starts outside
    float crashShot = -1;        // testing: --crashshot s: screenshot s seconds after the first enemy starts falling
    float briefShot = -1;        // testing: --brief seconds: screenshot during the briefing        // testing: --autoaim turns towards the nearest enemy during --fly
    bool fullscreen = false;
    bool menu = true;            // start with the main menu (off for --level/--mission/test options/--nomenu)
    // pilots and savegames (menu games only)
    bool saving = false;         // write savegames at mission changes
    const SaveGame* load = nullptr;              // continue from this savegame (world state)
    bool haveArms = false; SaveArms carryArms;  // weapons carried into the next operation area
    int lastScore = 0;
    bool played = false;         // a game ran since the menu was last shown (top ten entry)
    bool allMissions = false;    // --allmissions: Load Game lists every mission (testing)
    bool cheats = false, assault = false;   // main menu codes "oh dear" (0xa0458) and "emertxe" (ASSAULT MODE)
    bool level6 = false;                    // "level6 " (0xa0631): flag 0x253c12, never read by the original
    Settings settings;
};

static void usage() {
    std::puts("openassault [--game DIR] [--level N] [--fullscreen] [--size WxH]\n"
              "            [--cam x,y,z,yaw,pitch] [--hide set1,set2] [--shot out.bmp] [--extract DIR]\n"
              "            [--model NAME.3DI] [--time seconds] [--nomenu]\n"
              "Without options the main menu starts; --level/--mission start a mission directly.\n"
              "Helicopter: W/S forward/back, A/D strafe, mouse or arrows turn, Space/E up, Ctrl/Q down, Shift boost\n"
              "Fire: left mouse / F = weapon, 1/2/3 / Tab / wheel = select weapon, right mouse / R = guided rocket,\n"
              "B = smart bomb, T = thunderbolt (guns fire ahead, auto-aim on the locked target), F2 / V cockpit view\n"
              "G / middle mouse = lock the target nearest the screen centre, H = lock the next target further away\n"
              "F1 toggles free camera (WASD, Q/E, right mouse look), F12 screenshot, Esc quit");
}

static bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--game") o.game = next();
        else if (a == "--level") o.level = std::atoi(next().c_str());
        else if (a == "--shot") o.shot = next();
        else if (a == "--hide") o.hide = next();
        else if (a == "--extract") o.extract = next();
        else if (a == "--export-lang") o.exportLang = next();
        else if (a == "--model") o.model = next();
        else if (a == "--time") o.time = float(std::atof(next().c_str()));
        else if (a == "--fire") o.fire = std::atoi(next().c_str());
        else if (a == "--mute") o.mute = true;
        else if (a == "--autoaim") o.autoAim = true;
        else if (a == "--cockpit") o.cockpit = true;
        else if (a == "--chase") o.cockpit = false;
        else if (a == "--crashshot") o.crashShot = float(std::atof(next().c_str()));
        else if (a == "--allmissions") o.allMissions = true;
        else if (a == "--pilot") o.difficulty = std::clamp(std::atoi(next().c_str()), 0, 3);
        else if (a == "--brief") o.briefShot = float(std::atof(next().c_str()));
        else if (a == "--mission") { o.mission = next(); if (o.mission.find('.') == std::string::npos) o.mission = (o.mission.size() <= 2 ? "MISS_" + std::string(o.mission.size() == 1 ? "0" : "") + o.mission : o.mission) + ".SCR"; }
        else if (a == "--fly") {
            std::string s = next(); for (auto& c : s) if (c == ',') c = ' ';
            std::istringstream is(s); is >> o.flyInput[0] >> o.flyInput[1] >> o.flyInput[2] >> o.flyInput[3] >> o.flySeconds;
        }
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--size") { std::string s = next(); std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height); o.sizeGiven = true; }
        else if (a == "--cam") {
            std::string s = next(); for (auto& c : s) if (c == ',') c = ' ';
            std::istringstream is(s); for (float& f : o.cam) is >> f; o.haveCam = true;
        } else if (a == "--nomenu") o.menu = false;
        else { usage(); return false; }
        if (a != "--game" && a != "--fullscreen" && a != "--size" && a != "--mute" && a != "--chase" && a != "--cockpit") o.menu = false;
    }
    return true;
}

static int fatal(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    std::fprintf(stderr, "%s\n", buf);
    if (FILE* f = std::fopen("openassault.log", "a")) { std::fprintf(f, "%s\n", buf); std::fclose(f); }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenAssault", buf, nullptr);
    return 1;
}

// DATA.SFX\MUSIC XMI name -> GOG CD track (INIT.CFG CDAUDIOTRACKS, alphabetical XMI order)
static std::string musicTrack(const GameFS& fs, std::string xmi) {
    static const char* kXmi[] = {"BOSS", "DEAD", "END", "GAMEOVER", "LEV1A", "LEV1B", "LEV1C", "LEV2A", "LEV2B", "LEV3A",
                                 "LEV3B", "LEV4A", "LEV5A", "LEV6A", "LEV6B", "LEVOK", "MISOK", "MISSION", "TITEL"};
    std::vector<int> cdTracks = {13, 17, 18, 19, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 14, 16, 20};
    for (const char* cfg : {"INIT.CFG", "CDA\\INIT.CFG"}) {
        std::vector<uint8_t> raw;
        if (!fs.read(cfg, raw)) continue;
        std::string t(raw.begin(), raw.end());
        size_t k = t.find("CDAUDIOTRACKS=");
        if (k == std::string::npos) continue;
        std::vector<int> v; std::string num;
        for (size_t i = k + 14; i < t.size() && (std::isdigit((unsigned char)t[i]) || t[i] == '@'); i++) {
            if (t[i] == '@') { v.push_back(std::atoi(num.c_str())); num.clear(); } else num += t[i];
        }
        if (!num.empty()) v.push_back(std::atoi(num.c_str()));
        if (v.size() == 19) { cdTracks = v; break; }
    }
    for (auto& c : xmi) c = char(std::toupper((unsigned char)c));
    if (auto d = xmi.find('.'); d != std::string::npos) xmi.resize(d);
    for (int i = 0; i < 19; i++) if (xmi == kXmi[i]) {
        char p[64]; std::snprintf(p, sizeof p, "MUSIC\\track_%02d.ogg", cdTracks[size_t(i)]);
        return p;
    }
    return "";
}


// ---------------------------------------------------------------------------------------------
// Main menu (DATA\MENU): title picture, MENU.DAT background, HEADFONT heading, FONT/FONTDARK items
// (FONTUSEL = not selectable), texts from MENU\TEXT\GFX.CHR. Pages and texts follow the original
// (RE 0x9e..0xa2): MAIN MENU (1..8), CHOOSE PILOT (PLAYER.DAT, 0x9eaa1), Load Game = CHOOSE OPERATION
// AREA / LOAD MISSION with the pilot's savegames, OPTIONS MENU, SOUND SETUP, TOP TEN (HIGSCOR.DAT,
// 0xa1e58). Layout positions are an approximation of the original.
// Options > Graphic Detail: FULLSCREEN, Alt+Enter anywhere (desktop resolution, saved in OPENASSAULT.CFG)
// Resolutions (OpenAssault addition): the window has its own size (WINDOWSIZE, centred on the desktop) and the
// fullscreen mode its own (FULLSCREENSIZE; 0 = the desktop's resolution, a borderless window)
static std::vector<std::pair<int, int>> windowModes() {
    SDL_DisplayMode dm{};
    int dw = 99999, dh = 99999;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { dw = dm.w; dh = dm.h; }
    std::vector<std::pair<int, int>> v;
    for (auto p : {std::pair<int, int>{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1366, 768}, {1600, 900},
                   {1920, 1080}, {2560, 1440}, {3840, 2160}})
        if (p.first <= dw && p.second <= dh) v.push_back(p);
    if (v.empty()) v.push_back({1280, 720});
    return v;
}
static std::vector<std::pair<int, int>> fullscreenModes() {     // the display's own modes, without the desktop entry
    std::vector<std::pair<int, int>> v;
    for (int i = 0, n = SDL_GetNumDisplayModes(0); i < n; i++) {
        SDL_DisplayMode m{};
        if (SDL_GetDisplayMode(0, i, &m) != 0 || m.w < 640 || m.h < 480) continue;
        std::pair<int, int> p{m.w, m.h};
        if (std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
    }
    std::sort(v.begin(), v.end());
    return v;
}
static void applyVideo(SDL_Window* win, const Settings& s) {
    if (s.fullscreen) {
        if (s.fullW > 0) {
            SDL_DisplayMode want{}, got{};
            want.w = s.fullW; want.h = s.fullH;
            if (SDL_GetClosestDisplayMode(std::max(0, SDL_GetWindowDisplayIndex(win)), &want, &got) &&
                SDL_SetWindowDisplayMode(win, &got) == 0 && SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN) == 0) return;
        }
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(win, 0);
        SDL_SetWindowSize(win, s.windowW, s.windowH);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}
static void setFullscreen(SDL_Window* win, Settings& s, bool on) {
    s.fullscreen = on; applyVideo(win, s); profile::saveSettings(s);
}
static bool fullscreenKey(const SDL_Event& e) {
    return e.type == SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) && (e.key.keysym.mod & KMOD_ALT);
}

static SDL_Event keyEvent(SDL_Keycode k) {      // controller -> keyboard for menus and prompts
    SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.state = SDL_PRESSED;
    e.key.keysym.sym = k; e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    return e;
}

// menu slider: 50..300 % in 25 % steps as a bar of 11 cells
static std::string distanceBar(int pct) {
    int n = std::clamp((pct - 50) / 25, 0, 10);
    return std::string(size_t(n + 1), '#') + std::string(size_t(10 - n), '.') + " " + std::to_string(pct) + "%";
}

static bool runMenu(Options& opt, GameFS& fs, SDL_Window* win, Audio& audio, SaveGame& loaded) {
    static bool titleShown = false;
    Renderer r;
    std::string err;
    if (!r.init(err)) return fatal("Shader error: %s", err.c_str()), false;
    struct Img { Font f; int tex = -1; };
    auto load = [&](const char* path, Img& i) {
        if (!i.f.load(fs, path)) return false;
        i.tex = r.uploadFont(i.f.rgba.data(), i.f.texW, i.f.texH);
        return true;
    };
    Img title, back, credits, line, head, font, dark, usel, player, hiscore, pilotFont, portrait[4];
    load("DATA\\MENU\\GFX\\TITEL.DAT", title);
    load("DATA\\MENU\\GFX\\MENU.DAT", back);
    load("DATA\\MENU\\GFX\\CREDITS.DAT", credits);
    load("DATA\\MENU\\GFX\\LINIE.DAT", line);
    load("DATA\\MENU\\GFX\\HEADFONT.DAT", head);
    load("DATA\\MENU\\GFX\\FONT.DAT", font);
    load("DATA\\MENU\\GFX\\FONTDARK.DAT", dark);
    load("DATA\\MENU\\GFX\\FONTUSEL.DAT", usel);
    load("DATA\\MENU\\GFX\\PLAYER.DAT", player);
    load("DATA\\MENU\\GFX\\HIGSCOR.DAT", hiscore);
    load("DATA\\MENU\\GFX\\PILOTFNT.DAT", pilotFont);
    for (int p = 1; p < 4; p++) { char n[48]; std::snprintf(n, sizeof n, "DATA\\MENU\\GFX\\PILOT%d.DAT", p + 1); load(n, portrait[p]); }
    // start screens (0x9fe02): bluebyte.DAT, then MPATH.DAT (if present), then TITEL.DAT
    Img blueByte, mplayer;
    load("DATA\\MENU\\GFX\\bluebyte.DAT", blueByte);
    load("DATA\\MENU\\GFX\\MPATH.DAT", mplayer);
    // rotating logo beside the page headings (0x9b8d3: MENU\BACKGRND\EAL%04ld.DAT, 32 frames)
    Img logo[32];
    for (int k = 0; k < 32; k++) { char n[48]; std::snprintf(n, sizeof n, "DATA\\MENU\\BACKGRND\\EAL%04d.DAT", k); load(n, logo[k]); }
    TextDB gfx; gfx.load(fs, "DATA\\MENU\\TEXT\\GFX.CHR");
    TextPainter text;
    TextPainter::Face fHead{&head.f, head.tex}, fItem{&font.f, font.tex}, fDark{&dark.f, dark.tex}, fUsel{&usel.f, usel.tex}, fPilot{&pilotFont.f, pilotFont.tex};
    if (head.tex < 0) fHead = fItem;
    if (dark.tex < 0) fDark = fItem;
    if (usel.tex < 0) fUsel = fDark;
    if (pilotFont.tex < 0) fPilot = fDark;
    if (audio.ok() && opt.settings.music) audio.playMusic(musicTrack(fs, "TITEL"), true);

    enum Page { Title, Main, Areas, Missions, Credits, Pilot, Confirm, Options, Sound, TopTen, Detail, Keys, Logo, MPlayer } page =
        titleShown ? Main : blueByte.tex >= 0 ? Logo : Title;
    float pageTime = 0;                              // seconds on the current start screen
    Page timedPage = page;
    int sel = 0, area = opt.level;
    int& pilot = opt.settings.pilot;
    std::vector<std::pair<int, int>> saves;          // (level, mission) of the current pilot
    std::vector<std::string> missions;               // --allmissions: every mission file of the area
    std::vector<int> missionNo;
    auto listMissions = [&](int lvl) {
        missions.clear(); missionNo.clear();
        if (opt.allMissions) {
            char d[48];
            for (int m = 1; m < 90; m++) {                               // 9x are test missions
                std::snprintf(d, sizeof d, "DATA\\LEVEL%02d\\MISSIONS\\MISS_%02d.SCR", lvl, m);
                if (fs.resolve(d)) { char n[16]; std::snprintf(n, sizeof n, "MISS_%02d.SCR", m); missions.push_back(n); missionNo.push_back(m); }
            }
        } else {
            for (auto [l, m] : saves) if (l == lvl) { char n[16]; std::snprintf(n, sizeof n, "MISS_%02d.SCR", m); missions.push_back(n); missionNo.push_back(m); }
        }
    };
    struct Item { std::string label; int action; bool enabled = true; };
    enum { ANone, ANew, ALoad, APilot, AOptions, ATop, ANet, ACredits, AQuit, ABack, AYes, ANo, ASound, AMusic, AMusicVol, ASfxVol, ADetail, AViewRange, AViewDist, AFullscreen, AWinSize, AFullSize, AHdTex, ARumble, ALanguage, ABriefing, AKeys, AResetKeys, AStick0 = 280, AKey0 = 300,
           AArea0 = 100, AMission0 = 200 };
    std::function<void()> confirmYes; Page confirmBack = Main; int confirmText = 53;
    // top ten (0xa1e58): a finished game enters its score; the name is typed in place
    std::vector<HiScore> top = profile::loadTop();
    int entering = -1; std::string entryName;
    const bool hdPack = hdPackPresent(fs);
    int capturingStick = -1;                  // Keys page: the stick function waiting for a stick movement
    int capturing = -1;                       // Keys page: the function waiting for a key
    if (opt.played && titleShown) {
        opt.played = false;
        int rank = opt.lastScore > 0 ? profile::topRank(opt.lastScore) : -1;
        if (rank >= 0) {
            top.insert(top.begin() + rank, HiScore{"", opt.lastScore, pilot});
            top.resize(10);
            entering = rank; page = TopTen;
            SDL_StartTextInput();
        }
    }
    auto items = [&]() {
        std::vector<Item> v;
        if (page == Main) {
            v = {{gfx.get(1), ANew}, {gfx.get(2), ALoad}, {gfx.get(3), APilot}, {gfx.get(4), AOptions},
                 {gfx.get(5), ATop}, {gfx.get(6), ANet, false}, {gfx.get(7), ACredits}, {gfx.get(8), AQuit}};
        } else if (page == Areas) {
            bool any = false;
            for (int a = 1; a <= 6; a++) {
                bool has = opt.allMissions || std::any_of(saves.begin(), saves.end(), [&](auto& s) { return s.first == a; });
                if (has) { v.push_back({gfx.get(143 + a), AArea0 + a}); any = true; }
            }
            if (!any) v.push_back({gfx.get(194), ANone, false});
            v.push_back({gfx.get(16), ABack});
        } else if (page == Missions) {
            for (size_t m = 0; m < missions.size(); m++) {
                int n = missionNo[m];
                std::string l = n <= 16 ? gfx.get(150 + n) : lang::tr("Mission") + " " + std::to_string(n);
                v.push_back({l, AMission0 + int(m)});
            }
            v.push_back({gfx.get(16), ABack});
        } else if (page == Confirm) {
            v = {{gfx.get(99), AYes}, {gfx.get(100), ANo}};
        } else if (page == Options) {
            v = {{gfx.get(56), ASound}, {gfx.get(57), ANone, false}, {gfx.get(58), ADetail}, {gfx.get(59), AKeys},
                 {lang::tr("Rumble") + ": " + gfx.get(opt.settings.rumble == 0 ? 72 : opt.settings.rumble == 1 ? 127 : 129), ARumble},
                 {lang::tr("Language") + ": " + lang::displayName(lang::code()), ALanguage}, {gfx.get(16), ABack}};
        } else if (page == Keys) {
            auto keyName = [&](int v) -> std::string {
                if (v <= 0) return "-";
                if (v == kMouse + SDL_BUTTON_LEFT) return gfx.get(282);
                if (v == kMouse + SDL_BUTTON_RIGHT) return gfx.get(283);
                if (v >= kMouse) return lang::tr("Mouse") + " " + std::to_string(v - kMouse);
                std::string n = SDL_GetScancodeName(SDL_Scancode(v));
                for (auto& c : n) c = char(std::toupper((unsigned char)c));
                return n.empty() ? "?" : n;
            };
            for (int ci = 0; ci < CControlCount; ci++) {
                const int c = kControlOrder[ci];
                std::string l = gfx.get(kControlText[c]) + ":  ";
                if (capturing == c) l += "?";
                else {
                    l += keyName(opt.settings.keys[c][0]); if (opt.settings.keys[c][1] > 0) l += " / " + keyName(opt.settings.keys[c][1]);
                    if (opt.settings.pad[c] >= pad::kPad) l += " / " + lang::tr(pad::name(opt.settings.pad[c] - pad::kPad));
                }
                v.push_back({l, AKey0 + c});
            }
            static const char* stickText[SCount] = {"Stick forward/back", "Stick turn", "Stick drift", "Stick height"};
            for (int s = 0; s < SCount; s++)
                v.push_back({lang::tr(stickText[s]) + ":  " + (capturingStick == s ? std::string("?") : lang::tr(pad::stickName(opt.settings.stick[s]))), AStick0 + s});
            v.push_back({gfx.get(52), AResetKeys});
            v.push_back({gfx.get(16), ABack});
        } else if (page == Detail) {
            auto trim = [](std::string s) { size_t a = s.find_first_not_of(' '); return a == std::string::npos ? s : s.substr(a); };
            v = {{trim(gfx.get(124)) + " " + gfx.get(127 + opt.settings.viewRange), AViewRange},
                 {lang::tr("View distance") + ": " + distanceBar(opt.settings.viewDistance), AViewDist},
                 {lang::tr("HD textures") + ": " + (!hdPack ? lang::tr("Not installed") : gfx.get(opt.settings.hdTextures ? 71 : 72)), AHdTex, hdPack},
                 {lang::tr("Fullscreen") + ": " + gfx.get(opt.settings.fullscreen ? 71 : 72), AFullscreen},
                 {lang::tr("Window size") + ": " + std::to_string(opt.settings.windowW) + "x" + std::to_string(opt.settings.windowH), AWinSize},
                 {lang::tr("Fullscreen size") + ": " + (opt.settings.fullW > 0 ? std::to_string(opt.settings.fullW) + "x" + std::to_string(opt.settings.fullH) : lang::tr("Desktop")), AFullSize},
                 {gfx.get(16), ABack}};
        } else if (page == Sound) {
            auto trim = [](std::string s) { size_t a = s.find_first_not_of(' '); return a == std::string::npos ? s : s.substr(a); };
            v = {{trim(gfx.get(88)) + " " + gfx.get(opt.settings.music ? 94 : 96), AMusic},
                 {trim(gfx.get(89)) + " " + std::to_string(opt.settings.musicVolume) + "%", AMusicVol},
                 {trim(gfx.get(90)) + " " + std::to_string(opt.settings.sfxVolume) + "%", ASfxVol},
                 {trim(gfx.get(169)) + " " + gfx.get(170 + opt.settings.briefing), ABriefing},
                 {gfx.get(16), ABack}};
        }
        return v;
    };
    auto heading = [&]() {
        switch (page) {
        case Main: return gfx.get(0);
        case Areas: return gfx.get(143);
        case Missions: return gfx.get(150);
        case Confirm: return gfx.get(confirmText);
        case Options: return gfx.get(55);
        case Sound: return gfx.get(86);
        case Detail: return gfx.get(123);
        case Keys: return gfx.get(10);
        default: return std::string();
        }
    };
    auto applyVolumes = [&]() {
        audio.masterVolume = 0.8f * float(opt.settings.sfxVolume) / 100.f;
        audio.musicVolume = 0.55f * float(opt.settings.musicVolume) / 100.f / std::max(0.01f, audio.masterVolume);
    };
    applyVolumes();
    auto newGameState = [&]() {
        opt.level = 1; opt.mission = "MISS_01.SCR"; opt.carryScore = 0; opt.carryTicks = 0; opt.haveArms = false; opt.load = nullptr;
    };
    // layout in the 640x480 picture: content panel x 130..620, y 20..465
    const float panelX0 = 140, panelX1 = 610;
    auto rowH = [&]() { return page == Missions ? 21.f : page == Keys ? 16.f : 30.f; };   // Keys: 22 rows (16 controls, 4 sticks, reset, back)
    auto firstY = [&]() { return page == Keys ? 100.f : 130.f; };
    // CHOOSE PILOT: the four portraits of PLAYER.DAT
    const float pq[4][2] = {{10, 8}, {330, 8}, {10, 248}, {330, 248}};
    const float pw = 300, ph = 225;
    int pilotHover = pilot;
    titleShown = true;
    bool quit = false, start = false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    const Uint32 menuStart = SDL_GetTicks();
    float s = 1, ox = 0;
    int mx = -1, my = -1;
    auto goMain = [&](int item) { page = Main; sel = item; };
    Uint32 prevTicks = SDL_GetTicks();
    float menuTime = 0;                              // drives the rotating logo
    while (!quit && !start) {
        {
            Uint32 now = SDL_GetTicks();
            float dt = float(now - prevTicks) / 1000.f; prevTicks = now;
            menuTime += dt;
            if (page != timedPage) { timedPage = page; pageTime = 0; } else pageTime += dt;
        }
        int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
        s = float(h) / 480.f; ox = (float(w) - 640.f * s) * 0.5f;
        if (ox < 0) { s = float(w) / 640.f; ox = 0; }
        auto list = items();
        auto toVirtual = [&](int px, int py, float& vx, float& vy) {
            int dw, dh, ww, wh; SDL_GL_GetDrawableSize(win, &dw, &dh); SDL_GetWindowSize(win, &ww, &wh);
            vx = (float(px * dw / std::max(1, ww)) - ox) / s; vy = float(py * dh / std::max(1, wh)) / s;
        };
        auto hit = [&](float vx, float vy) {
            if (vx < panelX0 || vx > panelX1) return -1;
            int i = int(std::floor((vy - firstY() + 4.f) / rowH()));
            return i >= 0 && i < int(list.size()) ? i : -1;
        };
        auto pilotAt = [&](float vx, float vy) {
            for (int p = 0; p < 4; p++) if (vx >= pq[p][0] && vx < pq[p][0] + pw && vy >= pq[p][1] && vy < pq[p][1] + ph) return p;
            return -1;
        };
        auto adjust = [&](int a, int dir) {
            if (a == ABriefing) { opt.settings.briefing = (opt.settings.briefing + (dir > 0 ? 1 : 2)) % 3; profile::saveSettings(opt.settings); return; }
            if (a >= AStick0 && a < AStick0 + SCount) {          // left / right: cycle - , LEFT X, LEFT X INV, ... RIGHT Y INV
                int& b = opt.settings.stick[a - AStick0];
                b = (b + 1 + (dir > 0 ? 1 : 8)) % 9 - 1;
                profile::saveSettings(opt.settings); return;
            }
            if (a == ARumble) {
                opt.settings.rumble = (opt.settings.rumble + (dir > 0 ? 1 : 2)) % 3; profile::saveSettings(opt.settings);
                pad::setRumbleScale(opt.settings.rumble * 0.5f); pad::rumble(0.6f, 250);       // test pulse
                return;
            }
            if (a == AHdTex) { opt.settings.hdTextures = !opt.settings.hdTextures; profile::saveSettings(opt.settings); return; }
            if (a == ALanguage) {                     // OpenAssault: text language, the menu texts reload at once
                auto v = lang::available();
                auto it = std::find(v.begin(), v.end(), lang::code());
                size_t k = it == v.end() ? 0 : size_t(it - v.begin());
                k = (k + (dir > 0 ? 1 : v.size() - 1)) % v.size();
                opt.settings.language = v[k]; lang::set(v[k]); profile::saveSettings(opt.settings);
                gfx = TextDB(); gfx.load(fs, "DATA\\MENU\\TEXT\\GFX.CHR");
                return;
            }
            if (a == AFullscreen) { setFullscreen(win, opt.settings, !opt.settings.fullscreen); return; }
            if (a == AWinSize) {
                auto v = windowModes();
                size_t k = 0;
                for (size_t q = 0; q < v.size(); q++)       // the current size, or the largest listed one below it
                    if (v[q].first <= opt.settings.windowW && v[q].second <= opt.settings.windowH) k = q;
                k = (k + (dir > 0 ? 1 : v.size() - 1)) % v.size();
                opt.settings.windowW = v[k].first; opt.settings.windowH = v[k].second;
                if (!opt.settings.fullscreen) applyVideo(win, opt.settings);
                profile::saveSettings(opt.settings); return;
            }
            if (a == AFullSize) {                       // Desktop, then the display's modes
                auto v = fullscreenModes();
                int cur = 0;                            // 0 = desktop, 1.. = v[cur - 1]
                for (size_t q = 0; q < v.size(); q++) if (v[q].first == opt.settings.fullW && v[q].second == opt.settings.fullH) cur = int(q) + 1;
                int n = int(v.size()) + 1;
                cur = (cur + (dir > 0 ? 1 : n - 1)) % n;
                opt.settings.fullW = cur ? v[size_t(cur - 1)].first : 0; opt.settings.fullH = cur ? v[size_t(cur - 1)].second : 0;
                if (opt.settings.fullscreen) applyVideo(win, opt.settings);
                profile::saveSettings(opt.settings); return;
            }
            if (a == AViewDist) { opt.settings.viewDistance = std::clamp(opt.settings.viewDistance + dir * 25, 50, 300); profile::saveSettings(opt.settings); return; }
            if (a == AViewRange) { opt.settings.viewRange = (opt.settings.viewRange + (dir > 0 ? 1 : 2)) % 3; profile::saveSettings(opt.settings); return; }
            if (a == AMusic) { opt.settings.music = !opt.settings.music; if (opt.settings.music) audio.playMusic(musicTrack(fs, "TITEL"), true); else audio.stopMusic(); }
            else if (a == AMusicVol) opt.settings.musicVolume = std::clamp(opt.settings.musicVolume + dir * 10, 0, 100);
            else if (a == ASfxVol) { opt.settings.sfxVolume = std::clamp(opt.settings.sfxVolume + dir * 10, 0, 100); }
            else return;
            applyVolumes();
            profile::saveSettings(opt.settings);
        };
        // start screens: Blue Byte 170 ticks, MPATH 100 ticks (timer 0x261044), a key or click leaves them earlier
        auto nextStart = [&]() { page = page == Logo && mplayer.tex >= 0 ? MPlayer : Title; };
        auto activate = [&](int i) {
            if (page == Logo || page == MPlayer) { nextStart(); return; }
            if (page == Title) {
                // a fresh start asks for the pilot first (the Pilot page is the original's first menu after the title)
                static bool pilotAsked = false;
                if (!pilotAsked) { pilotAsked = true; page = Pilot; pilotHover = pilot; return; }
                goMain(0); return;
            }
            if (page == Credits) { goMain(6); return; }
            if (page == TopTen) { if (entering < 0) goMain(4); return; }
            if (page == Pilot) {
                if (i >= 0 && i < 4) { pilot = i; profile::saveSettings(opt.settings); }
                goMain(2); return;
            }
            if (i < 0 || i >= int(list.size()) || !list[size_t(i)].enabled) return;
            int a = list[size_t(i)].action;
            if (a == ANew) {
                auto begin = [&]() { profile::erase(pilot); newGameState(); opt.difficulty = pilot; start = true; };
                SaveGame cur;
                // 0xa0dde: a pilot with progress is asked first; New Game deletes his savegames
                if (profile::latest(pilot, cur) && (cur.level != 1 || cur.mission != 1)) {
                    confirmYes = begin; confirmBack = Main; confirmText = 53; page = Confirm; sel = 1;
                } else begin();
            }
            else if (a == ALoad) { saves = profile::list(pilot); page = Areas; sel = 0; }
            else if (a == APilot) { page = Pilot; pilotHover = pilot; }
            else if (a == AOptions) { page = Options; sel = 0; }
            else if (a == ASound) { page = Sound; sel = 0; }
            else if (a == ADetail) { page = Detail; sel = 0; }
            else if (a == AKeys) { page = Keys; sel = 0; capturing = -1; }
            else if (a == AResetKeys) { defaultControls(opt.settings.keys); defaultPad(opt.settings.pad); defaultSticks(opt.settings.stick); profile::saveSettings(opt.settings); }
            else if (a >= AKey0 && a < AKey0 + CControlCount) capturing = a - AKey0;
            else if (a >= AStick0 && a < AStick0 + SCount) capturingStick = a - AStick0;
            else if (a == AMusic || a == AMusicVol || a == ASfxVol || a == AViewRange || a == ABriefing) adjust(a, 1);
            else if (a == AFullscreen || a == AHdTex || a == ARumble || a == ALanguage || a == AWinSize || a == AFullSize) adjust(a, 1);
            else if (a == AViewDist) adjust(a, opt.settings.viewDistance >= 300 ? -10 : 1);
            else if (a == ATop) { top = profile::loadTop(); page = TopTen; }
            else if (a == ACredits) page = Credits;
            else if (a == AQuit) quit = true;
            else if (a == AYes) { auto f = confirmYes; page = confirmBack; sel = 0; if (f) f(); }
            else if (a == ANo) { page = confirmBack; sel = 0; if (confirmBack == Pilot) pilotHover = pilot; }
            else if (a == ABack) {
                if (page == Missions) { page = Areas; sel = 0; }
                else if (page == Sound || page == Detail || page == Keys) { sel = page == Detail ? 2 : page == Keys ? 3 : 0; page = Options; }
                else goMain(page == Options ? 3 : 1);
            }
            else if (a >= AMission0) {
                int m = a - AMission0;
                newGameState();
                opt.difficulty = pilot; opt.level = area; opt.mission = missions[size_t(m)];
                if (!opt.allMissions && profile::read(pilot, area, missionNo[size_t(m)], loaded)) {
                    opt.carryScore = loaded.score; opt.carryTicks = loaded.playTicks; opt.carryArms = loaded.arms; opt.haveArms = true;
                    opt.mission = loaded.missionFile;
                    if (loaded.hasWorld) opt.load = &loaded;
                }
                start = true;
            }
            else if (a > AArea0) {
                area = a - AArea0; listMissions(area);
                char d[32]; std::snprintf(d, sizeof d, "DATA\\LEVEL%02d", area);
                if (!fs.resolve(d) || missions.empty()) return;
                page = Missions; sel = 0;
            }
        };
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (fullscreenKey(e)) { setFullscreen(win, opt.settings, !opt.settings.fullscreen); continue; }
            pad::event(e);
            if (capturingStick >= 0) {                              // move a stick to assign it, Esc / B / Start cancel
                int ax = pad::stickMoved(e);
                bool cancel = (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) || pad::lastPressed() == SDL_CONTROLLER_BUTTON_B || pad::lastPressed() == SDL_CONTROLLER_BUTTON_START;
                if (ax >= 0) {
                    for (int s = 0; s < SCount; s++) if (s != capturingStick && opt.settings.stick[s] >= 0 && opt.settings.stick[s] / 2 == ax) opt.settings.stick[s] = -1;
                    opt.settings.stick[capturingStick] = ax * 2;
                    profile::saveSettings(opt.settings);
                }
                if (ax >= 0 || cancel) { capturingStick = -1; continue; }
                if (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERAXISMOTION) continue;
            }
            if (capturing >= 0 && pad::lastPressed() >= 0) {      // ADJUST CONTROLS with a controller button (Start cancels)
                int b = pad::lastPressed();
                if (b != SDL_CONTROLLER_BUTTON_START) {
                    int v = pad::kPad + b;
                    for (int c = 0; c < CControlCount; c++) if (opt.settings.pad[c] == v) opt.settings.pad[c] = 0;
                    opt.settings.pad[capturing] = v;
                    profile::saveSettings(opt.settings);
                }
                capturing = -1;
                continue;
            }
            if (capturing < 0) if (SDL_Keycode pk = pad::menuKey(e, false)) e = keyEvent(pk);
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_TEXTINPUT && entering >= 0) {
                for (const char* c = e.text.text; *c; c++)
                    if (entryName.size() < 13 && *c >= 32 && *c < 127) entryName += char(std::toupper((unsigned char)*c));
                top[size_t(entering)].name = entryName;
            } else if (capturing >= 0 && (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN)) {
                // ADJUST CONTROLS: the pressed key / button becomes the first key of the function (the old one the second);
                // Esc cancels, Backspace clears the function
                int v = e.type == SDL_KEYDOWN ? int(e.key.keysym.scancode) : kMouse + e.button.button;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) { capturing = -1; continue; }
                int* slot = opt.settings.keys[capturing];
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKSPACE) { slot[0] = slot[1] = 0; }
                else if (slot[0] != v) { slot[1] = slot[0]; slot[0] = v; }
                for (int c = 0; c < CControlCount; c++)          // a key does one thing only
                    if (c != capturing) for (int q = 0; q < 2; q++) if (opt.settings.keys[c][q] == v) opt.settings.keys[c][q] = 0;
                if (slot[1] == v) slot[1] = 0;
                capturing = -1;
                profile::saveSettings(opt.settings);
                continue;
            } else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (page == Main && entering < 0) {      // 0xa0458: typed codes in the main menu
                    static std::string typed;
                    if (k >= 32 && k < 127) { typed += char(k); if (typed.size() > 16) typed.erase(0, typed.size() - 16); }
                    auto ends = [&](const char* c) { std::string x = c; return typed.size() >= x.size() && typed.compare(typed.size() - x.size(), x.size(), x) == 0; };
                    // every code plays speech 0x64 (SPEAK_E\121.WAV, 0x58060); "level6 " only toggles 0x253c12,
                    // which nothing in the exe reads
                    auto cheatSound = [&]() { if (audio.ok()) audio.play(audio.sound("SPEAK_E\\121.WAV"), 1.f, 0.f, false); };
                    if (ends("oh dear")) { opt.cheats = true; typed.clear(); cheatSound(); }
                    if (ends("level6 ")) { opt.level6 = !opt.level6; typed.clear(); cheatSound(); }
                    if (ends("emertxe")) { opt.assault = !opt.assault; typed.clear(); cheatSound(); }
                    // with the cheats on, Alt+1..6 starts that operation area (0xa0748)
                    if (opt.cheats && (e.key.keysym.mod & KMOD_ALT) && k >= SDLK_1 && k <= SDLK_6) {
                        newGameState(); opt.difficulty = pilot; opt.level = int(k - SDLK_0); start = true; continue;
                    }
                }
                if (entering >= 0) {
                    if (k == SDLK_BACKSPACE && !entryName.empty()) entryName.pop_back();
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) {
                        if (entryName.empty()) entryName = "PILOT";
                        top[size_t(entering)].name = entryName;
                        profile::saveTop(top);
                        entering = -1; SDL_StopTextInput();
                        continue;
                    }
                    top[size_t(entering)].name = entryName;
                    continue;
                }
                if (page == Title || page == Credits || page == TopTen || page == Logo || page == MPlayer) { activate(0); continue; }
                if (page == Pilot) {
                    // arrows move between the four portraits; Enter picks; Delete clears the pilot's games
                    if (k == SDLK_LEFT || k == SDLK_RIGHT) pilotHover ^= 1;
                    else if (k == SDLK_UP || k == SDLK_DOWN) pilotHover ^= 2;
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) activate(pilotHover);
                    else if (k == SDLK_ESCAPE) activate(-1);
                    else if (k == SDLK_DELETE) {
                        int victim = pilotHover;
                        confirmYes = [&, victim]() { profile::erase(victim); };
                        confirmBack = Pilot; confirmText = 130; page = Confirm; sel = 1;
                    }
                    continue;
                }
                auto step = [&](int d) {
                    int n = int(list.size());
                    for (int t = 0; t < n; t++) { sel = (sel + d + n) % n; if (list[size_t(sel)].enabled) break; }
                };
                if (k == SDLK_UP) step(-1);
                else if (k == SDLK_DOWN) step(1);
                else if (k == SDLK_LEFT || k == SDLK_RIGHT) { if (sel < int(list.size())) adjust(list[size_t(sel)].action, k == SDLK_LEFT ? -1 : 1); }
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) activate(sel);
                else if (k == SDLK_ESCAPE) {
                    if (page == Main) sel = int(list.size()) - 1;
                    else if (page == Confirm) activate(1);
                    else activate(int(list.size()) - 1);
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                if (SDL_GetTicks() - menuStart < 400) continue;              // ignore the warp after leaving relative mode
                mx = e.motion.x; my = e.motion.y;
                float vx, vy; toVirtual(mx, my, vx, vy);
                if (page == Pilot) { int p = pilotAt(vx, vy); if (p >= 0) pilotHover = p; continue; }
                int i = hit(vx, vy);
                if (i >= 0 && list[size_t(i)].enabled) sel = i;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                if (entering >= 0) continue;
                if (page == Title || page == Credits || page == TopTen || page == Logo || page == MPlayer) { activate(0); continue; }
                float vx, vy; toVirtual(e.button.x, e.button.y, vx, vy);
                if (page == Pilot) { activate(pilotAt(vx, vy)); continue; }
                int i = hit(vx, vy);
                if (i < 0) continue;
                int a = list[size_t(i)].action;
                if (a == AMusicVol || a == ASfxVol || a == AViewDist) adjust(a, e.button.button == SDL_BUTTON_LEFT ? 1 : -1);
                else if (e.button.button == SDL_BUTTON_LEFT) activate(i);
            }
        }
        if (quit || start) break;
        if ((page == Logo && pageTime >= 170.f / 60.f) || (page == MPlayer && pageTime >= 100.f / 60.f)) nextStart();
        list = items();
        if (sel >= int(list.size()) || (sel >= 0 && !list[size_t(sel)].enabled)) {
            sel = 0;
            while (sel < int(list.size()) - 1 && !list[size_t(sel)].enabled) sel++;
        }
        // draw
        std::vector<Renderer::HudQuad> q;
        text.sx = text.sy = s;
        auto blit = [&](const Img& i, float x0, float y0, float x1, float y1) {
            if (i.tex < 0) return;
            Renderer::HudQuad b{x0 * s, y0 * s, x1 * s, y1 * s, {255, 255, 255, 255}};
            b.font = i.tex; b.u0 = 0; b.u1 = 1; b.v0 = 0; b.v1 = 1;
            q.push_back(b);
        };
        auto pic = [&](const Img& i) { blit(i, 0, 0, 640, 480); };
        auto pilotCorner = [&]() {        // the chosen pilot in the top left frame (MENU.DAT shows the first one)
            if (pilot > 0 && portrait[pilot].tex >= 0) blit(portrait[pilot], 0, 0, float(portrait[pilot].f.texW), float(portrait[pilot].f.texH));
        };
        const uint8_t white[4] = {255, 255, 255, 255};
        const float cx = (panelX0 + panelX1) * 0.5f;
        auto headLine = [&](const std::string& hd, float y) {
            float hw = text.width(fHead, hd);
            text.draw(q, fHead, hd, cx - hw / 2, y, white);
            if (line.tex >= 0) blit(line, cx - 210.f, y + 30.f, cx + 211.f, y + 33.f);
        };
        auto centred = [&](const TextPainter::Face& f, const std::string& t, float y) { text.draw(q, f, t, 320.f - text.width(f, t) / 2, y, white); };
        if (page == Logo) pic(blueByte);
        else if (page == MPlayer) { pic(mplayer); centred(fItem, gfx.get(182), 431); }     // 0xa0032: GFX 182 "Press any key..."
        else if (page == Title) {
            pic(title);
            centred(fItem, gfx.get(182), 460);                                                // 0xa0160
            text.draw(q, fPilot, "VERSION R 1.2.3 3DFX", 4, 470, white);                    // 0xa0194 (PILOTFNT)
        } else if (page == Credits) pic(credits.tex >= 0 ? credits : title);
        else if (page == Pilot) {
            pic(player);
            for (int p = 0; p < 4; p++) {
                float x0 = pq[p][0], y0 = pq[p][1];
                if (p != pilotHover) text.box(q, x0, y0, x0 + pw, y0 + ph, std::array<uint8_t, 4>{0, 0, 0, 110}.data());
                // 0x9eaa1: difficulty under the badge, then area / mission / points of the pilot's game
                std::string diff = gfx.get(188 + p);
                text.draw(q, fPilot, diff, x0 + 44 - text.width(fPilot, diff) / 2, y0 + 216, white);
                SaveGame g; int lvl = 1, mis = 1, pts = 0;
                if (profile::latest(p, g)) { lvl = g.level; mis = g.mission; pts = g.score; }
                std::string areaText = gfx.get(100 + lvl) + " " + gfx.get(296) + " " + std::to_string(profile::areaLimit(p));   // GFX 296 "of"
                text.draw(q, fPilot, areaText, x0 + 114, y0 + 199, white);
                text.draw(q, fPilot, gfx.get(150 + std::min(mis, 16)), x0 + 114, y0 + 208, white);
                text.draw(q, fPilot, gfx.get(181) + " " + std::to_string(pts), x0 + 114, y0 + 217, white);
                if (p == pilot) {
                    const uint8_t sel4[4] = {255, 210, 60, 255};
                    text.box(q, x0, y0, x0 + pw, y0 + 2, sel4); text.box(q, x0, y0 + ph - 2, x0 + pw, y0 + ph, sel4);
                    text.box(q, x0, y0, x0 + 2, y0 + ph, sel4); text.box(q, x0 + pw - 2, y0, x0 + pw, y0 + ph, sel4);
                }
            }
        } else if (page == TopTen) {
            pic(hiscore.tex >= 0 ? hiscore : back);
            pilotCorner();
            headLine(gfx.get(413), 50);
            const float cw = 9.f, x0 = panelX0 + 6;
            text.draw(q, fDark, gfx.get(414), x0 + cw * 4, 106, white);
            text.draw(q, fDark, gfx.get(415), x0 + cw * 19, 106, white);
            text.draw(q, fDark, gfx.get(416), x0 + cw * 31, 106, white);
            for (int i = 0; i < 10 && i < int(top.size()); i++) {
                const HiScore& hs = top[size_t(i)];
                std::string nm = hs.name;
                bool blink = i == entering && (SDL_GetTicks() / 300) % 2 == 0;
                if (i == entering) nm += blink ? "_" : " ";
                char buf[96];
                std::snprintf(buf, sizeof buf, "%2d.", i + 1);
                float y = 136.f + float(i) * 26.f;
                const auto& f = i == entering ? fItem : fDark;
                text.draw(q, f, buf, x0, y, white);
                std::string dots = nm; while (dots.size() < 13) dots += '.';
                text.draw(q, f, dots, x0 + cw * 4, y, white);
                std::snprintf(buf, sizeof buf, "%07d", hs.score);
                text.draw(q, f, buf, x0 + cw * 19, y, white);
                text.draw(q, f, gfx.get(188 + std::clamp(hs.diff, 0, 3)), x0 + cw * 31, y, white);
            }
        } else {
            pic(back);
            pilotCorner();
            headLine(heading(), 62);
            {   // 0xa17ef: two copies of the rotating logo beside the heading; the counter grows by 8 per menu frame up
                // to 0x200 and the frame is counter/16 (here 60 frames a second). They stand 100 pixels from the
                // centre (130 with the French texts); a translated heading wider than that pushes them out to 130.
                const Img& lg = logo[int(menuTime * 30.f) % 32];
                if (lg.tex >= 0) {
                    float lw = float(lg.f.texW), lh = float(lg.f.texH), ly = 62.f + 12.f - lh / 2;
                    const float half = text.width(fHead, heading()) / 2;
                    float d = std::max(half > 68.f ? 130.f : 100.f, half + 8.f + lw / 2);     // never over the heading
                    blit(lg, cx - d - lw / 2, ly, cx - d + lw / 2, ly + lh);
                    blit(lg, cx + d - lw / 2, ly, cx + d + lw / 2, ly + lh);
                }
            }
            if (page == Main && opt.cheats) text.draw(q, fDark, lang::tr("CHEAT CODES ACTIVATED"), 40, 460, white);
            if (page == Main && opt.assault) text.draw(q, fDark, lang::tr("ASSAULT MODE ACTIVATED"), 40, 446, white);
            for (size_t i = 0; i < list.size(); i++) {
                const auto& f = !list[i].enabled ? fUsel : int(i) == sel ? fItem : fDark;
                float iw = text.width(f, list[i].label);
                text.draw(q, f, list[i].label, cx - iw / 2, firstY() + float(i) * rowH(), white);
            }
        }
        for (auto& x : q) { x.x0 += ox; x.x1 += ox; }
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        r.drawHud(q, w, h);
        if (const char* shot = std::getenv("XA_MENUSHOT")) {           // testing: XA_MENUSHOT=file.bmp [XA_MENUPAGE=n]
            static int frames = 0;
            if (frames == 0) {
                if (const char* pg = std::getenv("XA_MENUPAGE")) {
                    page = Page(std::atoi(pg));
                    if (page == Areas || page == Missions) { saves = profile::list(pilot); area = 1; listMissions(1); }
                }
                if (const char* nm = std::getenv("XA_MENUNAME")) { entering = 2; top.insert(top.begin() + 2, HiScore{nm, 420000, pilot}); top.resize(10); page = TopTen; }
            }
            if (++frames == 3) {
                std::vector<uint8_t> px(size_t(w) * h * 4);
                gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
                SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
                for (int y = 0; y < h; y++) std::memcpy((uint8_t*)sf->pixels + y * sf->pitch, &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
                SDL_SaveBMP(sf, shot); SDL_FreeSurface(sf);
                quit = true;
            }
        }
        SDL_GL_SwapWindow(win);
    }
    if (entering >= 0) { SDL_StopTextInput(); top[size_t(entering)].name = entryName.empty() ? "PILOT" : entryName; profile::saveTop(top); }
    if (audio.ok()) audio.stopMusic();
    opt.difficulty = pilot;
    return !quit;
}

enum { RunQuit = 0, RunMenu = 1, RunNextLevel = 2 };

static int runGame(Options& opt, GameFS& fs, SDL_Window* win, Audio& audio) {
    char lv[32]; std::snprintf(lv, sizeof lv, "DATA\\LEVEL%02d", opt.level);
    if (!fs.resolve(lv)) return fatal("Game data not found: %s", lv), RunQuit;
    int exitCode = RunQuit;
    TextureLib tex; ModelLib models; World world; TypeDB types; DefDB defs; SpriteLib sprites;
    if (!tex.load(fs, lv)) return (fatal("%s", tex.error.c_str()), RunQuit);
    if (!models.load(fs, lv)) return (fatal("%s", models.error.c_str()), RunQuit);
    if (!world.load(fs, lv)) return (fatal("%s", world.error.c_str()), RunQuit);
    types.load(fs, lv);
    defs.load(fs, lv, opt.difficulty);
    sprites.load(fs);
    std::printf("Level %d: %zu textures, %zu models (%d failed), %zu placed objects\n",
                opt.level, tex.pages().size(), models.models().size(), models.failed, world.objects().size());

    Renderer r;
    std::string err;
    if (!r.init(err)) return (fatal("Shader error: %s", err.c_str()), RunQuit);
    r.uploadTextures(tex, &fs, opt.settings.hdTextures && hdPackPresent(fs));
    r.uploadModels(models);
    r.uploadSprites(sprites);
    Font fontSmall, fontBig;
    TextPainter text;
    Hud hud2d;
    hud2d.load(fs, r);
    if (fontSmall.load(fs, "DATA\\MENU\\FONT\\S_HIGH.DAT")) text.small = {&fontSmall, r.uploadFont(fontSmall.rgba.data(), fontSmall.texW, fontSmall.texH)};
    if (fontBig.load(fs, "DATA\\MENU\\FONT\\B_HIGH.DAT")) text.big = {&fontBig, r.uploadFont(fontBig.rgba.data(), fontBig.texW, fontBig.texH)};
    TextDB missionText, ingameText;
    missionText.load(fs, std::string(lv) + "\\MISSIONS\\MISSION.CHR");
    ingameText.load(fs, "DATA\\GLOBAL\\TEXT\\INGAME.CHR");
    r.setWorld(world, models, types);
    if (!opt.model.empty()) {
        int mi = models.find(opt.model);
        if (mi < 0) return (fatal("Model not found: %s", opt.model.c_str()), RunQuit);
        r.clearInstances();
        r.addInstance(mi, models.models()[size_t(mi)].objClass, {0, 0, 0}, 0);
        if (!opt.haveCam) {
            float rad = std::max(1.f, models.models()[size_t(mi)].radius * r.unitScale);
            opt.haveCam = true;
            float c[5] = {0, -rad * 2.2f, rad * 1.1f, 90, -25};
            std::copy(c, c + 5, opt.cam);
        }
    }
    // Collision-only meshes (*col.3dw) and the sky dome are not drawn by default.
    for (const auto& s : world.sets)
        if (s.find("col.3dw") != std::string::npos || s.rfind("sky", 0) == 0 || isTemplateSet(s)) r.hiddenSets.insert(s);
    { std::string h = opt.hide; for (auto& c : h) if (c == ',') c = ' '; std::istringstream is(h); std::string s; while (is >> s) r.hiddenSets.insert(s); }

    // Player helicopter = the placed object of type "ADV" (if the level has one).
    int playerWorld = -1;
    for (size_t i = 0; i < world.objects().size(); i++) {
        const ObjectType* t = types.get(world.objects()[i].objClass);
        if (t && (t->name == "ADV" || t->name == "ADV_PANZA")) { playerWorld = int(i); break; }
    }
    if (std::getenv("XA_VEH"))
        for (size_t i = 0; i < world.objects().size(); i++) {
            const ObjectType* t = types.get(world.objects()[i].objClass);
            if (!t) continue;
            std::string b = t->behavior;
            if (b.rfind("ADV", 0) == 0 || b == "TANK_BASE" || b == "HELI_BASE" || b == "GROUND_ONLY_TANK" || b == "GROUND_ONLY_HELI")
                std::printf("veh %zu %s beh %s set %s at %.0f %.0f %.0f\n", i, t->name.c_str(), b.c_str(), world.objects()[i].set.c_str(),
                            world.objects()[i].pos[0] * 0.001f, world.objects()[i].pos[1] * 0.001f, world.objects()[i].pos[2] * 0.001f);
        }
    Renderer::Instance* playerInst = opt.model.empty() ? r.instanceForWorldObject(playerWorld) : nullptr;
    CollisionWorld coll;
    Heli heli;
    // The player's vehicle: ADV (ADV_HELI helicopter) or, on tank levels, ADV_PANZA (ADV_TANK).
    bool playerTank = playerWorld >= 0 && types.get(world.objects()[size_t(playerWorld)].objClass) &&
                      types.get(world.objects()[size_t(playerWorld)].objClass)->behavior == "ADV_TANK";
    // Vehicle change (RE 0x37ec5): the level's ADV / ADV_PANZA objects are the vehicle templates;
    // TANK_BASE / HELI_BASE objects are the places where a vehicle is swapped.
    int heliTemplate = -1, tankTemplate = -1;
    std::vector<int> tankBases, heliBases;
    for (size_t i = 0; i < world.objects().size(); i++) {
        const ObjectType* t = types.get(world.objects()[i].objClass);
        if (!t) continue;
        if (t->behavior == "ADV_HELI" && heliTemplate < 0 && int(i) != playerWorld) heliTemplate = int(i);
        if (t->behavior == "ADV_TANK" && tankTemplate < 0 && int(i) != playerWorld) tankTemplate = int(i);
        if (t->behavior == "TANK_BASE") tankBases.push_back(int(i));
        if (t->behavior == "HELI_BASE") heliBases.push_back(int(i));
    }
    if (playerTank) tankTemplate = playerWorld; else heliTemplate = playerWorld;
    auto configureVehicle = [&](Heli& h) {
        h.tank = playerTank;
        int src = playerTank ? tankTemplate : -1;
        if (src >= 0) {
            const Model& pm = models.models()[world.objects()[size_t(src)].proto];
            h.clearance = std::max(0.5f, -float(pm.bboxMin[2]) * r.unitScale);
            h.radius = 3.f;
        }
    };
    bool flying = playerInst != nullptr && !opt.haveCam;
    if (playerInst) {
        Uint64 t0 = SDL_GetPerformanceCounter();
        coll.build(world, models, types, r.unitScale, playerWorld);
        std::printf("Collision: %zu triangles (%.0f ms)\n", coll.triangleCount(),
                    double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency()));
        const WorldObject& po = world.objects()[size_t(playerWorld)];
        configureVehicle(heli);
        heli.pos = playerInst->pos;
        heli.yaw = -po.angle[4] * 6.2831853f / 4096.f;                    // placement angles turn clockwise
        float g = coll.groundBelow(heli.pos + Vec3{0, 0, 5});
        if (g > -1e8f) heli.pos.z = g + heli.clearance;
    }

    Combat combat;
    std::vector<Vec3> rocketPylons;              // heli-local launch points (parts start1..6)
    if (playerInst) {
        combat.init(world, models, types, defs, sprites, r, coll, &audio, playerWorld);
        int pm = playerInst->model;
        const auto& ms = models.models();
        if (!ms[size_t(pm)].lods.empty()) pm = ms[size_t(pm)].lods[0];
        const ObjectType* pt = types.get(size_t(playerInst->objClass));
        if (pm >= 0 && pt)
            for (const auto& part : ms[size_t(pm)].parts)
                for (const auto& tp : pt->parts)
                    if (tp.variableOffset == part.variableOffset && tp.name.rfind("start", 0) == 0) {
                        rocketPylons.push_back({part.pivot[0] * r.unitScale, part.pivot[1] * r.unitScale, part.pivot[2] * r.unitScale});
                        break;
                    }
        if (rocketPylons.empty()) rocketPylons = {{-3.4f, -1.9f, -1.6f}, {3.4f, -1.9f, -1.6f}};
    }
    // ---- enemies and missions
    PathDB paths; paths.setLevel(fs, lv);
    EnemySystem enemies;
    MissionFile mission;
    std::vector<uint8_t> shieldDown;                     // *_WHEN_DESTROYED events already done (1) or running (2)
    std::map<int, float> eventTimer;                     // TIME_EXPLODE / FLICKER countdown (ticks)
    std::set<std::string> enemySets{"DUMMY.3DW"};      // sets switched by missions ([ENEMYSCAPE])
    auto upperS = [](std::string x) { for (auto& c : x) c = char(std::toupper((unsigned char)c)); return x; };
    for (int i = 0; i < 100; i++) {
        char n[32]; std::snprintf(n, sizeof n, "MISS_%02d.SCR", i);
        MissionFile m;
        if (m.load(fs, lv, n, opt.difficulty)) for (auto& sname : m.enemySets) enemySets.insert(sname);
    }
    // Alternative enemy layouts (enemy*/enemx* for other difficulty folders) are never always-on.
    for (const auto& sname : world.sets) { std::string u = upperS(sname); if (u.rfind("ENEM", 0) == 0) enemySets.insert(u); }
    combat.setDropSources(std::vector<std::string>(enemySets.begin(), enemySets.end()));   // extra drops copy these (0x733df)
    std::set<std::string> activeSets;
    bool crashing = false; float crashSpin = 0, crashSmoke = 0;
    bool cockpit = opt.cockpit;                             // inside view (HELICOCK.3DI, TYP ADV_COCKPIT)
    const int cockpitModel = models.find("HELICOCK.3DI");
    float deathTimer = 0, missionClock = 0, goalTimer = 0;
    // Mission timer (RE 0x51737): [MISSIONTIME] m:s is added as (m*60+s)*60*133/100 ticks of 1/60 s,
    // so unused time carries over to the next mission; it counts down to 0 and nothing happens there.
    float missionTimer = 0, missionTimerStart = 0; bool timerShown = false; std::string lastMissionFile;
    int speedScale[3][4] = {{100, 100, 100, 100}, {100, 100, 100, 100}, {100, 100, 100, 100}};   // 0x2f0d4: reset per level
    if (opt.assault) { speedScale[1][0] = 175; speedScale[1][1] = speedScale[1][2] = speedScale[1][3] = 140; }   // 0x3075a: ASSAULT MODE
    combat.resetArms(2);
    auto toSaveArms = [&]() {
        SaveArms a; const auto& c = combat.arms;
        a.sel = c.sel; for (int k = 0; k < 3; k++) { a.ammo[k] = c.ammo[k]; a.level[k] = c.level[k]; }
        a.weapon3 = c.weapon3; a.rockets = c.rockets; a.smartbombs = c.smartbombs; a.thunderbolts = c.thunderbolts;
        a.shield = c.shield; a.magma = c.magma;
        return a;
    };
    if (opt.haveArms) {
        auto& c = combat.arms; const SaveArms& a = opt.carryArms;
        c.sel = a.sel; for (int k = 0; k < 3; k++) { c.ammo[k] = a.ammo[k]; c.level[k] = a.level[k]; }
        c.weapon3 = a.weapon3; c.rockets = a.rockets; c.smartbombs = a.smartbombs; c.thunderbolts = a.thunderbolts;
        c.shield = a.shield > 0 ? a.shield : Combat::kMaxShield; c.magma = a.magma;
    }
    combat.score = opt.carryScore;
    combat.scorePercent = profile::scorePercent(opt.difficulty);
    combat.pilot = opt.difficulty;
    int bonusBeepScore = 0; float bonusLeft = 0;                           // time bonus still counting (seconds of mission time)
    float playTicks = opt.carryTicks;              // game time for the level-end screen (ticks)
    bool endScreen = false, endToMenu = false; float endT = 0;
    std::string missionStatus;
    struct Banner { std::string text; float time = 0; bool big = false; };
    Banner banner, goalBanner, pickupMsg;
    std::string tickerText; float tickerStart = 0;   // mission text ticker (0x1a28a0, start 0x21be24)
    std::string speakQueued;                      // speech requested by callbacks (played in the frame loop)
    std::vector<char> startAlive;                // per world object: alive when the current mission began
    // Mission area ([BORDER] *.BRD). RE 0x3dd3f: outside the area a "RETURN TO MISSION AREA!" warning
    // with a countdown appears after 180 ticks; after 780 ticks (13 s) the mission is failed.
    Vec3 lastGatePos{1e9f, 0, 0};                // previous helicopter position (scape switch gates)
    std::vector<Vec3> borderPoly; float outsideTicks = 0; bool wasInside = false;
    Vec3 missionStartPos; float missionStartYaw = 0;   // where a restarted mission puts the helicopter
    auto insideBorder = [&](Vec3 p) {
        if (borderPoly.size() < 3) return true;
        bool in = false;
        for (size_t i = 0, j = borderPoly.size() - 1; i < borderPoly.size(); j = i++) {
            const Vec3& a = borderPoly[i]; const Vec3& b = borderPoly[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
        return in;
    };
    // Respawn on the landing pad; in the second half of a level (after the tunnel flight) at the
    // point where the mission began.
    auto respawnHeli = [&]() {
        const WorldObject& po = world.objects()[size_t(playerWorld)];
        Vec3 pad{po.pos[0] * r.unitScale, po.pos[1] * r.unitScale, po.pos[2] * r.unitScale};
        Vec3 d = missionStartPos - pad;
        heli = Heli{};
        configureVehicle(heli);
        if (dot(d, d) < 2500.f * 2500.f) { heli.pos = pad; heli.yaw = -po.angle[4] * 6.2831853f / 4096.f; }
        else { heli.pos = missionStartPos; heli.yaw = missionStartYaw; }
        lastGatePos = {1e9f, 0, 0};
        float g0 = coll.groundBelow(heli.pos + Vec3{0, 0, 5});
        if (g0 > -1e8f) heli.pos.z = heli.pos.z == pad.z ? g0 + heli.clearance : std::max(heli.pos.z, g0 + heli.clearance);
    };
    // ---- Scape switches (OBJECT.TYP SCAPESWITCH_START / SCAPESWITCH_TO): every area of a level lists
    // the landscape sets that are visible (and solid), its fog and the flight ceiling. Placed switch
    // objects (SCHALT1/SCHALT2 quads) fire when the helicopter flies through them; mission files
    // name one with [SCAPE]; ACTIVATION_WHEN_DESTROYED fires when the named object is destroyed.
    std::set<std::string> managedSets;
    for (size_t t = 0; t < types.size(); t++) for (const auto& sn : types.get(t)->scapeSets) managedSets.insert(sn);
    struct SwitchGate { int wi, type; std::vector<Vec3> tri; bool fired = false; };
    std::vector<SwitchGate> gates;
    std::vector<char> noCollide(world.objects().size(), 0);
    for (size_t i = 0; i < world.objects().size(); i++) {
        const WorldObject& o = world.objects()[i];
        const ObjectType* t = types.get(o.objClass);
        if (!t || (t->behavior != "SCAPESWITCH_TO" && t->behavior != "SCAPESWITCH_START")) continue;
        SwitchGate g{int(i), int(o.objClass), {}};
        const Model& m0 = models.models()[o.proto];
        int li = m0.lods.empty() ? int(o.proto) : m0.lods[0];
        const Model& gm = models.models()[size_t(li)].aliasOf >= 0 ? models.models()[size_t(models.models()[size_t(li)].aliasOf)] : models.models()[size_t(li)];
        Mat4 xf = Mat4::translate({o.pos[0] * r.unitScale, o.pos[1] * r.unitScale, o.pos[2] * r.unitScale}) * worldObjectRotation(o.angle);
        std::vector<Vec3> v;
        for (size_t k = 0; k + 2 < gm.verts.size(); k += 3)
            v.push_back(xf.transformPoint({gm.verts[k] * r.unitScale, gm.verts[k + 1] * r.unitScale, gm.verts[k + 2] * r.unitScale}));
        for (size_t k = 2; k < v.size(); k++) { g.tri.push_back(v[0]); g.tri.push_back(v[k - 1]); g.tri.push_back(v[k]); }
        gates.push_back(g);
        noCollide[i] = 1;
        coll.setOwnerEnabled(int(i), false);
        if (Renderer::Instance* in = r.instanceForWorldObject(int(i))) in->hidden = true;
    }
    float ceilingZ = 1e9f;
    std::string currentScape;
    int viewRange = opt.settings.viewRange;            // 0 low, 1 medium, 2 high (INIT.CFG DETAILSIGHT)
    auto applyScape = [&](int typeIndex) {
        const ObjectType* t = types.get(size_t(typeIndex));
        if (!t || t->scapeSets.empty()) return;
        if (t->name == currentScape) return;
        currentScape = t->name;
        std::set<std::string> on(t->scapeSets.begin(), t->scapeSets.end());
        std::set<std::string> done;
        for (size_t i = 0; i < world.objects().size(); i++) {
            const std::string& sn = world.objects()[i].set;
            std::string u = upperS(sn);
            if (!managedSets.count(u) || isTemplateSet(sn)) continue;
            bool vis = on.count(u) > 0;
            // collision-only meshes (*COL.3DW, e.g. RIVERCOL) are listed too: solid, but never drawn
            const bool colOnly = u.find("COL.3DW") != std::string::npos;
            if (!done.count(sn)) { done.insert(sn); if (vis && !colOnly) r.hiddenSets.erase(sn); else r.hiddenSets.insert(sn); }
            if (noCollide[i] || combat.isPickupObject(int(i))) continue;
            const Combat::Target* tg = combat.target(int(i));
            coll.setOwnerEnabled(int(i), !(tg && tg->isTarget && !tg->alive));
            coll.setScapeHidden(int(i), !vis);   // enemies still walk on it (see CollisionWorld::includeHiddenScape)
        }
        if (t->hasFog) {
            for (int k = 0; k < 3; k++) r.fogColor[k] = float(t->fogColor[k]) / 255.f;
            float fn = t->fogNear, ff = t->fogFar;
            if (t->fogDetails > 0) {      // viewing range (Options / F6, INIT.CFG DETAILSIGHT): the area's low / medium / high pair
                int d = std::clamp(viewRange, 0, t->fogDetails - 1);
                fn = t->fogDetail[d][0]; ff = t->fogDetail[d][1];
            }
            const float mul = float(opt.settings.viewDistance) / 100.f;   // Options > Detail: VIEW DISTANCE, F7 / F8
            fn *= mul; ff *= mul;
            if (ff > 0) { r.fogNear = fn * r.unitScale; r.fogFar = ff * r.unitScale; r.viewDistance = r.fogFar * 1.05f; }
        }
        ceilingZ = t->hasMaxHeight ? t->maxHeight * r.unitScale : 1e9f;
        std::printf("Scape %s (%zu sets, ceiling %.0f)\n", t->name.c_str(), on.size(), ceilingZ);
        enemies.measureGround();   // units of the area that just became solid
    };
    auto applyScapeByName = [&](const std::string& n) {
        std::string un = upperS(n);
        for (size_t t = 0; t < types.size(); t++) if (upperS(types.get(t)->name) == un) { applyScape(int(t)); return; }
    };
    for (size_t t = 0; t < types.size(); t++)
        if (types.get(t)->behavior == "SCAPESWITCH_START" && upperS(types.get(t)->name) == "SWITCH_START") { applyScape(int(t)); break; }
    auto checkGates = [&](Vec3 from, Vec3 to) {
        if (from.x > 1e8f) return;
        Vec3 d = to - from;
        if (dot(d, d) < 1e-8f) return;
        for (auto& g : gates)
            for (size_t k = 0; k + 2 < g.tri.size(); k += 3) {
                // segment / triangle (Moller-Trumbore)
                Vec3 e1 = g.tri[k + 1] - g.tri[k], e2 = g.tri[k + 2] - g.tri[k];
                Vec3 p = cross(d, e2); float det = dot(e1, p);
                if (std::fabs(det) < 1e-9f) continue;
                float inv = 1.f / det; Vec3 s0 = from - g.tri[k];
                float u = dot(s0, p) * inv; if (u < 0 || u > 1) continue;
                Vec3 q = cross(s0, e1); float v = dot(d, q) * inv; if (v < 0 || u + v > 1) continue;
                float tt = dot(e2, q) * inv; if (tt < 0 || tt > 1) continue;
                applyScape(g.type);
                break;
            }
    };
    // objects waiting for APPEAR_WHEN_DESTROYED are not there yet; removed barriers stay away
    auto eventVisibility = [&](size_t i) {
        const ObjectType* st = types.get(world.objects()[i].objClass);
        if (!st || st->whenDestroyed.empty()) return;
        int kind = st->whenDestroyed.front().kind;
        bool done = i < shieldDown.size() && shieldDown[i] == 1;
        bool gone = (kind == 4 && !done) || ((kind == 0 || kind == 3) && done);
        if (!gone) return;
        coll.setOwnerEnabled(int(i), false);
        combat.setActive(int(i), false);
        enemies.setActive(int(i), false);
        if (Renderer::Instance* in = r.instanceForWorldObject(int(i))) in->hidden = true;
    };
    auto applyMission = [&](std::string name, bool restart) {
        if (!mission.load(fs, lv, name, opt.difficulty)) { std::printf("Mission %s not found\n", name.c_str()); return false; }
        for (int v = 0; v < 3; v++) for (int k = 0; k < 4; k++) if (mission.scaleSpeed[v][k] > 0) speedScale[v][k] = mission.scaleSpeed[v][k];
        std::set<std::string> want(mission.enemySets.begin(), mission.enemySets.end());
        const auto& objs = world.objects();
        for (size_t i = 0; i < objs.size(); i++) {
            std::string set = upperS(objs[i].set);
            if (!enemySets.count(set)) continue;
            bool on = want.count(set) > 0;
            bool wasOn = activeSets.count(set) > 0;
            // Restarting a mission restores it as it was at its start: units destroyed in an earlier
            // mission of the same set (e.g. the convoy of mission 2 in mission 3) stay destroyed.
            if (restart && on && wasOn && i < startAlive.size() && !startAlive[i]) continue;
            if (on && (!wasOn || restart)) { combat.resetTarget(int(i)); if (i < shieldDown.size()) shieldDown[i] = 0; }
            combat.setActive(int(i), on);
            const Combat::Target* tg = combat.target(int(i));
            coll.setOwnerEnabled(int(i), on && !(tg && tg->isTarget && !tg->alive));
            if (on != wasOn || restart) enemies.setActive(int(i), on);
            if (Renderer::Instance* in = r.instanceForWorldObject(int(i))) if (!on) in->hidden = true;
            if (on) eventVisibility(i);
        }
        activeSets = want;
        if (restart) combat.missionRestarted(); else combat.missionStarted();
        if (!restart || startAlive.empty()) {
            startAlive.assign(objs.size(), 1);
            for (size_t i = 0; i < objs.size(); i++) if (const Combat::Target* tg = combat.target(int(i))) startAlive[i] = tg->alive ? 1 : 0;
        }
        combat.scaleAdvShots = mission.scaleAdv; combat.scaleEnemyShots = mission.scaleEnemy;
        missionClock = 0;
        missionStatus.clear();                      // the window title showed the previous mission's result
        if (!mission.scape.empty()) { currentScape.clear(); applyScapeByName(mission.scape); }
        borderPoly.clear(); outsideTicks = 0; wasInside = false;
        if (!restart || missionStartPos.x == 0.f) { missionStartPos = heli.pos; missionStartYaw = heli.yaw; }
        if (!mission.border.empty() && !mission.noBorder) {
            std::vector<uint8_t> raw;
            if (MissionFile::readFile(fs, lv, opt.difficulty, mission.border, raw)) {
                std::istringstream in(decodeText(raw)); std::string line;
                while (std::getline(in, line)) {
                    size_t k = line.find("DSTPOS");
                    if (k == std::string::npos) continue;
                    std::istringstream ls(line.substr(k + 6)); float x, y;
                    if (ls >> x >> y) borderPoly.push_back({x * r.unitScale, y * r.unitScale, 0});
                }
            }
            if (borderPoly.size() < 3) borderPoly.clear();
        }
        combat.rescued.clear();
        if (upperS(mission.file) == lastMissionFile) missionTimer = missionTimerStart;
        else { missionTimer += float(mission.timeLimit) * 1.33f; missionTimerStart = missionTimer; lastMissionFile = upperS(mission.file); }
        if (mission.timeLimit > 0) timerShown = true;
        std::printf("Mission %s: enemy sets", mission.file.c_str());
        for (auto& x : mission.enemySets) std::printf(" %s", x.c_str());
        std::printf(", %zu goal rules\n", mission.rules.size());
        return true;
    };
    if (playerInst) {
        enemies.init(world, types, defs, paths, r, combat, coll);
        // lifts, gates and trains outside the mission enemy sets run all the time (dispatcher: always active)
        for (size_t i = 0; i < world.objects().size(); i++)
            if (!enemySets.count(upperS(world.objects()[i].set)) && enemies.isMover(int(i))) enemies.setActive(int(i), true);
        if (std::getenv("XA_SETS")) {
            std::map<std::string, std::vector<int>> bySet;
            for (size_t q = 0; q < world.objects().size(); q++) bySet[world.objects()[q].set].push_back(int(q));
            for (auto& [sn, v] : bySet) {
                float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
                std::map<std::string, int> tc;
                for (int q : v) { const auto& o = world.objects()[size_t(q)]; for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], o.pos[k] * r.unitScale); mx[k] = std::max(mx[k], o.pos[k] * r.unitScale); }
                    const ObjectType* tq = types.get(o.objClass); tc[tq ? tq->name : "?"]++; }
                std::printf("set %-14s %4zu objs  x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f  hidden %d :", sn.c_str(), v.size(), mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], int(r.hiddenSets.count(sn)));
                int k = 0; for (auto& [tn, n] : tc) { if (k++ < 40) std::printf(" %s*%d", tn.c_str(), n); }
                std::printf("\n");
            }
        }
        if (std::getenv("XA_DEBUG_Z"))
            for (size_t q = 0; q < world.objects().size(); q++) {
                int wi = int(q);
                const ObjectType* tq = types.get(world.objects()[q].objClass);
                if (!tq || tq->name.find(std::getenv("XA_DEBUG_Z")) == std::string::npos) continue;
                { const Model& sm = models.models()[world.objects()[q].proto]; int li = sm.lods.empty() ? int(world.objects()[q].proto) : sm.lods[0]; const Model& g = models.models()[size_t(li)]; const Model& ga = g.aliasOf >= 0 ? models.models()[size_t(g.aliasOf)] : g;
                  std::printf("  verts:"); for (size_t k = 0; k + 2 < ga.verts.size(); k += 3) std::printf(" (%d %d %d)", ga.verts[k], ga.verts[k + 1], ga.verts[k + 2]); std::printf("  polys %zu angles %d %d %d\n", ga.polys.size(), world.objects()[q].angle[2], world.objects()[q].angle[3], world.objects()[q].angle[4]); }
                std::printf("obj %d %s set %s pos %.0f %.0f %.0f radius %.1f proto %s\n", wi, tq->name.c_str(), world.objects()[q].set.c_str(), world.objects()[q].pos[0] * r.unitScale, world.objects()[q].pos[1] * r.unitScale, world.objects()[q].pos[2] * r.unitScale, float(world.objects()[q].radius) * r.unitScale, models.models()[world.objects()[q].proto].name.c_str());
                continue;
                const auto& o = world.objects()[size_t(wi)];
                const Model& m = models.models()[o.proto];
                int li = m.lods.empty() ? int(o.proto) : m.lods[0];
                const Model& g = models.models()[size_t(li)];
                const Model& gg = g.aliasOf >= 0 ? models.models()[size_t(g.aliasOf)] : g;
                int32_t mn = 1 << 30, mx = -(1 << 30);
                for (size_t k = 2; k < gg.verts.size(); k += 3) { mn = std::min(mn, gg.verts[k]); mx = std::max(mx, gg.verts[k]); }
                for (int l : m.lods) { const Model& lm = models.models()[size_t(l)]; const Model& la = lm.aliasOf >= 0 ? models.models()[size_t(lm.aliasOf)] : lm;
                    std::printf("   lod %s alias %d polys %zu verts %zu\n", lm.name.c_str(), lm.aliasOf, la.polys.size(), la.verts.size() / 3); }
                std::printf("obj %d set %s %s z %.2f model %s bbox z %d..%d verts z %d..%d angles %d %d %d\n", wi, o.set.c_str(), types.get(o.objClass) ? types.get(o.objClass)->name.c_str() : "?", o.pos[2] * r.unitScale,
                            gg.name.c_str(), m.bboxMin[2], m.bboxMax[2], mn, mx, o.angle[2], o.angle[3], o.angle[4]);
            }
        combat.onDestroyed = [&](int wi) { return enemies.startCrash(wi); };
        combat.onExplosion = [&](Vec3 at, float size, bool big) {   // controller: explosions close to the player shake
            Vec3 d = at - heli.pos; float dist = std::sqrt(dot(d, d)), range = 40.f + size * 12.f;
            if (dist < range) pad::rumble((1.f - dist / range) * std::min(1.f, 0.3f + size * 0.12f) * (big ? 1.f : 0.7f), big ? 350 : 200);
        };
        // pickup messages (RE 0x79390 handlers, texts from INGAME.CHR, shown 120 ticks at y 354)
        combat.onPickup = [&](int x) {
            static const char* weapons[3] = {"RAZOR GUN", "LASER CANNON", "FIRE FLASH"};
            std::string m;
            auto fmt1 = [&](int id, const std::string& a) { std::string f = ingameText.get(id); auto p = f.find("%s"); if (p != std::string::npos) f.replace(p, 2, a); return f; };
            switch (x) {
            case 0: m = ingameText.get(21); break;                         // SMART BOMB
            case 1: m = ingameText.get(22); break;                         // THUNDERBOLT
            case 3: m = ingameText.get(23); break;                         // SCREENING DEVICE
            case 4: case 5: case 6: m = fmt1(16, lang::tr(weapons[x - 4])); break;   // AMMUNITION FOR %s
            case 7: case 8: {                                              // %s %ld
                std::string f = fmt1(17, lang::tr(weapons[x - 7])); auto p = f.find("%ld");
                if (p != std::string::npos) f.replace(p, 3, std::to_string(combat.arms.level[x - 7] + 1));
                m = f; break; }
            case 9: m = ingameText.get(18); break;                         // FIRE FLASH
            case 10: m = ingameText.get(19); break;                        // GUIDED MISSILE
            case 11: m = ingameText.get(20); break;                        // INDESTRUCTIBLE
            case 12: m = ingameText.get(14); break;                        // SHIELD ENERGY
            case 13: m = ingameText.get(15); break;                        // EXTRA SHIELD
            case 17: m = ingameText.get(24); break;                        // MAGMA
            case 2: case 14: case 15: m = Combat::extraName(x); break;      // RADAR / MEGASHIELD / NAVCOM: name only (0x79c84)
            case 18: m = ingameText.get(25); break;                        // PERSON ON BOARD
            default: break;
            }
            {   // pickup speech (0x79390 handlers -> 0x58110)
                const char* sp = nullptr;
                switch (x) {
                case 0: sp = "016.WAV"; break; case 1: sp = "036.WAV"; break;
                case 4: sp = "042.WAV"; break; case 5: sp = "048.WAV"; break; case 6: sp = "051.WAV"; break;
                case 7: sp = combat.arms.level[0] >= 2 ? "026.WAV" : "025.WAV"; break;
                case 8: sp = combat.arms.level[1] >= 2 ? "032.WAV" : "031.WAV"; break;
                case 9: sp = "035.WAV"; break; case 10: sp = "021.WAV"; break;
                case 12: sp = "013.WAV"; break; case 13: sp = "014.WAV"; break; case 18: sp = "094.WAV"; break;
                default: break;
                }
                if (sp) speakQueued = sp;
            }
            std::printf("Extra: %s (%s)\n", Combat::extraName(x), m.c_str());
            if (!m.empty()) pickupMsg = {m, 2.f, false};
        };
        if (opt.load && opt.load->hasWorld) {        // savegame: the world as it was when the mission began
            const SaveGame& sv = *opt.load;
            for (const auto& x : sv.activeSets) activeSets.insert(x);
            const auto& objs = world.objects();
            for (size_t i = 0; i < objs.size(); i++) {
                std::string set = upperS(objs[i].set);
                if (!enemySets.count(set) || !activeSets.count(set)) continue;
                combat.setActive(int(i), true);
                enemies.setActive(int(i), true);
            }
            combat.loadState(sv.alive, sv.taken);
            startAlive.assign(objs.size(), 1);
            for (size_t i = 0; i < objs.size() && i < sv.alive.size(); i++) startAlive[i] = char(sv.alive[i]);
            for (size_t i = 0; i < shieldDown.size() && i < sv.shieldDown.size(); i++) shieldDown[i] = char(sv.shieldDown[i]);
            missionTimer = sv.missionTimer; missionTimerStart = sv.missionTimerStart; timerShown = sv.timerShown; lastMissionFile = sv.lastMissionFile;
            if (sv.tank != playerTank && (sv.tank ? tankTemplate : heliTemplate) >= 0) {   // saved in the other vehicle
                playerTank = sv.tank;
                heli = Heli{}; configureVehicle(heli);
                int tpl = playerTank ? tankTemplate : heliTemplate;
                if (Renderer::Instance* ti = r.instanceForWorldObject(tpl)) { playerInst->model = ti->model; playerInst->objClass = ti->objClass; }
            }
            heli.pos = sv.heliPos; heli.yaw = sv.heliYaw; heli.vel = {};
            missionStartPos = heli.pos; missionStartYaw = heli.yaw;
            if (!sv.scape.empty()) applyScapeByName(sv.scape);
        }
        if (shieldDown.size() != world.objects().size()) shieldDown.resize(world.objects().size(), 0);
        for (size_t i = 0; i < world.objects().size(); i++)
            if (!enemySets.count(upperS(world.objects()[i].set)) && !isTemplateSet(world.objects()[i].set)) eventVisibility(i);
        applyMission(opt.mission, !(opt.load && opt.load->hasWorld));
    }
    auto saveGame = [&](int level, const std::string& next, bool withWorld) {
        if (!opt.saving) return;
        SaveGame sv;
        sv.pilot = opt.difficulty; sv.level = level; sv.missionFile = upperS(next);
        sv.mission = std::max(1, std::atoi(sv.missionFile.c_str() + std::min<size_t>(5, sv.missionFile.size())));
        sv.score = combat.score + int(bonusLeft * float(combat.scorePercent)); sv.playTicks = playTicks;
        sv.arms = toSaveArms();
        if (withWorld) {
            sv.hasWorld = true; sv.heliPos = heli.pos; sv.heliYaw = heli.yaw;
            sv.activeSets.assign(activeSets.begin(), activeSets.end());
            combat.saveState(sv.alive, sv.taken);
            sv.shieldDown.assign(shieldDown.begin(), shieldDown.end());
            sv.missionTimer = missionTimer; sv.missionTimerStart = missionTimerStart; sv.timerShown = timerShown; sv.lastMissionFile = lastMissionFile;
            sv.scape = currentScape; sv.tank = playerTank;
        }
        if (!profile::write(sv)) std::printf("%s\n", ingameText.get(243).c_str());
        else std::printf("Saved P%dL%02dM%02d\n", sv.pilot, sv.level, sv.mission);
    };
    if (const char* nr = std::getenv("XA_NEAR")) {   // testing: XA_NEAR=x,y,r lists the world objects around a point
        float nx = 0, ny = 0, rad = 30; std::sscanf(nr, "%f,%f,%f", &nx, &ny, &rad);
        for (size_t i = 0; i < world.objects().size(); i++) {
            const WorldObject& o = world.objects()[i]; float x = o.pos[0] * r.unitScale, y = o.pos[1] * r.unitScale;
            if ((x - nx) * (x - nx) + (y - ny) * (y - ny) > rad * rad) continue;
            const ObjectType* t = types.get(o.objClass);
            std::printf("near %zu %s (%s) set %s at %.0f %.0f %.0f angle %d %d %d%s%s\n", i, t ? t->name.c_str() : "?", t ? t->behavior.c_str() : "?", o.set.c_str(), x, y, o.pos[2] * r.unitScale,
                        o.angle[2], o.angle[3], o.angle[4], t && !t->releaseExtra.empty() ? (" release " + t->releaseExtra).c_str() : "", t && !t->whenDestroyed.empty() ? " (events)" : "");
        }
    }
    auto typeIndexByName = [&](const std::string& n) {
        std::string un = upperS(n);
        for (size_t i = 0; i < types.size(); i++) if (upperS(types.get(i)->name) == un) return int(i);
        return -1;
    };
    // 0x57704: the TYP counts as destroyed when no object of it is left in the active lists (alive, or still
    // waiting to appear); objects of enemy sets that are not loaded don't count
    auto typeGone = [&](const std::string& typ) {
        int cls = typeIndexByName(typ);
        if (cls < 0) return true;
        for (size_t i = 0; i < world.objects().size(); i++) {
            const WorldObject& o = world.objects()[i];
            if (int(o.objClass) != cls || isTemplateSet(o.set)) continue;
            std::string set = upperS(o.set);
            if (enemySets.count(set) && !activeSets.count(set)) continue;
            const ObjectType* et = types.get(o.objClass);
            int kind = et && !et->whenDestroyed.empty() ? et->whenDestroyed.front().kind : -1;
            if (i < shieldDown.size() && shieldDown[i] == 1 && kind != 4) continue;      // removed
            const Combat::Target* t = combat.target(int(i));
            if (t && t->isTarget && !t->alive) continue;
            if (combat.pickupTaken(int(i))) continue;                                        // collected extras / hostages
            return false;
        }
        return true;
    };
    enemies.typePresent = [&](const std::string& typ) { return !typeGone(typ); };   // ACTIVATION_WHEN_DESTROYED (0x59d1c)
    Briefing briefing;
    std::function<bool(const MissionFile::Cond&)> evalCond;
    evalCond = [&](const MissionFile::Cond& c) -> bool {
        switch (c.kind) {
        case MissionFile::Cond::Destroyed: {
            // 0x38bf0: "TYP a,b" sums the objects of all listed types: without NR none of them may be left,
            // with NR the destroyed ones of all types together count.
            // "DESTROYED TYP ADV" (L2 / L4 MISS_11, the ending flights): the loader never puts the player's
            // types (ADV_HELI, ADV_TANK, cockpits, 0x608b4) into the object lists, so they count 0 and the goal
            // is met as soon as goals are checked again, i.e. when the briefing flight is over.
            std::vector<int> classes;
            {
                std::string rest = c.typ;
                while (true) {
                    size_t p = rest.find(',');
                    std::string one = rest.substr(0, p);
                    if (!one.empty()) {
                        int cls = typeIndexByName(one); if (cls < 0) return false;
                        const ObjectType* ct = types.get(size_t(cls));
                        const std::string bh = ct ? ct->behavior : std::string();
                        if (!(bh == "CAMERA" || bh.rfind("ADV_", 0) == 0 || bh.rfind("NET_", 0) == 0)) classes.push_back(cls);   // 0x608b4: never listed
                    }
                    if (p == std::string::npos) break;
                    rest = rest.substr(p + 1);
                }
            }
            int dead = 0, total = 0;
            for (size_t i = 0; i < world.objects().size(); i++) {
                if (std::find(classes.begin(), classes.end(), int(world.objects()[i].objClass)) == classes.end() || isTemplateSet(world.objects()[i].set)) continue;
                std::string set = upperS(world.objects()[i].set);
                if (enemySets.count(set) && !activeSets.count(set)) continue;
                const Combat::Target* t = combat.target(int(i));
                total++;
                bool removed = false;
                if (i < shieldDown.size() && shieldDown[i] == 1)
                    if (const ObjectType* et = types.get(world.objects()[i].objClass); et && !et->whenDestroyed.empty())
                        removed = et->whenDestroyed.front().kind != 4;
                // 0x38bf0 counts the objects still in the lists: a unit leaves them at the end of its death
                // sequence (0x7295c), so the mothership counts until its explosions are over
                if (enemies.dying(int(i))) continue;
                if (t && t->alive && !removed) {
                    if (std::getenv("XA_DEBUG")) {
                        const Renderer::Instance* gi = r.instanceForWorldObject(int(i));
                        std::printf("goal %s: obj %zu set %s still alive (target %d active %d hidden %d at %.0f %.0f %.0f)\n", c.typ.c_str(), i, set.c_str(), int(t->isTarget), int(t->active),
                                    gi ? int(gi->hidden) : -1, gi ? gi->pos.x : 0.f, gi ? gi->pos.y : 0.f, gi ? gi->pos.z : 0.f);
                    }
                    continue;
                }
                dead++;
            }
            // 0x57704: met when no object of the TYP is left in the active lists, also when the
            // current difficulty's enemy file has none (e.g. L1 MISS_04 e-PANTHER-04-4 only in enemx004)
            return c.nr > 0 ? dead >= c.nr : dead == total;
        }
        case MissionFile::Cond::Rescued: {
            int need = c.nr > 0 ? c.nr : 1, got = 0;
            std::string rest = c.typ;
            while (!rest.empty()) {
                size_t p = rest.find(',');
                int cls = typeIndexByName(rest.substr(0, p));
                if (cls >= 0) { auto it = combat.rescued.find(cls); if (it != combat.rescued.end()) got += it->second; }
                if (p == std::string::npos) break;
                rest = rest.substr(p + 1);
            }
            return got >= need;
        }
        case MissionFile::Cond::Collided: {
            Vec3 c3{c.sphere[0] * r.unitScale, c.sphere[1] * r.unitScale, c.sphere[2] * r.unitScale};
            Vec3 d = heli.pos - c3;
            return deathTimer <= 0 && dot(d, d) < (c.sphere[3] * r.unitScale) * (c.sphere[3] * r.unitScale);
        }
        case MissionFile::Cond::PathEnd: {
            int cls = typeIndexByName(c.typ);
            for (size_t i = 0; i < world.objects().size(); i++)
                if (int(world.objects()[i].objClass) == cls && enemies.pathEndReached(int(i))) return true;
            return false;
        }
        default: return false;
        }
    };

    briefing.textLength = [&](int id) { return int(missionText.get(id).size()); };
    briefing.sound = [&](const char* wav) { if (audio.ok()) audio.play(audio.sound(wav), 0.8f, 0.f, false); };
    // briefing voices: MISSIONS\SPEAK.CHR lists the SPEAK_E\LEVELnn files of a text (played one after the other)
    TextDB speakText; speakText.load(fs, std::string(lv) + "\\MISSIONS\\SPEAK.CHR");
    std::vector<std::string> voiceQueue; int briefVoice = 0;
    const int briefMode = opt.settings.briefing;
    briefing.showText = briefMode != 2;
    briefing.speakText = [&](int id) {
        if (briefMode == 0 || !audio.ok() || !opt.settings.speech) return false;
        std::string list = speakText.get(id);
        if (list.empty()) return false;
        if (briefVoice) { audio.stop(briefVoice); briefVoice = 0; }
        voiceQueue.clear();
        std::string cur;
        for (char c : list + ",") { if (c == ',') { if (!cur.empty()) voiceQueue.push_back(cur); cur.clear(); } else if (c != ' ') cur += c; }
        for (const auto& v : voiceQueue) {           // the voice files must exist (else the text is shown)
            char path[64]; std::snprintf(path, sizeof path, "SPEAK_E\\LEVEL%02d\\%s", opt.level, v.c_str());
            if (audio.sound(path) >= 0) return true;
        }
        voiceQueue.clear();
        return false;
    };
    briefing.voiceBusy = [&]() { return !voiceQueue.empty() || (briefVoice && audio.playing(briefVoice)); };
    briefing.findObject = [&](const std::string& n, Vec3& p, float& rad) {
        int cls = typeIndexByName(n);
        for (size_t i = 0; i < world.objects().size(); i++) {
            if (int(world.objects()[i].objClass) != cls || isTemplateSet(world.objects()[i].set)) continue;
            std::string set = upperS(world.objects()[i].set);
            if (enemySets.count(set) && !activeSets.count(set)) continue;
            if (Renderer::Instance* in = r.instanceForWorldObject(int(i))) { p = in->pos; rad = in->radius; return true; }
        }
        return false;
    };
    auto startMissionIntro = [&]() {
        bool scriptedRun = (opt.flySeconds > 0 || opt.haveCam) && opt.briefShot < 0;
        if (!scriptedRun) briefing.start(mission, fs, lv, r.unitScale);
        // 0x35be3: [SCROLLTEXT] n puts MISSION.CHR text n (if shorter than 128 characters) into the ticker at the
        // bottom of the screen and restarts it; without it the previous text keeps running
        if (mission.scrollText >= 0) {
            std::string st = missionText.get(mission.scrollText);
            for (auto& ch : st) if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            if (st.size() < 128) { tickerText = st; tickerStart = -1; }   // started on the next frame
        }
    };
    if (playerInst) startMissionIntro();

    // ---- music: the GOG release has the CD audio as MUSIC\track_NN.ogg. INIT.CFG maps the
    // XMI names (alphabetical) to CD tracks: CDAUDIOTRACKS=13@17@18@19@2@...
    std::string currentMusic;
    auto playMusic = [&](std::string xmi, bool loop) {
        xmi = upperS(xmi);
        if (auto d = xmi.find('.'); d != std::string::npos) xmi.resize(d);
        if (loop && xmi == currentMusic) return;
        if (!opt.settings.music) return;
        std::string p = musicTrack(fs, xmi);
        if (p.empty()) return;
        if (audio.playMusic(p, loop)) currentMusic = loop ? xmi : "";
        else std::printf("Music %s (%s) not found\n", xmi.c_str(), p.c_str());
    };
    bool wasBriefing = false;

    // speech (DATA.SFX\SPEAK_E, played by 0x58060: a new phrase replaces the one running)
    int speechVoice = 0, distortVoice = 0;
    auto speak = [&](const char* file) {
        if (!audio.ok()) return;
        if (speechVoice) audio.stop(speechVoice);
        speechVoice = audio.play(audio.sound(std::string("SPEAK_E\\") + file), 1.f, 0.f, false);
    };
    int rotorVoice = audio.ok() && !playerTank ? audio.play(audio.sound("ROTOR04.WAV"), 0.f, 0.f, true) : 0;
    float aimPitch = -0.05f;
    size_t pylon = 0;
    Vec3 leadSmooth{1e9f, 0, 0};                 // lead point on the screen (smoothed)
    int lockedTarget = -1;                       // world index of the locked enemy (0x253800; guided rockets)
    bool lockReq1 = false, lockReq2 = false;     // Lock Target No. 1 / No. 2 pressed (0x253cc0 / 0x253cc2)
    bool lockCycling = false; float lockCycleTick = -1;   // 0x1930fc / 0x253844: stepping outwards with No. 2
    std::vector<int> visibleTargets;             // on-screen enemies (smart bomb)

    // Free camera state
    Vec3 eye{0, 0, 0}; float yaw = 90, pitch = -35;
    if (opt.haveCam) { eye = {opt.cam[0], opt.cam[1], opt.cam[2]}; yaw = opt.cam[3]; pitch = opt.cam[4]; }
    else if (playerInst) { eye = heli.pos + Vec3{0, -67, 37}; pitch = -8; }
    else {
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& o : world.objects()) for (int a = 0; a < 3; a++) {
            mn[a] = std::min(mn[a], o.pos[a] * r.unitScale); mx[a] = std::max(mx[a], o.pos[a] * r.unitScale);
        }
        eye = {(mn[0] + mx[0]) / 2, mn[1] - 300.f, mx[2] + 400.f};
    }
    // Chase camera state
    float camYaw = heli.yaw;
    // external view look-around (OpenAssault addition): fixed side (F3 cycles, or Alt / pad A + arrows) and a free offset
    // (Alt + mouse, or pad A + right stick) that returns to the fixed side when released
    float fixedTarget = 0, fixedYaw = 0, orbYaw = 0, orbPitch = 0;
    bool lookHeld = false;
    if (const char* fv = std::getenv("XA_FIXEDVIEW")) fixedTarget = fixedYaw = float(std::atoi(fv)) * 1.5707963f;   // testing: 0 behind, 1 right, 2 front, 3 left
    Vec3 chaseEye = heli.pos + Vec3{0, -45, 14};

    if (flying && opt.shot.empty()) SDL_SetRelativeMouseMode(SDL_TRUE);
    bool running = true; Uint64 last = SDL_GetPerformanceCounter();
    int shotIndex = 0;
    bool quitAfterShot = false, crashShotNow = false;
    float simTime = 0, accumulator = 0;
    const float step = 1.f / 60.f;
    float mouseYawLeft = 0;
    // 0x6458e: idle hover. Without forward / sideways / turn input the helicopter drifts every tick by
    // x += sin(((T + phase) * 27) & 0xfff) / rand(1500..3000) and z += sin((T * 33) & 0xfff) / rand(1000..8000)
    // game units (integer division, TRIG.IMG sine, 16383 = 1). Input picks a new phase rand(0x400..0x1000);
    // after 50 ticks on the ground it stops. Also runs during the briefing.
    float cutTick = -1; Vec3 cutCam;                 // HELIMOTION cutscene camera (ticks since its start, position)
    int bobTick = 0, bobPhase = 0x400, bobLanded = -1;
    float bobAcc = 0;
    auto bobRand = [](int lo, int hi) { return lo + std::rand() % (hi - lo + 1); };
    auto bobSin = [](int a) { return int(std::lround(16383.0 * std::sin(double(a & 0xfff) * 6.283185307179586 / 4096.0))); };
    auto hoverBob = [&](bool input) {
        bobTick++;
        if (heli.landed) { if (bobLanded < 0) bobLanded = bobTick; } else bobLanded = -1;
        if (bobLanded >= 0 && bobTick - bobLanded >= 50) return;
        if (input) { bobPhase = bobRand(0x400, 0x1000); return; }
        heli.pos.x += float(bobSin((bobTick + bobPhase) * 27) / bobRand(1500, 3000)) * r.unitScale;
        heli.pos.z += float(bobSin(bobTick * 33) / bobRand(1000, 8000)) * r.unitScale;
    };
    // remappable controls (Options > Controls Setup)
    auto held = [&](Control c) {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
        for (int slot = 0; slot < 2; slot++) {
            int v = opt.settings.keys[c][slot];
            if (v >= kMouse) { if (mb & SDL_BUTTON(v - kMouse)) return true; }
            else if (v > 0 && v < SDL_NUM_SCANCODES && ks[v]) return true;
        }
        return opt.settings.pad[c] >= pad::kPad && pad::held(opt.settings.pad[c] - pad::kPad);
    };
    auto pressed = [&](Control c, const SDL_Event& ev) {
        if (opt.settings.pad[c] >= pad::kPad && pad::lastPressed() == opt.settings.pad[c] - pad::kPad) return true;
        for (int slot = 0; slot < 2; slot++) {
            int v = opt.settings.keys[c][slot];
            if (v >= kMouse && ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == v - kMouse) return true;
            if (v > 0 && v < kMouse && ev.type == SDL_KEYDOWN && !ev.key.repeat && int(ev.key.keysym.scancode) == v) return true;
        }
        return false;
    };
    int forceVehicle = 0;                       // cheat: 1 helicopter, 2 tank
    float scripted = opt.flySeconds;           // --fly: run the simulation with fixed input first
    float titleTimer = 0;
    bool askQuit = false;                        // Esc in a menu-started game: TERMINATE MISSION? (Y/N)
    float levelDone = -1;                        // seconds until the next operation area loads
    while (running) {
        SDL_Event e;
        bool takeShot = false;
        float mouseDX = 0, mouseDY = 0;
        lookHeld = false;
        while (SDL_PollEvent(&e)) {
            if (fullscreenKey(e)) { setFullscreen(win, opt.settings, !opt.settings.fullscreen); continue; }
            pad::event(e);
            if (SDL_Keycode pk = pad::menuKey(e, true)) {   // Start = Esc; A skips the briefing / continues / answers Y
                if (pk == SDLK_ESCAPE) e = keyEvent(pk);
                else if (askQuit) e = keyEvent(SDLK_y);
                else if (briefing.active() || endScreen) e = keyEvent(SDLK_RETURN);
            } else if (askQuit && e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_B) e = keyEvent(SDLK_n);
            if (e.type == SDL_QUIT) running = false;
            if (endScreen) {                                              // level-end screen: a key continues
                if (e.type == SDL_KEYDOWN && endT > 1.f) { if (bonusLeft > 0) { combat.score += int(bonusLeft * float(combat.scorePercent)); bonusLeft = 0; } opt.carryScore = combat.score; opt.carryTicks = playTicks; opt.carryArms = toSaveArms(); opt.haveArms = true; exitCode = endToMenu ? RunMenu : RunNextLevel; running = false; }
                continue;
            }
            if (e.type == SDL_KEYDOWN && briefing.active() && (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_RETURN)) { briefing.skip(); continue; }
            if (e.type == SDL_MOUSEBUTTONDOWN && briefing.active()) { briefing.skip(); continue; }
            if (e.type == SDL_KEYDOWN && askQuit) {                       // TERMINATE MISSION? (Y/N)
                if (e.key.keysym.sym == SDLK_y || e.key.keysym.sym == SDLK_z || e.key.keysym.sym == SDLK_i) { exitCode = RunMenu; running = false; }   // I: "igen" (Hungarian texts)
                else if (e.key.keysym.sym == SDLK_n || e.key.keysym.sym == SDLK_ESCAPE) askQuit = false;
                continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) { if (opt.menu) askQuit = true; else running = false; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12) takeShot = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_3) combat.selectWeapon(int(e.key.keysym.sym - SDLK_1));
            if (pressed(CChangeWeapon, e)) { for (int q = 1; q <= 3; q++) { int nw = (combat.arms.sel + q) % 3; if (nw < 2 || combat.arms.weapon3) { combat.selectWeapon(nw); break; } } }
            if (e.type == SDL_MOUSEWHEEL && flying) { int d = e.wheel.y > 0 ? 2 : 1; for (int q = 0; q < 3; q++) { int nw = (combat.arms.sel + d * (q + 1)) % 3; if (nw < 2 || combat.arms.weapon3) { combat.selectWeapon(nw); break; } } }
            if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_F7 || e.key.keysym.sym == SDLK_F8) && playerInst) {   // viewing distance -/+
                opt.settings.viewDistance = std::clamp(opt.settings.viewDistance + (e.key.keysym.sym == SDLK_F8 ? 25 : -25), 50, 300); profile::saveSettings(opt.settings);
                std::string sc = currentScape; currentScape.clear(); if (!sc.empty()) applyScapeByName(sc);
                pickupMsg = {"VIEW DISTANCE " + std::to_string(opt.settings.viewDistance) + "%", 2.f, false};
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F6 && playerInst) {   // F6: viewing range (INGAME 72..74)
                viewRange = (viewRange + 1) % 3; opt.settings.viewRange = viewRange; profile::saveSettings(opt.settings);
                std::string sc = currentScape; currentScape.clear(); if (!sc.empty()) applyScapeByName(sc);
                pickupMsg = {ingameText.get(72 + viewRange), 2.f, false};
            }
            if (e.type == SDL_KEYDOWN && opt.cheats && (e.key.keysym.mod & KMOD_ALT) && (e.key.keysym.sym == SDLK_8 || e.key.keysym.sym == SDLK_9))
                forceVehicle = e.key.keysym.sym == SDLK_8 ? 1 : 2;
            if (pressed(CCockpit, e) && playerInst && cockpitModel >= 0 && !playerTank) cockpit = !cockpit;
            if (pressed(CLock1, e) && playerInst) lockReq1 = true;
            if (pressed(CLock2, e) && playerInst) lockReq2 = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1 && playerInst) {
                flying = !flying;
                SDL_SetRelativeMouseMode(flying ? SDL_TRUE : SDL_FALSE);
                if (!flying) { eye = chaseEye; yaw = camYaw * 57.29578f + 90.f; pitch = -15; }
            }
            if (e.type == SDL_MOUSEMOTION) {
                if (flying) { mouseDX += float(e.motion.xrel); mouseDY += float(e.motion.yrel); }   // the guns are fixed (no free aiming), see the auto-aim below
                else if (e.motion.state & SDL_BUTTON_RMASK) {
                    yaw -= e.motion.xrel * 0.2f; pitch = std::clamp(pitch - e.motion.yrel * 0.2f, -89.f, 89.f);
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = std::min(0.1f, float(now - last) / float(SDL_GetPerformanceFrequency())); last = now;
        if (askQuit) dt = 0;
        if (!speakQueued.empty()) { speak(speakQueued.c_str()); speakQueued.clear(); }
        if (!voiceQueue.empty() && !(briefVoice && audio.playing(briefVoice))) {     // next part of a briefing voice
            char path[64]; std::snprintf(path, sizeof path, "SPEAK_E\\LEVEL%02d\\%s", opt.level, voiceQueue.front().c_str());
            voiceQueue.erase(voiceQueue.begin());
            briefVoice = audio.play(audio.sound(path), 1.f, 0.f, false);
            if (std::getenv("XA_SNDDEBUG")) std::printf("briefing voice %s -> %d\n", path, briefVoice);
        }
        if (!briefing.active() && (briefVoice || !voiceQueue.empty())) { if (briefVoice) audio.stop(briefVoice); briefVoice = 0; voiceQueue.clear(); }
        {   // INCOMING MISSILE! (0x3e6c9 + speech 008)
            static bool wasIncoming = false;
            bool inc = flying && combat.incomingMissile();
            if (inc && !wasIncoming) speak("008.WAV");
            wasIncoming = inc;
        }
        if (bonusLeft > 0) {
            // 0x3da64: the remaining mission time counts down 60x fast, 100 points per second (x difficulty %)
            float dec = std::min(bonusLeft, 60.f * (endScreen ? std::max(dt, 0.f) + 0.f : dt));
            if (endScreen) { static Uint64 lastB = SDL_GetPerformanceCounter(); Uint64 nb = SDL_GetPerformanceCounter(); dec = std::min(bonusLeft, 60.f * std::min(0.1f, float(nb - lastB) / float(SDL_GetPerformanceFrequency()))); lastB = nb; }
            bonusLeft -= dec;
            combat.scoreFrac += dec * 100.f * float(combat.scorePercent) / 100.f;
            int whole = int(combat.scoreFrac); combat.score += whole; combat.scoreFrac -= float(whole);
            if (combat.score - bonusBeepScore > 50) { bonusBeepScore = combat.score; if (audio.ok()) audio.play(audio.sound("TEXTBEEP.WAV"), 1.f, 0.f, false); }   // 0x3dae0
        }
        if (endScreen) {
            endT += dt; dt = 0;
            if (!opt.shot.empty() && endT > 3.f) { opt.carryScore = combat.score; opt.carryTicks = playTicks; opt.carryArms = toSaveArms(); opt.haveArms = true; exitCode = endToMenu ? RunMenu : RunNextLevel; running = false; }
        }
        if (levelDone > 0) { levelDone -= dt; if (levelDone <= 0) { exitCode = RunNextLevel; running = false; } }
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        const float d2r = 3.14159265f / 180.f;

        if (playerInst && audio.ok()) {
            bool b = briefing.active();
            if (b && !wasBriefing) playMusic("MISSION", true);
            if (!b && (wasBriefing || currentMusic.empty()) && !mission.music.empty() && deathTimer <= 0) playMusic(mission.music, true);
            wasBriefing = b;
        }
        if (briefing.heliArrived) {                 // HELIMOTION finished (or skipped): continue from its end point
            briefing.heliArrived = false;
            heli.pos = briefing.heliPos; heli.yaw = briefing.heliYaw; heli.vel = {}; heli.landed = false;
            missionStartPos = heli.pos; missionStartYaw = heli.yaw;
        }
        if (playerInst) { checkGates(lastGatePos, heli.pos); lastGatePos = heli.pos; }
        bool fastForward = false;
        if (distortVoice && briefing.distort <= 0) { audio.stop(distortVoice); distortVoice = 0; }   // 0x914d4
        if (briefing.active() && playerInst) {
            if (opt.briefShot > 0.05f) { dt = 1.f / 30.f; fastForward = true; }   // testing: skip rendering until the shot
            briefing.zoomOutTicks = cockpit ? 45.f : 80.f; briefing.update(dt);
            if (briefing.distort > 0 && std::getenv("XA_DISTSHOT") && opt.briefShot > 0.05f) opt.briefShot = 0.04f;   // testing
            if (briefing.distort > 0 && !distortVoice && audio.ok()) distortVoice = audio.play(audio.sound("DISTORT.WAV"), 0.8f, 0.f, true);
            if (briefing.heliMoving) {
                Vec3 prev = heli.pos;
                heli.pos = briefing.heliPos; heli.yaw = briefing.heliYaw; heli.tiltPitch = briefing.heliPitch; heli.tiltRoll = briefing.heliRoll;
                heli.vel = dt > 0 ? (heli.pos - prev) * (1.f / dt) : Vec3{}; heli.rotorSpeed = 1; heli.landed = false;
                heli.rotorTicks += dt * 25.f * 1.6f;          // rotor turns during the flight
                camYaw = heli.yaw;                            // chase camera follows the flight
                Vec3 back{std::sin(camYaw), -std::cos(camYaw), 0};
                Vec3 want = heli.pos + back * 42.f + Vec3{0, 0, 13.f};
                Vec3 hit; int owner;
                if (coll.raycast(heli.pos + Vec3{0, 0, 3}, want, hit, owner)) want = hit + normalize(heli.pos - hit) * 2.f;
                chaseEye = chaseEye + (want - chaseEye) * (1.f - std::exp(-dt * 12.f));
                // 0x67fbd cutscene camera: at the start a fixed point, the farthest of target + k*(10000, 15000, 0)
                // (k = 0..9) whose next step is still free (0x57570: ground, buildings, units); for 250 ticks it
                // stays there and watches the helicopter, then it closes in on the chase camera by
                // ((t - 250) / 4 + 4) / 128 per tick, from 400 ticks on it is the chase camera
                if (cutTick < 0) {
                    cutTick = 0;
                    cutCam = chaseEye;
                    const Vec3 d = Vec3{10000.f, 15000.f, 0.f} * r.unitScale;
                    for (int k = 0; k < 10; k++) {
                        Vec3 c = heli.pos + d * float(k), hit; int owner;
                        if (coll.raycast(c, c + d, hit, owner)) break;
                        cutCam = c;
                    }
                }
                for (float t = cutTick + dt * 60.f; cutTick < t; cutTick += 1.f) {
                    if (cutTick >= 400.f) cutCam = chaseEye;
                    else if (cutTick >= 250.f) cutCam = cutCam + (chaseEye - cutCam) * std::min(1.f, (std::floor((cutTick - 250.f) / 4.f) + 4.f) / 128.f);
                }
            } else if (!playerTank) {
                // standing briefing: the objects keep running, so the rotor turns (ROTATE_PART) and the
                // helicopter hovers (0x6458e)
                heli.rotorTicks += dt * 25.f * heli.rotorSpeed * 1.6f;
                for (bobAcc += dt; bobAcc >= step; bobAcc -= step) hoverBob(false);
            }
            accumulator = 0;
            combat.update(dt);
            simTime += dt;
            playerInst->pos = heli.pos; playerInst->rot = heli.rotation(); playerInst->rotorTicks = heli.rotorTicks;
            if (opt.briefShot > 0) { opt.briefShot -= dt; if (opt.briefShot <= 0) { takeShot = true; quitAfterShot = true; } }
        } else if (flying || scripted > 0) {
            if (opt.briefShot > 0 && scripted <= 0) { if (std::getenv("XA_DEBUG")) std::printf("ff %.2f dt %.4f\n", opt.briefShot, dt); if (opt.briefShot > 0.05f) { fastForward = true; dt = 1.f / 30.f; } opt.briefShot -= dt; if (opt.briefShot <= 0) { takeShot = true; quitAfterShot = true; } }   // testing: --brief past the briefing
            HeliInput in;
            if (scripted > 0) {
                in.forward = opt.flyInput[0]; in.strafe = opt.flyInput[1]; in.lift = opt.flyInput[2];
                in.yawRate = opt.flyInput[3];
                dt = step; scripted -= step;
                Vec3 tgt;
                if (const char* tp = std::getenv("XA_TP")) {                 // testing: teleport once (GL units)
                    static float tpDelay = 0; static bool tpDone = false; tpDelay += step;
                    if (!tpDone && tpDelay > 1.f) { tpDone = true; std::sscanf(tp, "%f,%f,%f", &heli.pos.x, &heli.pos.y, &heli.pos.z); heli.vel = {}; }
                }
                static const bool pickTest = std::getenv("XA_PICKTEST") != nullptr;   // testing: fly to the nearest extra
                if (pickTest && combat.nearestPickup(heli.pos, tgt) && dot(tgt - heli.pos, tgt - heli.pos) < std::pow(std::max(300.f, float(std::atof(std::getenv("XA_PICKTEST")))), 2.f)) {
                    Vec3 d = tgt - heli.pos;
                    heli.yaw = std::atan2(-d.x, d.y);
                    in.lift = std::clamp((d.z + 2.f) * 0.3f, -1.f, 1.f);
                    in.forward = std::clamp(std::sqrt(d.x * d.x + d.y * d.y) * 0.05f, 0.f, 0.6f);
                    static Vec3 lastTgt{1e9f, 0, 0};
                    if (std::fabs(lastTgt.x - tgt.x) > 0.1f) { lastTgt = tgt; heli.pos = tgt + Vec3{12, 0, 6}; heli.vel = {0, 0, 0}; }
                    combat.arms.immortal = 10000;  // test: start near the extra, then fly normally
                    static float pt = 0; pt += step; if (pt > 1) { pt = 0; std::printf("pick: heli %.0f,%.0f,%.0f -> %.0f,%.0f,%.0f shield %.0f\n", heli.pos.x, heli.pos.y, heli.pos.z, tgt.x, tgt.y, tgt.z, combat.arms.shield); }
                } else if (std::getenv("XA_HUNTWI") && r.instanceForWorldObject(std::atoi(std::getenv("XA_HUNTWI")))) {   // testing: watch one unit
                    tgt = r.instanceForWorldObject(std::atoi(std::getenv("XA_HUNTWI")))->pos;
                    static bool placed = false;
                    if (!placed) { placed = true; Vec3 off{0, -45, 15}; if (const char* o = std::getenv("XA_HUNTOFF")) std::sscanf(o, "%f,%f,%f", &off.x, &off.y, &off.z); heli.pos = tgt + off; heli.vel = {}; }
                    Vec3 d = tgt - heli.pos; heli.yaw = std::atan2(-d.x, d.y);
                    in.forward = 0; in.lift = 0;
                } else if (std::getenv("XA_HUNT") && enemies.nearest(heli.pos, tgt)) {   // testing: hop next to the nearest enemy and shoot
                    static float hop = 0; hop -= step;
                    if (hop <= 0) { hop = 2.f; heli.pos = tgt + Vec3{0, -35, 18}; heli.vel = {}; std::printf("hunt: enemy at %.0f %.0f %.0f\n", tgt.x, tgt.y, tgt.z);
                        std::vector<std::pair<Vec3, Vec3>> cs; std::vector<int> ids; enemies.contacts(cs, &ids);
                        for (size_t q = 0; q < cs.size(); q++) { Vec3 dd = cs[q].first - tgt; if (dot(dd, dd) < 1.f) { const auto* tq = types.get(world.objects()[size_t(ids[q])].objClass); const auto* tg = combat.target(ids[q]);
                            std::printf("  wi %d %s set %s target %d hp %.0f\n", ids[q], tq ? tq->name.c_str() : "?", world.objects()[size_t(ids[q])].set.c_str(), tg ? int(tg->isTarget) : -1, tg ? tg->hp : 0.f); } } }
                    Vec3 d = tgt - heli.pos;
                    heli.yaw = std::atan2(-d.x, d.y);
                    aimPitch = std::clamp(std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)), -0.6f, 0.25f);
                    in.forward = 0; in.lift = 0;
                } else if (opt.autoAim && enemies.nearest(heli.pos, tgt)) {
                    Vec3 d = tgt - heli.pos;
                    heli.yaw = std::atan2(-d.x, d.y);
                    aimPitch = std::clamp(std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)), -0.6f, 0.25f);
                }
                if (scripted <= 0 && !opt.shot.empty()) flying = true;
            } else {
                in.forward = float(held(CForward)) - float(held(CBackward));
                in.strafe = float(held(CDriftRight)) - float(held(CDriftLeft));
                in.lift = float(held(CUp)) - float(held(CDown));
                in.yawRate = (float(held(CTurnLeft)) - float(held(CTurnRight))) * 1.6f;   // = full rotation input (12)
                {   // external view: look around while Alt / pad A is held (flying keys and the mouse turn pause meanwhile)
                    const bool outsideView = !(cockpit && !playerTank);
                    lookHeld = outsideView && (k[SDL_SCANCODE_LALT] || pad::held(SDL_CONTROLLER_BUTTON_A));
                    static bool f3Prev = false, snapPrev[4] = {false, false, false, false};
                    const bool f3 = k[SDL_SCANCODE_F3];
                    if (outsideView && f3 && !f3Prev) fixedTarget = std::fmod(fixedTarget + 1.5707963f, 6.2831853f);    // behind, right, front, left
                    f3Prev = f3;
                    const bool snap[4] = {k[SDL_SCANCODE_UP] || pad::held(SDL_CONTROLLER_BUTTON_DPAD_UP),
                                          k[SDL_SCANCODE_RIGHT] || pad::held(SDL_CONTROLLER_BUTTON_DPAD_RIGHT),
                                          k[SDL_SCANCODE_DOWN] || pad::held(SDL_CONTROLLER_BUTTON_DPAD_DOWN),
                                          k[SDL_SCANCODE_LEFT] || pad::held(SDL_CONTROLLER_BUTTON_DPAD_LEFT)};
                    for (int q = 0; q < 4; q++) { if (lookHeld && snap[q] && !snapPrev[q]) fixedTarget = float(q) * 1.5707963f; snapPrev[q] = snap[q]; }
                    if (lookHeld) {
                        orbYaw -= mouseDX * 0.004f; orbPitch = std::clamp(orbPitch + mouseDY * 0.004f, -0.2f, 1.2f);
                        orbYaw += pad::axis(SDL_CONTROLLER_AXIS_RIGHTX) * dt * 2.5f;
                        orbPitch = std::clamp(orbPitch - pad::axis(SDL_CONTROLLER_AXIS_RIGHTY) * dt * 1.5f, -0.2f, 1.2f);
                        in.forward = in.strafe = in.lift = in.yawRate = 0;
                    } else mouseYawLeft += -mouseDX * 0.0024f;                   // mouse turns directly (spread over the ticks)
                }
                in.boost = k[SDL_SCANCODE_LSHIFT] || pad::held(SDL_CONTROLLER_BUTTON_LEFTSTICK);
                // controller sticks: left = forward / back and turn, right = drift and height (tank: gun elevation)
                auto stick = [&](int fn) {      // Options > Controls: stick per function (default left Y / X, right Y / X)
                    int b = opt.settings.stick[fn];
                    if (lookHeld && b >= 0 && (b / 2 == SDL_CONTROLLER_AXIS_RIGHTX || b / 2 == SDL_CONTROLLER_AXIS_RIGHTY)) return 0.f;   // the right stick looks around
                    return b < 0 ? 0.f : kStickNatural[fn] * ((b & 1) ? -1.f : 1.f) * pad::axis(SDL_GameControllerAxis(b / 2));
                };
                in.forward = std::clamp(in.forward + stick(SForward), -1.f, 1.f);
                in.yawRate = std::clamp(in.yawRate + stick(STurn) * 1.6f, -1.6f, 1.6f);
                in.strafe = std::clamp(in.strafe + stick(SDrift), -1.f, 1.f);
                in.lift = std::clamp(in.lift + stick(SHeight), -1.f, 1.f);
                if (std::getenv("XA_PADDEBUG")) std::printf("pad in f %.2f y %.2f s %.2f l %.2f pos %.1f %.1f %.1f\n", in.forward, in.yawRate, in.strafe, in.lift, heli.pos.x, heli.pos.y, heli.pos.z);
            }
            bool gun = scripted > 0 ? (opt.fire & 1) : held(CFire);
            bool rocket = scripted > 0 ? (opt.fire & 2) : held(CRocket);
            bool bomb = scripted <= 0 && held(CSmartBomb);
            bool thud = scripted <= 0 && held(CThunderbolt);
            {   // [SCALESPEED] (0x33939 ...): movement rates of the mission's vehicle in percent
                const int* sc = speedScale[playerTank ? 2 : 1];
                for (int q = 0; q < 4; q++) heli.scale[q] = float(sc[q]) / 100.f;
            }
            accumulator += dt;
            while (accumulator >= step) {
                in.mouseYaw = mouseYawLeft; mouseYawLeft = 0;
                if (playerTank && scripted <= 0 && !opt.autoAim) {
                    // 0x33f89: the tank's HEIGHT axis raises / lowers the gun: speed +-2.5 per tick and step (released:
                    // back by 60/tick, at most 400), angle -= speed/64, kept in -400..90 (4096 units per turn)
                    static float elevV = 0, elev = 0;
                    float H = std::clamp(in.lift * 12.f, -12.f, 12.f);
                    if (H == 0) elevV = elevV > 0 ? std::max(0.f, elevV - 60.f) : std::min(0.f, elevV + 60.f);
                    float a = elevV + H * 20.f / 8.f;
                    if (H < 0 && elevV > 0) elevV = 0; else if (H > 0 && elevV < 0) elevV = 0; else elevV = a;
                    elevV = std::clamp(elevV, -400.f, 400.f);
                    elev = std::clamp(elev - elevV * float(speedScale[2][3]) / 100.f / 64.f, -400.f, 90.f);
                    aimPitch = -elev * 6.2831853f / 4096.f;
                }
                if (deathTimer <= 0) heli.update(in, step, &coll);
                if (deathTimer <= 0 && !playerTank)
                    hoverBob(in.forward != 0 || in.strafe != 0 || in.yawRate != 0 || in.mouseYaw != 0);
                {   // 0x37ec5: the helicopter touching a HELI_BASE becomes the tank at the nearest TANK_BASE,
                    // the tank touching a TANK_BASE the helicopter at the nearest HELI_BASE (once per contact;
                    // only if the level has the other vehicle)
                    static bool baseTouch = false;
                    bool touch = false;
                    const auto& mine = playerTank ? tankBases : heliBases;
                    const auto& other = playerTank ? heliBases : tankBases;
                    const int otherTemplate = playerTank ? heliTemplate : tankTemplate;
                    if (deathTimer <= 0 && !briefing.active() && otherTemplate >= 0 && !other.empty()) {   // touching a base of the own kind
                        for (int b : mine) {
                            const WorldObject& o = world.objects()[size_t(b)];
                            Vec3 bp{o.pos[0] * r.unitScale, o.pos[1] * r.unitScale, o.pos[2] * r.unitScale};
                            float br = std::max(4.f, float(o.radius) * r.unitScale);
                            Vec3 d = heli.pos - bp;
                            if (d.x * d.x + d.y * d.y < (br + heli.radius) * (br + heli.radius) && std::fabs(d.z) < br + heli.clearance + 2.f) touch = true;
                        }
                    }
                    bool forced = forceVehicle != 0 && (forceVehicle == 2) != playerTank && otherTemplate >= 0;   // Alt+8 / Alt+9 (0x30d10)
                    forceVehicle = 0;
                    if ((touch && !baseTouch && !other.empty()) || forced) {
                        int best = -1; float bestD = 1e30f;
                        for (int b : other) {
                            const WorldObject& o = world.objects()[size_t(b)];
                            Vec3 d = heli.pos - Vec3{o.pos[0] * r.unitScale, o.pos[1] * r.unitScale, o.pos[2] * r.unitScale};
                            if (dot(d, d) < bestD) { bestD = dot(d, d); best = b; }
                        }
                        const WorldObject& to = world.objects()[size_t(otherTemplate)];
                        playerTank = !playerTank;
                        Heli nh; configureVehicle(nh);
                        if (forced || best < 0) { nh.pos = heli.pos; nh.yaw = heli.yaw; }
                        else {
                            const WorldObject& bo = world.objects()[size_t(best)];
                            nh.pos = {bo.pos[0] * r.unitScale, bo.pos[1] * r.unitScale, bo.pos[2] * r.unitScale};
                            nh.yaw = -to.angle[4] * 6.2831853f / 4096.f;  // the template's placement angle (clockwise)
                        }
                        float g = coll.groundBelow(nh.pos + Vec3{0, 0, 5});
                        if (g > -1e8f) nh.pos.z = g + nh.clearance;
                        nh.rotorSpeed = playerTank ? 1.f : 0.35f;
                        heli = nh;
                        if (playerInst) {
                            if (Renderer::Instance* ti = r.instanceForWorldObject(otherTemplate)) { playerInst->model = ti->model; playerInst->objClass = ti->objClass; }
                            else { playerInst->model = int(to.proto); playerInst->objClass = int(to.objClass); }
                        }
                        if (playerTank) cockpit = false;
                        if (audio.ok()) {
                            if (playerTank && rotorVoice) { audio.stop(rotorVoice); rotorVoice = 0; }
                            if (!playerTank && !rotorVoice) rotorVoice = audio.play(audio.sound("ROTOR04.WAV"), 0.f, 0.f, true);
                        }
                        std::printf("Vehicle change: now %s (%s)\n", playerTank ? "tank" : "helicopter", forced ? "cheat" : "base");
                    }
                    baseTouch = touch;
                }
                {   // touching energy barriers, crushing walls and blocks hurts: ENEMY.DEF cause_damage, once per 30 ticks
                    static float touchCool = 0; touchCool -= step;
                    int owner = coll.takeContact();
                    if (owner >= 0 && size_t(owner) < world.objects().size() && touchCool <= 0 && deathTimer <= 0) {
                        if (const ObjectType* tq = types.get(world.objects()[size_t(owner)].objClass)) {
                            std::string b = upperS(tq->behavior.substr(0, tq->behavior.find('.')));
                            if (b == "STATIC_SHIELD" || b == "SHIELD_ENEMY" || b == "MOVINGWALL" || b == "BLOCK_ENEMY") {
                                int dmg = defs.get(tq->behavior, "cause_damage", 0);
                                const Combat::Target* tg = combat.target(owner);
                                if (dmg > 0 && (!tg || tg->alive)) { combat.damagePlayer(float(dmg)); touchCool = 0.5f; }
                            }
                        }
                    }
                }
                {   // magma drive (0x3378d): an extra forward push ramps up by 90/tick to the drive speed
                    // (heli 3780, tank 1800 raw units/tick, or the pad's MAGMA_SPEED_AND_TIME) and falls by 180/tick
                    static float magmaPush = 0;
                    float target = combat.arms.magmaOn ? (combat.arms.magmaSpeed > 0 ? combat.arms.magmaSpeed : (playerTank ? 1800.f : 3780.f)) : 0.f;
                    // tank (0x33de1): +16 / -48 per tick
                    float rate = (combat.arms.magmaOn ? (playerTank ? 16.f : 90.f) : (playerTank ? 48.f : 180.f)) * step * 60.f;
                    magmaPush += std::clamp(target - magmaPush, -rate, rate);
                    if (magmaPush > 0 && deathTimer <= 0) {
                        Vec3 fwd{-std::sin(heli.yaw), std::cos(heli.yaw), 0};
                        Vec3 body = heli.pos + fwd * (magmaPush * r.unitScale);   // moved along the heading every tick
                        Vec3 v0 = fwd * (magmaPush * r.unitScale * 60.f);
                        coll.resolveSphere(body, heli.radius, &v0, nullptr);
                        heli.pos = body;
                        static float flameT = 0; flameT -= step;
                        if (combat.arms.magmaOn && flameT <= 0) { flameT = 0.04f; combat.firePuff(heli.pos - fwd * (playerTank ? 5.f : 7.f) + Vec3{0, 0, 1.f}, 2.2f); }   // AFTERBURNER (MAGMASTRAHL)
                    }
                }
                if (heli.pos.z > ceilingZ) { heli.pos.z = ceilingZ; heli.vel.z = std::min(heli.vel.z, 0.f); }   // MAXHEIGHT of the area
                Mat4 hr = Mat4::rotZ(heli.yaw);
                Vec3 aim = normalize(hr.transformPoint({0, std::cos(aimPitch), std::sin(aimPitch)}));
                // RE 0x693c6: the guns fire straight ahead; with a locked target within +-150/4096 turn
                // (13 deg) of the nose the shot is aimed at the target (vertical and horizontal).
                if (lockedTarget >= 0) {
                    if (const Renderer::Instance* ti = r.instanceForWorldObject(lockedTarget)) {
                        Vec3 muzzle = heli.pos + heli.rotation().transformPoint(playerTank ? Vec3{0, 5.f, 1.2f} : Vec3{0, 8.5f, -1.6f});
                        Vec3 d = ti->pos + Vec3{0, 0, 1.f} - muzzle;
                        float ty = std::atan2(-d.x, d.y), dyaw = ty - heli.yaw;
                        while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
                        while (dyaw < -3.14159265f) dyaw += 6.2831853f;
                        if (std::fabs(dyaw) < 150.f / 4096.f * 6.2831853f && dot(d, d) > 1.f) aim = normalize(d);
                    }
                }
                if (gun && heli.rotorSpeed > 0.5f && combat.fireWeapon(heli.pos + heli.rotation().transformPoint(playerTank ? Vec3{0, 5.f, 1.2f} : Vec3{0, 8.5f, -1.6f}), hr.transformPoint({1, 0, 0}), aim, heli.vel))
                    pad::rumble(playerTank ? 0.3f : 0.12f, playerTank ? 120 : 60);          // controller: gun recoil
                if ((rocket || bomb || thud) && heli.rotorSpeed > 0.5f) {
                    Vec3 o = heli.pos + heli.rotation().transformPoint(rocketPylons[pylon % rocketPylons.size()]);
                    if (bomb && combat.arms.smartbombs > 0 && combat.rocketCooldown <= 0 && visibleTargets.empty()) {
                        pickupMsg = {ingameText.get(27), 1.f, false};                // NO TARGET AVAILABLE
                        combat.rocketCooldown = 0.6f;
                    }
                    bool fired = bomb ? combat.fireSmartbomb(o, heli.vel, visibleTargets)
                               : thud ? combat.fireThunderbolt(o, aim, heli.vel, lockedTarget)
                                      : combat.fireRocket(o, aim, heli.vel, lockedTarget);
                    if (fired) { pylon++; pad::rumble(bomb || thud ? 0.8f : 0.4f, bomb || thud ? 400 : 150); }
                }
                combat.setPlayer(heli.pos, 4.f, deathTimer <= 0);
                combat.playerLanded = heli.landed || (heli.pos.z - coll.groundBelow(heli.pos + Vec3{0, 0, 1}) < heli.clearance + 1.5f && dot(heli.vel, heli.vel) < 4.f);
                enemies.update(step, heli.pos, deathTimer <= 0);
                combat.update(step);
                if (opt.crashShot >= 0 && enemies.anyCrashing()) { opt.crashShot -= step; if (opt.crashShot < 0) { scripted = 0; quitAfterShot = true; crashShotNow = true; } }
                if (deathTimer <= 0) {
                    const float dmg = combat.takePlayerDamage();
                    combat.arms.shield -= dmg;
                    if (dmg > 0) pad::rumble(0.25f + std::min(0.75f, dmg / 800.f), 160);     // controller: hit
                    if (combat.arms.shield <= 0) {
                        pad::rumble(1.f, 700);
                        combat.arms.shield = 0;
                        // out of control: the helicopter falls, spinning, until it hits something
                        deathTimer = 30.f; crashing = true; crashSpin = 3.f;
                        heli.vel.z = std::max(heli.vel.z, 3.f);
                        combat.explodeAt(heli.pos, 6.f, false);
                        std::printf("Helicopter destroyed\n");
                        playMusic("DEAD", false);
                    }
                } else {
                    combat.takePlayerDamage();
                    if (crashing) {
                        heli.vel.z -= 30.f * step;
                        heli.yaw += crashSpin * step;
                        heli.tiltRoll = std::min(heli.tiltRoll + step * 0.7f, 0.9f);
                        heli.rotorSpeed = std::max(0.f, heli.rotorSpeed - step * 0.3f);
                        heli.rotorTicks += step * 25.f * heli.rotorSpeed * 1.6f;
                        Vec3 next = heli.pos + heli.vel * step, hit; int owner;
                        float g = coll.groundBelow(heli.pos + Vec3{0, 0, 1});
                        crashSmoke -= step;
                        if (crashSmoke <= 0) { combat.smokePuff(heli.pos, 4.f); combat.firePuff(heli.pos, 2.5f); crashSmoke = 0.05f; }
                        if (coll.raycast(heli.pos, next, hit, owner) || (g > -1e8f && next.z - heli.clearance <= g)) {
                            crashing = false; deathTimer = 3.f;
                            combat.explodeAt(heli.pos, 25.f, true);
                            playerInst->hidden = true;
                        } else heli.pos = next;
                    }
                    deathTimer -= step;
                    if (deathTimer <= 0) {           // respawn on the pad and restart the mission
                        respawnHeli();
                        combat.arms.shield = Combat::kMaxShield; combat.arms.rockets = std::max(combat.arms.rockets, 2); crashing = false;
                        playerInst->hidden = false;
                        applyMission(mission.file, true);
                    }
                }
                missionClock += step;
                const bool inArea = insideBorder(heli.pos);
                if (inArea) wasInside = true;
                if (deathTimer <= 0 && !inArea && wasInside) {           // (a mission may start outside its area)
                    if (outsideTicks <= 180.f && outsideTicks + step * 60.f > 180.f) speakQueued = "066.WAV";
                    outsideTicks += step * 60.f;
                    if (outsideTicks >= 780.f) {
                        outsideTicks = 0;
                        std::printf("Mission %s failed: left the mission area\n", mission.file.c_str());
                        banner = {ingameText.get(10), 3.f, true};
                        applyMission(mission.file, true);
                        respawnHeli();
                    }
                } else outsideTicks = 0;
                if (missionTimer > 0) missionTimer = std::max(0.f, missionTimer - step);
                playTicks += step * 60.f;
                goalTimer -= step;
                if (goalTimer <= 0 && !enemies.anyCrashing()) {
                    goalTimer = 0.25f;
                    const auto& triggerGone = typeGone;
                    // *_WHEN_DESTROYED events (0x582f4): energy barriers go down, gates explode, objects appear
                    // once every object of the trigger TYP is destroyed. shieldDown[i]: 1 = done, 2 = timer running
                    if (shieldDown.size() != world.objects().size()) shieldDown.assign(world.objects().size(), 0);
                    for (size_t i = 0; i < world.objects().size(); i++) {
                        if (shieldDown[i] == 1) continue;
                        const ObjectType* st = types.get(world.objects()[i].objClass);
                        if (!st || st->whenDestroyed.empty() || isTemplateSet(world.objects()[i].set)) continue;
                        std::string set = upperS(world.objects()[i].set);
                        if (enemySets.count(set) && !activeSets.count(set)) continue;
                        const auto& ev = st->whenDestroyed.front();
                        if (shieldDown[i] == 0) {
                            if (!triggerGone(ev.trigger)) continue;
                            if (std::getenv("XA_DEBUG")) std::printf("event %d on %zu (%s): %s destroyed\n", ev.kind, i, st->name.c_str(), ev.trigger.c_str());
                            if (ev.kind == 2 || ev.kind == 3) { shieldDown[i] = 2; eventTimer[int(i)] = float(ev.ticks); continue; }
                        } else {                                                  // timer running (TIME_EXPLODE / FLICKER)
                            float& t = eventTimer[int(i)];
                            t -= 15.f;                                            // 0.25 s = 15 ticks
                            if (ev.kind == 3) if (Renderer::Instance* in = r.instanceForWorldObject(int(i))) in->hidden = !in->hidden;
                            if (t > 0) continue;
                        }
                        shieldDown[i] = 1;
                        Renderer::Instance* in = r.instanceForWorldObject(int(i));
                        if (ev.kind == 1 || ev.kind == 2) {
                            const Combat::Target* tg = combat.target(int(i));
                            if (tg && tg->isTarget) { combat.kill(int(i), in ? in->pos : heli.pos); continue; }
                            if (in) combat.explodeAt(in->pos, std::max(6.f, in->radius), true);     // not destroyable by shots: it just bursts
                        }
                        if (ev.kind == 4) {                                       // appear
                            if (in) in->hidden = false;
                            // 0x590a0: appearing extras (not hostages) stay for 1500 ticks when the pilot is not "easy"
                            if (combat.isPickupObject(int(i)) && opt.difficulty > 0 && st->behavior.find("HOSTAGE") == std::string::npos) combat.setPickupLife(int(i), 1500.f / 60.f);
                            coll.setOwnerEnabled(int(i), true);
                            combat.setActive(int(i), true);
                            enemies.setActive(int(i), true);
                            continue;
                        }
                        coll.setOwnerEnabled(int(i), false);                      // remove / flicker
                        combat.setActive(int(i), false);
                        enemies.setActive(int(i), false);
                        if (in) in->hidden = true;
                    }
                    for (auto& g : gates) {
                        const ObjectType* gt = types.get(size_t(g.type));
                        if (g.fired || !gt || gt->activateWhenDestroyed.empty()) continue;
                        MissionFile::Cond c; c.kind = MissionFile::Cond::Destroyed; c.typ = gt->activateWhenDestroyed;
                        if (evalCond(c)) { g.fired = true; applyScape(g.type); }
                    }
                    if (static const char* hp = std::getenv("XA_HELIPOS"); hp && !briefing.active() && !endScreen) {   // testing: start at x,y,z
                        static bool done = false; float x, y, z;
                        if (!done && std::sscanf(hp, "%f,%f,%f", &x, &y, &z) == 3) { heli.pos = {x, y, z}; heli.vel = {}; heli.landed = false; }
                        done = true;
                    }
                    static const char* autoGoal = std::getenv("XA_AUTOGOAL");      // testing: solve the goals of every mission
                    static float autoT = 0;
                    if (autoGoal && !briefing.active() && !endScreen) {
                        combat.arms.immortal = 10000; autoT -= 0.25f;
                        if (autoT <= 0) {
                            autoT = float(std::max(1, std::atoi(autoGoal)));
                            MissionFile::Rule* bestRule = nullptr;    // the main goal (most conditions), not a shortcut
                            for (auto& rule : mission.rules)
                                if (!rule.failed && !rule.conds.empty() && (!bestRule || rule.conds.size() > bestRule->conds.size())) bestRule = &rule;
                            for (auto& rule : mission.rules) {
                                if (&rule != bestRule) continue;
                                std::vector<int> orders;
                                for (const auto& c : rule.conds) orders.push_back(std::max(0, c.order));
                                std::sort(orders.begin(), orders.end()); orders.erase(std::unique(orders.begin(), orders.end()), orders.end());
                                int want = rule.stage < int(orders.size()) ? orders[size_t(rule.stage)] : -1;
                                for (const auto& c : rule.conds) {
                                    if (std::max(0, c.order) != want || evalCond(c)) continue;
                                    std::printf("autogoal %s: kind %d %s\n", mission.file.c_str(), int(c.kind), c.typ.c_str());
                                    if (c.kind == MissionFile::Cond::Collided) { heli.pos = {c.sphere[0] * r.unitScale, c.sphere[1] * r.unitScale, c.sphere[2] * r.unitScale}; heli.vel = {}; heli.landed = false; }
                                    else if (c.kind == MissionFile::Cond::Destroyed) {
                                        std::string rest = c.typ; int n = 0;
                                        while (!rest.empty()) {
                                            size_t p = rest.find(','); int cls = typeIndexByName(rest.substr(0, p));
                                            // barriers etc. go when their trigger TYP is destroyed: destroy that instead
                                            if (const ObjectType* et = cls >= 0 ? types.get(size_t(cls)) : nullptr; et && !et->whenDestroyed.empty()) {
                                                int tc = typeIndexByName(et->whenDestroyed.front().trigger);
                                                for (size_t i = 0; i < world.objects().size() && tc >= 0; i++)
                                                    if (int(world.objects()[i].objClass) == tc && !isTemplateSet(world.objects()[i].set)) {
                                                        std::string set = upperS(world.objects()[i].set);
                                                        if (enemySets.count(set) && !activeSets.count(set)) continue;
                                                        Renderer::Instance* in = r.instanceForWorldObject(int(i)); combat.kill(int(i), in ? in->pos : heli.pos);
                                                    }
                                            }
                                            for (size_t i = 0; i < world.objects().size() && cls >= 0; i++) {
                                                if (int(world.objects()[i].objClass) != cls || isTemplateSet(world.objects()[i].set)) continue;
                                                std::string set = upperS(world.objects()[i].set);
                                                if (enemySets.count(set) && !activeSets.count(set)) continue;
                                                Renderer::Instance* in = r.instanceForWorldObject(int(i));
                                                combat.kill(int(i), in ? in->pos : heli.pos); n++;
                                                if (c.nr > 0 && n >= c.nr) break;
                                            }
                                            if (p == std::string::npos) break;
                                            rest = rest.substr(p + 1);
                                        }
                                    } else if (c.kind == MissionFile::Cond::Rescued) {
                                        std::vector<Vec3> hs; combat.hostages(hs); for (auto& [rc, rn] : combat.rescued) std::printf("  rescued %s x%d\n", types.get(size_t(rc)) ? types.get(size_t(rc))->name.c_str() : "?", rn); std::printf("  hostages %zu heli %.0f %.0f %.0f landed %d\n", hs.size(), heli.pos.x, heli.pos.y, heli.pos.z, int(heli.landed));
                                        if (hs.empty()) {     // hostages are freed from buildings (RELEASE_EXTRA HOSTAGE) or cages (APPEAR_WHEN_DESTROYED)
                                            for (int wi : combat.hostageReleasers()) { Renderer::Instance* in = r.instanceForWorldObject(wi); combat.kill(wi, in ? in->pos : heli.pos); }
                                            std::string rest = c.typ;
                                            while (!rest.empty()) {
                                                size_t p = rest.find(','); int cls = typeIndexByName(rest.substr(0, p));
                                                if (const ObjectType* et = cls >= 0 ? types.get(size_t(cls)) : nullptr; et && !et->whenDestroyed.empty()) {
                                                    int tc = typeIndexByName(et->whenDestroyed.front().trigger);
                                                    for (size_t i = 0; i < world.objects().size() && tc >= 0; i++)
                                                        if (int(world.objects()[i].objClass) == tc && !isTemplateSet(world.objects()[i].set)) {
                                                            Renderer::Instance* in = r.instanceForWorldObject(int(i)); combat.kill(int(i), in ? in->pos : heli.pos);
                                                        }
                                                }
                                                if (p == std::string::npos) break;
                                                rest = rest.substr(p + 1);
                                            }
                                        }
                                        if (!hs.empty()) {      // barriers around the hostages: destroy their generators too
                                            for (size_t i = 0; i < world.objects().size(); i++) {
                                                const ObjectType* et = types.get(world.objects()[i].objClass);
                                                if (!et || et->whenDestroyed.empty() || (et->whenDestroyed.front().kind != 0 && et->whenDestroyed.front().kind != 3)) continue;
                                                if (!activeSets.count(upperS(world.objects()[i].set))) continue;
                                                int tc = typeIndexByName(et->whenDestroyed.front().trigger);
                                                for (size_t j = 0; j < world.objects().size() && tc >= 0; j++)
                                                    if (int(world.objects()[j].objClass) == tc && activeSets.count(upperS(world.objects()[j].set))) {
                                                        Renderer::Instance* in = r.instanceForWorldObject(int(j)); combat.kill(int(j), in ? in->pos : heli.pos);
                                                    }
                                            }
                                        }
                                        if (!hs.empty()) {
                                            static int hk = 0; hk++;
                                            Vec3 at = hs[size_t(hk / 2) % hs.size()] + Vec3{(hk & 1) ? 9.f : -9.f, 0, 0};
                                            at.z = hs[size_t(hk / 2) % hs.size()].z;
                                            float g = coll.groundBelow(at + Vec3{0, 0, 3});
                                            if (g > -1e8f) at.z = g + heli.clearance;
                                            heli.pos = at; heli.vel = {}; heli.landed = true; std::printf("  -> to %.0f %.0f %.0f (hostage %.0f %.0f %.0f)\n", at.x, at.y, at.z, hs[size_t(hk / 2) % hs.size()].x, hs[size_t(hk / 2) % hs.size()].y, hs[size_t(hk / 2) % hs.size()].z);
                                        }
                                    }
                                    break;
                                }
                                break;
                            }
                        }
                    }
                    for (const auto& rule : mission.rules) {
                        // a success block without conditions (MISS_51: tunnel flight) loads its follow-up right away;
                        // [CONTINUE] blocks (reaching the tunnel entrance) have no action of their own
                        if (rule.action == "[CONTINUE]") continue;
                        if (rule.conds.empty() && (rule.failed || rule.next.empty())) continue;
                        // [FAILED]: any condition (e.g. one convoy truck reaching its goal).
                        // [SUCCESSFULL]: every condition; ORDER n groups are met one after the other
                        // (a finished group stays finished).
                        bool all = true, any = false;
                        if (rule.failed) { for (const auto& c : rule.conds) if (evalCond(c)) { any = true; break; } }
                        else {
                            std::vector<int> orders;
                            for (const auto& c : rule.conds) orders.push_back(std::max(0, c.order));
                            std::sort(orders.begin(), orders.end()); orders.erase(std::unique(orders.begin(), orders.end()), orders.end());
                            auto& st = const_cast<MissionFile::Rule&>(rule).stage;
                            while (st < int(orders.size())) {
                                bool ok = true;
                                for (const auto& c : rule.conds) if (std::max(0, c.order) == orders[size_t(st)] && !evalCond(c)) { ok = false; break; }
                                if (!ok) break;
                                st++;
                            }
                            all = st >= int(orders.size());
                        }
                        if (rule.failed ? !any : !all) continue;
                        if (rule.failed) { std::printf("Mission %s failed\n", mission.file.c_str()); missionStatus = "FAILED - restarting"; banner = {ingameText.get(10), 3.f, true}; applyMission(mission.file, true); }
                        else if (rule.next == "FINALMISSION" || rule.next == "GOTOMENU") {   // end of the level
                            std::printf("Mission %s accomplished, level complete (%s)\n", mission.file.c_str(), rule.next.c_str());
                            missionStatus = "LEVEL COMPLETE"; mission.rules.clear(); endScreen = true; endT = 0; endToMenu = rule.next == "GOTOMENU"; if (timerShown && missionTimer > 0) { bonusLeft = missionTimer; bonusBeepScore = combat.score; }
                        }
                        else if (!rule.next.empty()) {
                            // 0x39434: [LOADMISSION] says "mission successful" (speech 077) and saves; [LOADSUBMISSION]
                            // continues silently and saves; [LOADNEXTMISSION] (0x35a0c) continues without a save.
                            // A mission with [DONOTSAVEGAME] is never saved.
                            std::printf("Mission %s accomplished -> %s (%s)\n", mission.file.c_str(), rule.next.c_str(), rule.action.c_str());
                            missionStatus = "accomplished"; if (timerShown && missionTimer > 0) bonusLeft = missionTimer;
                            const bool loud = rule.action == "[LOADMISSION]" && !rule.conds.empty();
                            if (loud) { banner = {ingameText.get(6), 3.f, true}; playMusic("MISOK", false); speakQueued = "077.WAV"; }
                            MissionFile peek;
                            if (rule.action != "[LOADNEXTMISSION]" && peek.load(fs, lv, rule.next, opt.difficulty) && !peek.noSave) saveGame(opt.level, rule.next, true);
                            if (applyMission(rule.next, false)) startMissionIntro();
                        }
                        else { std::printf("Mission %s accomplished, level complete\n", mission.file.c_str()); missionStatus = "LEVEL COMPLETE"; mission.rules.clear(); endScreen = true; endT = 0; endToMenu = false; if (timerShown && missionTimer > 0) bonusLeft = missionTimer; }
                        break;
                    }
                }
                accumulator -= step; simTime += step;
                if (tickerStart < 0) tickerStart = simTime;              // the ticker starts with the flight
            }
            combat.setGunSound(gun && heli.rotorSpeed > 0.5f);
            audio.setVoice(rotorVoice, 0.15f + 0.5f * heli.rotorSpeed, 0.6f + 0.5f * heli.rotorSpeed);
            playerInst->pos = heli.pos;
            playerInst->rot = heli.rotation();
            playerInst->rotorTicks = heli.rotorTicks;
            // Chase camera: behind and above, smoothed.
            float a = 1.f - std::exp(-dt * 4.f);
            float dy = heli.yaw - camYaw;
            while (dy > 3.14159265f) dy -= 6.2831853f;
            while (dy < -3.14159265f) dy += 6.2831853f;
            camYaw += dy * a;
            if (!lookHeld) { orbYaw -= orbYaw * (1.f - std::exp(-dt * 3.f)); orbPitch -= orbPitch * (1.f - std::exp(-dt * 3.f)); }
            float df = fixedTarget - fixedYaw;
            while (df > 3.14159265f) df -= 6.2831853f;
            while (df < -3.14159265f) df += 6.2831853f;
            fixedYaw += df * (1.f - std::exp(-dt * 5.f));
            const float ca = camYaw + fixedYaw + orbYaw;
            Vec3 back{std::sin(ca), -std::cos(ca), 0};
            const float dist = playerTank ? 24.f : 42.f, hgt = playerTank ? 9.f : 13.f;
            Vec3 want = heli.pos + back * (dist * std::cos(orbPitch)) + Vec3{0, 0, hgt + dist * std::sin(orbPitch)};
            // keep the camera on the helicopter's side of walls, ceilings and the ground (caves!)
            auto pullIn = [&](Vec3 p) {
                Vec3 pivot = heli.pos + Vec3{0, 0, 3.f}, hit; int owner;
                if (coll.raycast(pivot, p, hit, owner)) {
                    Vec3 d = p - pivot; float l = std::sqrt(dot(d, d));
                    Vec3 hd = hit - pivot; float hl = std::sqrt(dot(hd, hd));
                    if (l > 1e-3f) p = pivot + d * (std::max(0.f, hl - 2.f) / l);
                }
                return p;
            };
            want = pullIn(want);
            chaseEye = pullIn(chaseEye + (want - chaseEye) * (1.f - std::exp(-dt * 6.f)));
        } else {
            Vec3 fwd{std::cos(yaw * d2r) * std::cos(pitch * d2r), std::sin(yaw * d2r) * std::cos(pitch * d2r), std::sin(pitch * d2r)};
            Vec3 right = normalize(cross(fwd, {0, 0, 1}));
            float sp = (k[SDL_SCANCODE_LSHIFT] ? 800.f : 120.f) * dt;
            if (k[SDL_SCANCODE_W]) eye = eye + fwd * sp;
            if (k[SDL_SCANCODE_S]) eye = eye - fwd * sp;
            if (k[SDL_SCANCODE_D]) eye = eye + right * sp;
            if (k[SDL_SCANCODE_A]) eye = eye - right * sp;
            if (k[SDL_SCANCODE_E]) eye.z += sp;
            if (k[SDL_SCANCODE_Q]) eye.z -= sp;
            simTime += dt;
        }
        if (crashShotNow) { takeShot = true; crashShotNow = false; }
        if (scripted > 0 || (fastForward && !takeShot)) continue;

        int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
        // Briefings run on the cockpit monitor (0x2f385: D6-MAIN.RLE frame instead of the HUD); the ZOOM commands move
        // the view between the pilot's eye and that monitor (BEHAVIOR.TYP ADV_HELI CAMERA_BRIEFING -587 2700 0).
        const char* zoomTest = std::getenv("XA_ZOOMK");      // testing: fixed zoom position in the cockpit
        const bool zoomView = (briefing.active() && briefing.zooming() || (zoomTest && !briefing.active())) && !playerTank && cockpitModel >= 0 && playerInst && deathTimer <= 0;
        const bool monitorView = briefing.active() && briefing.onMonitor && !briefing.zooming() && playerInst;
        // with the outside view the zoom first flies from the chase camera into the cockpit (and back out at the end)
        const float zoomRaw = zoomTest && !briefing.active() ? float(std::atof(zoomTest)) : briefing.zoom;
        constexpr float kOutPart = 0.45f;
        const bool viaOutside = !cockpit || briefing.zoomingIn() || zoomTest;   // the briefing always starts with the helicopter seen from outside
        const bool zoomOutside = zoomView && viaOutside && zoomRaw < kOutPart * 0.9f;
        // cutscenes (HELIMOTION / SETCAMERA without the monitor, e.g. L1 MISS_51, L3 after the briefing) always show the helicopter from outside
        const bool inside = (cockpit && !playerTank && flying && !briefing.active() && deathTimer <= 0 && playerInst) || (zoomView && !zoomOutside);
        float monX0 = 0, monY0 = 0, monX1 = float(w), monY1 = float(h);        // monitor picture in window pixels
        // HELIMOTION cutscenes are letterboxed: black bars of 72 of 480 lines above and below (measured on a recording
        // of the original, 640x336 picture)
        if (!briefing.active() || !briefing.heliMoving) cutTick = -1;
        const bool letterbox = cutTick >= 0 && briefing.heliMoving && !monitorView && !zoomView;
        if (monitorView) {
            float sc = float(h) / 480.f, ox = float(w) * 0.5f - 320.f * sc;
            monX0 = ox + Hud::kMonX0 * sc; monX1 = ox + Hud::kMonX1 * sc; monY0 = Hud::kMonY0 * sc; monY1 = Hud::kMonY1 * sc;
            r.vpX = int(monX0); r.vpW = int(monX1 - monX0); r.vpY = h - int(monY1); r.vpH = int(monY1 - monY0);
        } else if (letterbox) {
            int bar = h * 72 / 480;
            monY0 = float(bar); monY1 = float(h - bar);
            r.vpX = 0; r.vpW = w; r.vpY = bar; r.vpH = h - 2 * bar;
        } else r.vpW = 0;
        const float aspect = monitorView || letterbox ? (monX1 - monX0) / std::max(1.f, monY1 - monY0) : float(w) / float(std::max(h, 1));
        float fovDeg = inside ? 70.f : 60.f;
        if (zoomView && viaOutside && zoomRaw < kOutPart) fovDeg = 60.f + 10.f * zoomRaw / kOutPart;
        Mat4 proj = Mat4::perspective(fovDeg * d2r, aspect, zoomView ? 0.005f : inside ? 0.05f : 0.5f, 20000.f);
        Vec3 camPos; Mat4 view;
        if (zoomView) {
            Mat4 hm = Mat4::translate(heli.pos) * heli.rotation();
            // left monitor of HELICOCK.3DI: green screen of texture page 7 (u 2688..10240, v 5760..11904) mapped onto
            // the tilted panel face (polys 55/56): centre (-0.526, 3.221, -0.244), 0.40 x 0.28 m, facing (0, -0.823, 0.568)
            const Vec3 eyeL{0, 1.2f, 0.85f}, mon{-0.526f, 3.221f, -0.244f}, nrm{0, -0.823f, 0.568f}, monUp{0, 0.568f, 0.823f};
            const Vec3 endL = mon + nrm * 0.218f;             // the screen fills the D6-MAIN picture (438 of 480 lines, fov 70)
            float kz = !viaOutside ? zoomRaw : std::max(0.f, (zoomRaw - kOutPart) / (1.f - kOutPart)); kz = kz * kz * (3 - 2 * kz);
            Vec3 posL = eyeL + (endL - eyeL) * kz;
            Vec3 fwdL = eyeL + Vec3{0, 1.f, 0};
            float kl = std::min(1.f, kz * 1.5f); kl = kl * kl * (3 - 2 * kl);
            Vec3 lookL = fwdL + (mon - fwdL) * kl;
            Vec3 upL = normalize(Vec3{0, 0, 1} + (monUp - Vec3{0, 0, 1}) * kz);
            camPos = hm.transformPoint(posL);
            view = Mat4::lookAt(camPos, hm.transformPoint(lookL), heli.rotation().transformPoint(upL));
            if (viaOutside && zoomRaw < kOutPart) {           // outside part: chase camera -> pilot's eye
                float ko = zoomRaw / kOutPart; ko = ko * ko * (3 - 2 * ko);
                Vec3 chaseLook = heli.pos + Vec3{0, 0, 4.f} + heli.forwardDir() * 10.f;
                Vec3 eyeW = hm.transformPoint(eyeL), eyeLook = hm.transformPoint(eyeL + Vec3{0, 10.f, 0});
                camPos = chaseEye + (eyeW - chaseEye) * ko;
                Vec3 up = normalize(Vec3{0, 0, 1} + (heli.rotation().transformPoint({0, 0, 1}) - Vec3{0, 0, 1}) * ko);
                view = Mat4::lookAt(camPos, chaseLook + (eyeLook - chaseLook) * ko, up);
            }
        } else if (briefing.active() && briefing.hasCamera) {
            camPos = briefing.camPos;
            view = Mat4::lookAt(briefing.camPos, briefing.camTarget, {0, 0, 1});
        } else if (const char* wch = std::getenv("XA_WATCH"); wch && r.instanceForWorldObject(std::atoi(wch))) {   // testing: camera on world object n
            const Renderer::Instance* wi = r.instanceForWorldObject(std::atoi(wch));
            float off[3] = {25, -25, 15};
            if (const char* wo = std::getenv("XA_WATCHOFS")) std::sscanf(wo, "%f,%f,%f", &off[0], &off[1], &off[2]);
            camPos = wi->pos + Vec3{off[0], off[1], off[2]};
            if (std::getenv("XA_WATCHSIDE")) {   // testing: camera on the right of the motion, which then goes to screen right
                Vec3 dir{1, 0, 0};
                std::vector<std::pair<Vec3, Vec3>> cs; std::vector<int> cid; enemies.contacts(cs, &cid);
                for (size_t k = 0; k < cs.size(); k++) if (cid[k] == std::atoi(wch)) { Vec3 v = cs[k].second; v.z = 0; if (dot(v, v) > 1e-6f) dir = normalize(v); }
                camPos = wi->pos + Vec3{dir.y, -dir.x, 0} * 22.f + Vec3{0, 0, 6};
            }
            view = Mat4::lookAt(camPos, wi->pos, {0, 0, 1});
        } else if (inside && std::getenv("XA_LOOKAT")) {      // testing: XA_LOOKAT=x,y,z,tx,ty,tz in the helicopter frame
            Mat4 hm = Mat4::translate(heli.pos) * heli.rotation();
            float a[6] = {0, 1.2f, 0.85f, -0.587f, 2.7f, 0};
            std::sscanf(std::getenv("XA_LOOKAT"), "%f,%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
            camPos = hm.transformPoint({a[0], a[1], a[2]});
            view = Mat4::lookAt(camPos, hm.transformPoint({a[3], a[4], a[5]}), heli.rotation().transformPoint({0, 0, 1}));
        } else if (inside) {
            Mat4 hr = heli.rotation();
            camPos = (Mat4::translate(heli.pos) * hr).transformPoint({0, 1.2f, 0.85f});
            // 0x66c31: the camera matrix adds the lead angles to the helicopter's (the cockpit model does not)
            hr = hr * Mat4::rotZ(heli.cockpitYaw()) * Mat4::rotX(heli.cockpitPitch());
            Vec3 dir = hr.transformPoint({0, std::cos(aimPitch * 0.5f), std::sin(aimPitch * 0.5f)});
            Vec3 up = hr.transformPoint({0, 0, 1});
            view = Mat4::lookAt(camPos, camPos + dir, up);
        } else if (letterbox) {                               // 0x67fbd: cutscene camera, always aimed at the helicopter
            camPos = cutCam;
            view = Mat4::lookAt(cutCam, heli.pos, {0, 0, 1});
        } else if (flying || briefing.active()) {
            camPos = chaseEye;
            view = Mat4::lookAt(chaseEye, heli.pos + Vec3{0, 0, 4.f} + heli.forwardDir() * (10.f * std::max(0.f, std::cos(fixedYaw + orbYaw))), {0, 0, 1});
        } else {
            Vec3 fwd{std::cos(yaw * d2r) * std::cos(pitch * d2r), std::sin(yaw * d2r) * std::cos(pitch * d2r), std::sin(pitch * d2r)};
            camPos = eye;
            view = Mat4::lookAt(eye, eye + fwd, {0, 0, 1});
        }
        float t = opt.time >= 0 ? opt.time : simTime;
        if (playerInst) playerInst->hidden = inside || (deathTimer > 0 && !crashing);
        std::vector<Renderer::Extra> extras = combat.extras;
        extras.insert(extras.end(), enemies.extras.begin(), enemies.extras.end());
        if (inside) extras.push_back({cockpitModel, typeIndexByName("ADV_COCKPIT"), heli.pos, heli.rotation()});
        r.draw(proj, view, camPos, w, h, t, &extras, &combat.sprites);
        audio.setListener(camPos, {view.m[0], view.m[4], view.m[8]});
        enemies.sounds(audio, camPos, dt, !askQuit && !endScreen);
        std::vector<Renderer::HudQuad> hud;
        text.begin(w, h);
        if (briefing.active()) {
            const uint8_t white[4] = {230, 240, 255, 255}, shade[4] = {0, 0, 0, 150};
            if (monitorView) {
                // texts and target marks on the monitor; the scripts use 1024x768 positions of the 4:3 picture
                const float sc = float(h) / 480.f, ox = float(w) * 0.5f - 320.f * sc;
                std::vector<Renderer::HudQuad> mon;
                text.sx = text.sy = sc;
                for (const auto& t : briefing.texts) {
                    if (!briefing.showText && !t.always && t.voiced) continue;   // BRIEFING 2: voice only (texts without a voice stay)
                    std::string full = missionText.get(t.id);
                    std::string shown = full.substr(0, size_t(std::clamp(t.typed, 0.f, float(full.size()))));
                    float maxW = float(t.widthChars) * 9.f;
                    auto lines = text.wrap(text.small, full, maxW);
                    float y = t.y * 480.f / 768.f, x = t.x * 640.f / 1024.f;
                    text.box(mon, x - 8, y - 6, x + maxW + 8, y + float(lines.size()) * 17.f + 6, shade);
                    size_t used = 0;
                    for (const auto& ln : lines) {
                        if (used >= shown.size()) break;
                        std::string part = ln.substr(0, std::min(ln.size(), shown.size() - used));
                        text.draw(mon, text.small, part, x, y, white);
                        used += ln.size() + 1; y += 17.f;
                    }
                }
                for (auto& q : mon) { q.x0 += ox; q.x1 += ox; }
                // selected targets: corner brackets
                Mat4 vpm = proj * view;
                for (const auto& n : briefing.selected) {
                    Vec3 p; float rad;
                    if (!briefing.findObject(n, p, rad)) continue;
                    float cx = vpm.m[0] * p.x + vpm.m[4] * p.y + vpm.m[8] * p.z + vpm.m[12];
                    float cy = vpm.m[1] * p.x + vpm.m[5] * p.y + vpm.m[9] * p.z + vpm.m[13];
                    float cw = vpm.m[3] * p.x + vpm.m[7] * p.y + vpm.m[11] * p.z + vpm.m[15];
                    if (cw <= 0) continue;
                    float sx = monX0 + (cx / cw * 0.5f + 0.5f) * (monX1 - monX0), sy = monY0 + (0.5f - cy / cw * 0.5f) * (monY1 - monY0);
                    float sz = std::clamp(rad / cw * (monY1 - monY0) * 0.9f, 12.f, 200.f);
                    const uint8_t red[4] = {255, 60, 40, 230};
                    auto q = [&](float a, float b, float c, float d) { hud.push_back({a, b, c, d, {red[0], red[1], red[2], red[3]}}); };
                    q(sx - sz, sy - sz, sx - sz + 10, sy - sz + 2); q(sx - sz, sy - sz, sx - sz + 2, sy - sz + 10);
                    q(sx + sz - 10, sy - sz, sx + sz, sy - sz + 2); q(sx + sz - 2, sy - sz, sx + sz, sy - sz + 10);
                    q(sx - sz, sy + sz - 2, sx - sz + 10, sy + sz); q(sx - sz, sy + sz - 10, sx - sz + 2, sy + sz);
                    q(sx + sz - 10, sy + sz - 2, sx + sz, sy + sz); q(sx + sz - 2, sy + sz - 10, sx + sz, sy + sz);
                }
                if (briefing.distort > 0) {   // DISTORT (0x578e4): random noise over the monitor picture
                    static unsigned nr = 12345u;
                    const int cs = 4;
                    for (int y = int(monY0); y < int(monY1); y += cs)
                        for (int x = int(monX0); x < int(monX1); x += cs) {
                            nr = nr * 1103515245u + 12345u; uint8_t g = uint8_t((nr >> 16) & 0xff);
                            hud.push_back({float(x), float(y), float(x + cs), float(y + cs), {g, g, g, 200}});
                        }
                }
                hud2d.briefingFrame(hud, w, h);
                hud.insert(hud.end(), mon.begin(), mon.end());
                text.begin(w, h);
            }
            const uint8_t hint[4] = {200, 200, 200, 200};
            if (monitorView) {          // inside the monitor, under the picture
                const float sc = float(h) / 480.f, ox = float(w) * 0.5f - 320.f * sc;
                std::vector<Renderer::HudQuad> hq; text.sx = text.sy = sc;
                text.draw(hq, text.small, lang::tr("SPACE / ESC: SKIP BRIEFING"), Hud::kMonX1 - text.width(text.small, lang::tr("SPACE / ESC: SKIP BRIEFING")) - 8.f, Hud::kMonY1 - 16.f, hint);
                for (auto& q : hq) { q.x0 += ox; q.x1 += ox; }
                hud.insert(hud.end(), hq.begin(), hq.end());
                text.begin(w, h);
            } else
                text.draw(hud, text.small, lang::tr("SPACE / ESC: SKIP BRIEFING"), 640.f - text.width(text.small, lang::tr("SPACE / ESC: SKIP BRIEFING")) - 12.f, 452.f, hint);
            if (briefing.fade > 0) hud.push_back({0, 0, float(w), float(h), {0, 0, 0, uint8_t(255 * briefing.fade)}});
        } else if (flying) {
            // Original cockpit HUD (GLOBAL\COCKPIT): panels, shield bar, counters, radar, target marks.
            Mat4 vpm = proj * view;
            auto project = [&](Vec3 p, float& sx, float& sy) {
                float cx = vpm.m[0] * p.x + vpm.m[4] * p.y + vpm.m[8] * p.z + vpm.m[12];
                float cy = vpm.m[1] * p.x + vpm.m[5] * p.y + vpm.m[9] * p.z + vpm.m[13];
                float cw = vpm.m[3] * p.x + vpm.m[7] * p.y + vpm.m[11] * p.z + vpm.m[15];
                if (cw <= 0.01f) return false;
                sx = (cx / cw * 0.5f + 0.5f) * float(w); sy = (0.5f - cy / cw * 0.5f) * float(h);
                return sx > -50 && sy > -50 && sx < float(w) + 50 && sy < float(h) + 50;
            };
            Hud::State hs;
            const Vec3 camRight{view.m[0], view.m[4], view.m[8]};
            auto markRadius = [&](Vec3 p, int wi, float sx, float sy) {   // 0x3c3a8: projected radius / 3 (640x480 pixels)
                const Renderer::Instance* in = r.instanceForWorldObject(wi);
                float rad = in ? in->radius : 3.f, ex, ey;
                if (!project(p + camRight * rad, ex, ey)) return 6.f;
                return std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy)) / 3.f * 480.f / float(h);
            };
            auto energyOf = [&](int wi) {
                const Combat::Target* t = combat.target(wi);
                return t && t->maxHp > 0 ? t->hp / t->maxHp : 1.f;
            };
            hs.shield = combat.arms.shield / Combat::kMaxShield;
            hs.shieldBlink = combat.arms.immortal > 0;
            hs.rockets = combat.arms.rockets;
            hs.weapon = combat.arms.sel;
            for (int wq = 0; wq < 3; wq++) { hs.ammo[wq] = combat.arms.ammo[wq]; hs.level[wq] = combat.arms.level[wq]; }
            hs.weapon3 = combat.arms.weapon3;
            hs.smartbombs = combat.arms.smartbombs; hs.thunderbolts = combat.arms.thunderbolts; hs.magma = int(combat.arms.magma);
            hs.score = combat.score;
            if (timerShown) hs.timeLeft = int(missionTimer);
            Vec3 aimDir = Mat4::rotZ(heli.yaw).transformPoint({0, std::cos(aimPitch), std::sin(aimPitch)});
            hs.aimValid = project(heli.pos + aimDir * 180.f, hs.aimX, hs.aimY);
            std::vector<std::pair<Vec3, Vec3>> contacts;
            std::vector<int> contactIds;
            enemies.contacts(contacts, &contactIds);
            std::vector<int> markIds;
            struct LockCand { int id; float scr2, dist; };
            std::vector<LockCand> lockCands;
            const float lockScrR = float(h) * 128.f / 480.f;                     // 0x253604: 128 pixels at 640x480
            const float sightR = r.fogFar > 0 ? r.fogFar : 700.f;               // 0x253620: sight range of the vehicle
            visibleTargets.clear();
            float bestD = 1e30f; int best = -1;
            const float radarRange = 500.f;
            Vec3 fwd2{-std::sin(heli.yaw), std::cos(heli.yaw), 0}, right2{std::cos(heli.yaw), std::sin(heli.yaw), 0};
            for (size_t i = 0; i < contacts.size(); i++) {
                Vec3 d = contacts[i].first - heli.pos;
                float rx = dot(d, right2) / radarRange, ry = dot(d, fwd2) / radarRange, l = std::sqrt(rx * rx + ry * ry);
                if (l > 1.f) { rx /= l; ry /= l; }
                hs.radar.push_back({rx, ry, true});
                float dist = std::sqrt(dot(d, d)), sx, sy;
                bool seen = true;                                       // no marks through hills
                if (dist < 700.f) {
                    Vec3 hit; int owner = -1;
                    Vec3 tp = contacts[i].first + Vec3{0, 0, 1.5f};
                    if (coll.raycast(heli.pos, tp, hit, owner) && owner != contactIds[i]) {
                        Vec3 hd = hit - heli.pos;
                        seen = std::sqrt(dot(hd, hd)) > dist - 6.f;
                    }
                }
                if (dist < 700.f && seen && project(contacts[i].first, sx, sy)) {
                    hs.marks.push_back({sx, sy, false, markRadius(contacts[i].first, contactIds[i], sx, sy), Hud::MEnemy, energyOf(contactIds[i])});
                    markIds.push_back(contactIds[i]);
                    visibleTargets.push_back(contactIds[i]);
                }
            }
            {
                std::vector<Vec3> hos; combat.hostages(hos);
                for (const Vec3& hp : hos) {
                    Vec3 d = hp - heli.pos;
                    float rx = dot(d, right2) / radarRange, ry = dot(d, fwd2) / radarRange, l = std::sqrt(rx * rx + ry * ry);
                    if (l > 1.f) { rx /= l; ry /= l; }
                    hs.radar.push_back({rx, ry, false, true});
                }
            }
            {   // ADV_FRIEND units: blue circle, TARGETF.DAT and a blue energy bar (never locked)
                std::vector<Vec3> fr; std::vector<int> fid; enemies.friends(fr, fid);
                for (size_t i = 0; i < fr.size(); i++) {
                    Vec3 d = fr[i] - heli.pos; float sx, sy;
                    if (dot(d, d) < 700.f * 700.f && project(fr[i], sx, sy))
                        hs.marks.push_back({sx, sy, false, markRadius(fr[i], fid[i], sx, sy), Hud::MFriend, energyOf(fid[i])});
                }
            }
            hs.radar.push_back({0, 0, false});
            // Target selection (0x3b09e): the locked target stays until a key changes it.
            //  - destroyed (0x72a8a): cleared, and No. 2 runs; outside the sight box: No. 1 runs
            //  - No. 1 (0x3b16b): the candidate nearest to the screen centre, not the current one
            //  - No. 2 (0x3b265): the nearest candidate farther away than the current one, else the nearest; a
            //    press more than 40 ticks after the last one, or after No. 1, starts again from the nearest
            if (lockedTarget >= 0) {
                const Combat::Target* tg = combat.target(lockedTarget);
                const Renderer::Instance* ti = r.instanceForWorldObject(lockedTarget);
                if (!tg || !tg->alive || !tg->active || !ti) { lockedTarget = -1; lockReq2 = true; }
                else {
                    Vec3 d = ti->pos - heli.pos;
                    if (std::fabs(d.x) > sightR || std::fabs(d.y) > sightR || std::fabs(d.z) > sightR) lockReq1 = true;
                }
            }
            if (opt.autoAim && lockedTarget < 0) lockReq1 = true;               // testing: --autoaim keeps a target locked
            // 0x3ad1b (only built for a request): every object of the mission's enemy set (the runtime object
            // list 0x1d42d8, 0x21c57c objects; landscape sets are never in it) with energy left, inside the sight
            // box (per axis), on the screen within 128 pixels of its centre, not hidden (depth test with
            // radius * 1.25), not SHIELD_ENEMY and not UNSELECTIBLE; its distance is kept for stepping (0x20dbc8)
            if (lockReq1 || lockReq2) {
                const auto& wobjs = world.objects();
                for (size_t i = 0; i < wobjs.size(); i++) {
                    if (int(i) == playerWorld || isTemplateSet(wobjs[i].set) || !activeSets.count(upperS(wobjs[i].set))) continue;
                    const ObjectType* ot = types.get(wobjs[i].objClass);
                    if (!ot || ot->unselectible || ot->behavior == "SHIELD_ENEMY") continue;
                    const Renderer::Instance* in = r.instanceForWorldObject(int(i));
                    if (!in || in->hidden) continue;
                    const Combat::Target* tg = combat.target(int(i));
                    if (tg && (!tg->alive || !tg->active)) continue;
                    Vec3 d = in->pos - heli.pos;
                    if (std::fabs(d.x) >= sightR || std::fabs(d.y) >= sightR || std::fabs(d.z) >= sightR) continue;
                    float sx, sy;
                    if (!project(in->pos, sx, sy)) continue;
                    float cx = sx - float(w) / 2, cy = sy - float(h) / 2;
                    if (cx * cx + cy * cy >= lockScrR * lockScrR) continue;
                    float dist = std::sqrt(dot(d, d));
                    Vec3 hit; int owner = -1;
                    if (coll.raycast(heli.pos, in->pos, hit, owner) && owner != int(i)) {
                        Vec3 hd = hit - heli.pos;
                        if (std::sqrt(dot(hd, hd)) < dist - in->radius * 1.25f) continue;
                    }
                    lockCands.push_back({int(i), cx * cx + cy * cy, dist});
                }
            }
            const float nowTick = simTime * 60.f;
            if (lockReq2) { if (lockCycleTick >= 0 && nowTick - lockCycleTick > 40.f) lockCycling = false; lockCycleTick = nowTick; }
            if (lockReq1) {
                if (lockCycling) lockedTarget = -1;
                lockCycling = false;
                int pick = -1; float bd = 1e30f;
                for (const auto& lc : lockCands) if (lc.scr2 < bd && lc.id != lockedTarget) { bd = lc.scr2; pick = lc.id; }
                lockedTarget = pick;
            }
            if (lockReq2) {
                if (!lockCycling) lockedTarget = -1;
                lockCycling = true;
                float d0 = -1;
                for (const auto& lc : lockCands) if (lc.id == lockedTarget) d0 = lc.dist;
                int pick = -1; float bd = 1e30f;
                if (d0 >= 0) for (const auto& lc : lockCands) if (lc.dist > d0 && lc.dist - d0 < bd) { bd = lc.dist - d0; pick = lc.id; }
                if (pick < 0) { bd = 1e30f; for (const auto& lc : lockCands) if (lc.dist < bd) { bd = lc.dist; pick = lc.id; } }
                lockedTarget = pick;
            }
            // 0x3b5f1: a request that finds nothing shows NO TARGET AVAILABLE (INGAME 27) for 60 ticks
            if ((lockReq1 || lockReq2) && lockedTarget < 0) pickupMsg = {ingameText.get(27), 1.f, false};
            if ((lockReq1 || lockReq2) && std::getenv("XA_DEBUG")) {
                const ObjectType* lt = lockedTarget >= 0 ? types.get(world.objects()[size_t(lockedTarget)].objClass) : nullptr;
                std::printf("lock %s: %zu candidates -> %d %s (", lockReq1 ? "No.1" : "No.2", lockCands.size(), lockedTarget, lt ? lt->name.c_str() : "-");
                for (const auto& lc : lockCands) { const ObjectType* ct = types.get(world.objects()[size_t(lc.id)].objClass); std::printf(" %s", ct ? ct->name.c_str() : "?"); }
                std::printf(" )\n");
            }
            lockReq1 = lockReq2 = false;
            best = -1;
            for (size_t m = 0; m < markIds.size(); m++) if (markIds[m] == lockedTarget) best = int(m);
            if (best < 0 && lockedTarget >= 0)      // a locked object without an enemy mark (building, extra) gets one
                if (const Renderer::Instance* ti = r.instanceForWorldObject(lockedTarget); ti && !ti->hidden) {
                    float sx, sy;
                    if (project(ti->pos, sx, sy)) {
                        hs.marks.push_back({sx, sy, false, markRadius(ti->pos, lockedTarget, sx, sy), Hud::MEnemy, energyOf(lockedTarget)});
                        markIds.push_back(lockedTarget);
                        best = int(hs.marks.size()) - 1;
                    }
                }
            if (best >= 0) {
                hs.marks[size_t(best)].locked = true;
                // lead point for the cannon (projectile speed 600 units/s)
                for (const auto& c : contacts) {
                    float sx, sy;
                    if (!project(c.first, sx, sy) || std::fabs(sx - hs.marks[size_t(best)].x) > 0.5f || std::fabs(sy - hs.marks[size_t(best)].y) > 0.5f) continue;
                    Vec3 d = c.first - heli.pos;
                    float tFly = std::sqrt(dot(d, d)) / 600.f;
                    hs.leadValid = project(c.first + (c.second - heli.vel) * tFly, hs.leadX, hs.leadY);
                    // 0x3bcb8: the lead point follows with 1/10 of the distance per tick
                    if (hs.leadValid) {
                        if (leadSmooth.x > 1e8f) leadSmooth = {hs.leadX, hs.leadY, 0};
                        float k = std::min(1.f, dt * 6.f);
                        leadSmooth.x += (hs.leadX - leadSmooth.x) * k; leadSmooth.y += (hs.leadY - leadSmooth.y) * k;
                        hs.leadX = leadSmooth.x; hs.leadY = leadSmooth.y;
                    }
                    break;
                }
            }
            if (!hs.leadValid) leadSmooth = {1e9f, 0, 0};
            hud2d.build(hud, hs, w, h);
            const uint8_t yellow[4] = {255, 220, 90, 240};
            // 0x41d78: the ticker runs right to left at 1 pixel per tick in the strip x 6..633, y 459 (17 high) and
            // starts again once the whole text has left the strip
            if (!tickerText.empty()) {
                if (tickerStart < 0) tickerStart = simTime;   // (no simulation step yet)
                const float tw = text.width(text.small, tickerText);
                float el = (simTime - tickerStart) * 60.f;
                if (el > tw + 627.f) { tickerStart = simTime; el = 0; }
                const uint8_t tick[4] = {200, 240, 210, 255};
                text.drawClipped(hud, text.small, tickerText, 6.f + 627.f - el, 460.f, tick, 6.f, 633.f);
            }
            if (combat.incomingMissile() && int(simTime * 4) % 2 == 0) { const std::string im = ingameText.get(13); const uint8_t red2[4] = {255, 80, 60, 255}; text.draw(hud, text.big, im, 320.f - text.width(text.big, im) / 2, 120.f, red2); }
            if (outsideTicks > 180.f) {
                std::string wmsg = ingameText.get(0) + "  " + std::to_string(int((780.f - outsideTicks) / 60.f) + 1);
                const uint8_t red[4] = {255, 80, 60, 255};
                text.draw(hud, text.big, wmsg, 320.f - text.width(text.big, wmsg) / 2, 150.f, red);
            }
            if (askQuit) { const std::string q = ingameText.get(4); text.draw(hud, text.big, q, 320.f - text.width(text.big, q) / 2, 240.f, yellow); }
            if (pickupMsg.time > 0) text.draw(hud, text.small, pickupMsg.text, 320.f - text.width(text.small, pickupMsg.text) / 2, 354.f, yellow);
            if (banner.time > 0) text.draw(hud, text.big, banner.text, 320.f - text.width(text.big, banner.text) / 2, 200.f, yellow);
            if (bonusLeft > 0 && !endScreen) {
                char bb[64]; std::snprintf(bb, sizeof bb, ingameText.get(82).c_str(), long(bonusLeft) / 60, long(bonusLeft) % 60);
                text.draw(hud, text.big, bb, 320.f - text.width(text.big, bb) / 2, 235.f, yellow);
            }
            if (deathTimer > 0 && !crashing) text.draw(hud, text.big, lang::tr("SIOUX DESTROYED"), 320.f - text.width(text.big, lang::tr("SIOUX DESTROYED")) / 2, 230.f, yellow);
        }
        if (endScreen) {
            // level-end screen (0x354b4): a box opens in the middle, then the operation summary
            // INGAME.CHR 9 NEXT OPERATION AREA REACHED, 80 TIME, 81 SCORE, 7 PRESS F12 FOR NEXT MISSION
            float k = std::min(1.f, endT / 0.8f);
            float bw = 460.f * k, bh = 170.f * k, sx = float(w) / 640.f, sy = float(h) / 480.f;
            hud.push_back({(320.f - bw / 2) * sx, (240.f - bh / 2) * sy, (320.f + bw / 2) * sx, (240.f + bh / 2) * sy, {0, 0, 0, 200}});
            const uint8_t col[4] = {255, 220, 90, 255};
            if (k >= 1.f) {
                char buf[96];
                auto fmt = [&](int id, long a, long b) { std::string f = ingameText.get(id); std::snprintf(buf, sizeof buf, f.c_str(), a, b); return std::string(buf); };
                long mins = long(playTicks / 3600.f), secs = long(playTicks / 60.f) % 60;
                std::string l1 = ingameText.get(9), l2 = fmt(80, mins, secs), l3 = fmt(81, long(combat.score), 0), l4 = ingameText.get(7);
                text.draw(hud, text.big, l1, 320.f - text.width(text.big, l1) / 2, 175.f, col);
                text.draw(hud, text.big, l2, 320.f - text.width(text.big, l2) / 2, 215.f, col);
                text.draw(hud, text.big, l3, 320.f - text.width(text.big, l3) / 2, 245.f, col);
                if (bonusLeft > 0) { std::snprintf(buf, sizeof buf, ingameText.get(82).c_str(), long(bonusLeft) / 60, long(bonusLeft) % 60); text.draw(hud, text.small, buf, 320.f - text.width(text.small, buf) / 2, 272.f, col); }
                if (int(endT * 2) % 2 == 0) text.draw(hud, text.small, l4, 320.f - text.width(text.small, l4) / 2, 290.f, col);
            }
        }
        if (!hud.empty()) r.drawHud(hud, w, h);
        banner.time -= dt; goalBanner.time -= dt; pickupMsg.time -= dt;

        titleTimer += dt;
        if (titleTimer > 0.25f && flying) {
            titleTimer = 0;
            char title[160];
            float g = coll.groundBelow(heli.pos + Vec3{0, 0, 1});
            char tl[32] = "";
            if (timerShown) { int left = int(missionTimer); std::snprintf(tl, sizeof tl, "  time %d:%02d", left / 60, left % 60); }
            std::snprintf(title, sizeof title, "OpenAssault - %s %s  enemies %d  shield %.0f%%  score %d  rockets %d%s  alt %.0f",
                          mission.file.c_str(), missionStatus.c_str(), enemies.activeCount(), combat.arms.shield / Combat::kMaxShield * 100.f,
                          combat.score, combat.arms.rockets, tl, g > -1e8f ? heli.pos.z - heli.clearance - g : 0.f);
            SDL_SetWindowTitle(win, title);
        }

        if (takeShot || (!opt.shot.empty() && !(endScreen && endT < 1.5f))) {
            std::vector<uint8_t> px(size_t(w) * h * 4);
            gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
            gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
            for (int y = 0; y < h; y++) std::memcpy((uint8_t*)s->pixels + y * s->pitch, &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
            std::string name = opt.shot.empty() ? "openassault_" + std::to_string(shotIndex++) + ".bmp" : opt.shot;
            SDL_SaveBMP(s, name.c_str()); SDL_FreeSurface(s);
            if (flying) std::printf("Saved %s (heli %.1f,%.1f,%.1f yaw %.2f landed %d score %d kills %d rockets %d shield %.0f enemies %d %s)\n", name.c_str(), heli.pos.x, heli.pos.y, heli.pos.z, heli.yaw, int(heli.landed), combat.score, combat.kills, combat.arms.rockets, combat.arms.shield, enemies.activeCount(), mission.file.c_str());
            else std::printf("Saved %s (camera %.1f,%.1f,%.1f,%.1f,%.1f)\n", name.c_str(), eye.x, eye.y, eye.z, yaw, pitch);
            if (!opt.shot.empty() || quitAfterShot) running = false;
        }
        SDL_GL_SwapWindow(win);
    }
    audio.stopMusic();
    if (rotorVoice) audio.stop(rotorVoice);
    enemies.sounds(audio, {}, 0, false);
    if (distortVoice) audio.stop(distortVoice);
    if (speechVoice) audio.stop(speechVoice);
    combat.setGunSound(false);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    opt.lastScore = combat.score + int(bonusLeft * float(combat.scorePercent));
    return exitCode;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 1;
    char lv[32]; std::snprintf(lv, sizeof lv, "DATA\\LEVEL%02d", opt.level);
    // Default: search the working directory and the executable's folder plus its parents.
    if (opt.game == "." && !GameFS(".").resolve(lv)) {
        if (char* base = SDL_GetBasePath()) {
            std::string dir = base; SDL_free(base);
            for (int up = 0; up < 4; up++) {
                if (GameFS(dir).resolve(lv)) { opt.game = dir; break; }
                while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
                size_t cut = dir.find_last_of("/\\");
                if (cut == std::string::npos) break;
                dir = dir.substr(0, cut + 1);
            }
        }
    }
    GameFS fs(opt.game);
    if (!fs.resolve(lv)) return fatal("Game data not found: %s\\%s\nPut openassault.exe into the game folder or use --game <folder>.", opt.game.c_str(), lv);

    if (const char* dd = std::getenv("XA_DUMPDAT")) {          // testing: XA_DUMPDAT=DATA\MENU\GFX\X.DAT -> /tmp/dump.tga
        Font f; if (f.load(fs, dd)) { writeTga("/tmp/dump.tga", f.texW, f.texH, f.rgba); std::printf("%dx%d\n", f.texW, f.texH); }
        return 0;
    }
    if (!opt.exportLang.empty()) {
        std::string exeDir = ".";
        if (char* base = SDL_GetBasePath()) { exeDir = base; SDL_free(base); lang::init(exeDir); }
        std::string msg;
        bool ok = lang::exportTemplate(opt.exportLang, exeDir, [&](const std::string& rel, std::string& text) {
            std::string p = rel;
            for (auto& c : p) if (c == '/') c = '\\';
            std::vector<uint8_t> raw;
            if (!fs.read(p, raw)) return false;
            text = decodeText(raw);
            return true;
        }, msg);
        std::printf("%s\n", msg.c_str());
        if (!std::getenv("XA_NOBOX"))                               // testing: XA_NOBOX=1 skips the message box
            SDL_ShowSimpleMessageBox(ok ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR, "OpenAssault", msg.c_str(), nullptr);
        return ok ? 0 : 1;
    }
    if (!opt.extract.empty()) {
        // every 256x256 page of the six levels as <hash>.png (the HDTEX name; pages shared by levels once),
        // transparent where the colour key is, plus INDEX.TXT: hash, levels, original page names
        std::error_code ec;
        std::filesystem::create_directories(opt.extract, ec);
        std::map<std::string, std::pair<std::string, std::string>> index;   // hash -> levels, names
        std::vector<uint8_t> rgba;
        int written = 0;
        for (int l = 1; l <= 6; l++) {
            char dir[32]; std::snprintf(dir, sizeof dir, "DATA\\LEVEL%02d", l);
            TextureLib tex;
            if (!tex.load(fs, dir)) continue;
            for (const auto& p : tex.pages()) {
                if (p.rgb565.empty() || p.width != 256 || p.height != 256) continue;
                std::string h = pageFileName(p);
                auto& e = index[h];
                if (e.first.empty()) {
                    rgb565ToRgba(p.rgb565, rgba, true);
                    written += writePng(opt.extract + "/" + h + ".png", p.width, p.height, rgba);
                }
                std::string ls = std::to_string(l);
                if (e.first.find(ls) == std::string::npos) e.first += (e.first.empty() ? "" : ",") + ls;
                if ((" " + e.second + " ").find(" " + p.name + " ") == std::string::npos) e.second += (e.second.empty() ? "" : " ") + p.name;
            }
        }
        if (FILE* f = std::fopen((opt.extract + "/INDEX.TXT").c_str(), "w")) {
            std::fprintf(f, "# OpenAssault textures: <hash>.png  levels  original page names\n"
                            "# Edit a picture (any size, alpha = transparency) and put it into HDTEX\\ in the game folder\n"
                            "# under the same name (.png .jpg .tga .bmp; a .png wins over the pack's .jpg). It is used in\n"
                            "# every level that has the page, only while Options > HD textures is on.\n");
            for (const auto& [h, e] : index) std::fprintf(f, "%s  %s  %s\n", h.c_str(), e.first.c_str(), e.second.c_str());
            std::fclose(f);
        }
        char msg[512];
        std::snprintf(msg, sizeof msg, "%d textures written to %s (see INDEX.TXT).\nEdited or upscaled copies go into HDTEX\\ (used with HD textures on).", written, opt.extract.c_str());
        std::printf("%s\n", msg);
        if (!std::getenv("XA_NOBOX")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "OpenAssault", msg, nullptr);
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return fatal("SDL init failed: %s", SDL_GetError());
    pad::init();
    { Settings st = profile::loadSettings(); pad::setRumbleScale(st.rumble * 0.5f); }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    opt.settings = profile::loadSettings();
    if (char* base = SDL_GetBasePath()) { lang::init(base); SDL_free(base); }
    if (const char* l = std::getenv("XA_LANG")) opt.settings.language = l;     // testing: XA_LANG=EN / HU
    lang::set(opt.settings.language);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (!opt.sizeGiven) { opt.width = opt.settings.windowW; opt.height = opt.settings.windowH; }
    if (opt.fullscreen) opt.settings.fullscreen = true;
    if (opt.settings.fullscreen && opt.shot.empty()) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!opt.shot.empty()) flags |= SDL_WINDOW_HIDDEN;
    SDL_Window* win = SDL_CreateWindow("OpenAssault", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width, opt.height, flags);
    if (!win) return fatal("Could not create window: %s", SDL_GetError());
    if (opt.settings.fullscreen && opt.settings.fullW > 0 && opt.shot.empty()) applyVideo(win, opt.settings);   // own fullscreen resolution
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) return fatal("OpenGL 3.3 context failed: %s\nUpdate the graphics driver.", SDL_GetError());
    SDL_GL_SetSwapInterval(1);
    if (!gl::load(SDL_GL_GetProcAddress)) return fatal("Failed to load OpenGL 3.3 functions");

    Audio audio;
    if (!opt.mute && (opt.shot.empty() || std::getenv("XA_AUDIOTEST"))) audio.init(fs);
    bool nextLevel = false;
    SaveGame loaded;
    opt.difficulty = opt.menu ? opt.settings.pilot : opt.difficulty;
    if (std::getenv("XA_SAVETEST")) opt.saving = true;               // testing: save in --level runs too
    if (const char* ls = std::getenv("XA_LOADSAVE")) {                 // testing: XA_LOADSAVE=pilot,level,mission
        int p = 0, l = 1, m = 1;
        if (std::sscanf(ls, "%d,%d,%d", &p, &l, &m) == 3 && profile::read(p, l, m, loaded)) {
            opt.difficulty = p; opt.level = l; opt.mission = loaded.missionFile; opt.carryScore = loaded.score; opt.carryTicks = loaded.playTicks;
            opt.carryArms = loaded.arms; opt.haveArms = true; if (loaded.hasWorld) opt.load = &loaded;
        }
    }
    for (;;) {
        if (opt.menu && !nextLevel) {
            opt.load = nullptr;
            if (!runMenu(opt, fs, win, audio, loaded)) break;  // sets level/mission/pilot, arms and score (new or loaded game)
            opt.saving = true;
        }
        nextLevel = false;
        int rc = runGame(opt, fs, win, audio);
        opt.load = nullptr;
        opt.played = true;
        if (rc == RunQuit) break;
        if (rc == RunNextLevel) {
            // the pilot (difficulty) decides how many operation areas can be played (0x35908)
            if (opt.level < 6 && (!opt.menu || opt.level < profile::areaLimit(opt.difficulty))) {
                opt.level++; opt.mission = "MISS_01.SCR"; nextLevel = true;
                if (opt.saving) {                   // 0x5a44c: the next area is saved as mission 1
                    SaveGame sv; sv.pilot = opt.difficulty; sv.level = opt.level; sv.mission = 1; sv.missionFile = opt.mission;
                    sv.score = opt.carryScore; sv.playTicks = opt.carryTicks; sv.arms = opt.carryArms;
                    profile::write(sv);
                }
                continue;
            }
            if (!opt.menu) break;
            continue;
        }
        if (!opt.menu) break;                       // RunMenu without a menu (test runs)
    }
    audio.shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
