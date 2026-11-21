#include "profile.h"
#include "pad.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace xa {
namespace fs = std::filesystem;

void defaultControls(int k[CControlCount][2]) {
    const int d[CControlCount][2] = {
        {SDL_SCANCODE_W, SDL_SCANCODE_UP}, {SDL_SCANCODE_S, SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT, 0}, {SDL_SCANCODE_RIGHT, 0},
        {SDL_SCANCODE_SPACE, SDL_SCANCODE_E}, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Q}, {SDL_SCANCODE_A, 0}, {SDL_SCANCODE_D, 0},
        {kMouse + SDL_BUTTON_LEFT, SDL_SCANCODE_F}, {SDL_SCANCODE_TAB, 0}, {kMouse + SDL_BUTTON_RIGHT, SDL_SCANCODE_R},
        {SDL_SCANCODE_B, 0}, {SDL_SCANCODE_T, 0}, {SDL_SCANCODE_F2, SDL_SCANCODE_V},
        {SDL_SCANCODE_G, kMouse + SDL_BUTTON_MIDDLE}, {SDL_SCANCODE_H, 0}};   // Lock Target No. 1 / No. 2
    for (int i = 0; i < CControlCount; i++) { k[i][0] = d[i][0]; k[i][1] = d[i][1]; }
}

void defaultPad(int p[CControlCount]) {
    using namespace pad;
    const int d[CControlCount] = {
        kPad + SDL_CONTROLLER_BUTTON_DPAD_UP, kPad + SDL_CONTROLLER_BUTTON_DPAD_DOWN, kPad + SDL_CONTROLLER_BUTTON_DPAD_LEFT, kPad + SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        kPad + SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, kPad + SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0, 0,
        kPad + kRT, kPad + SDL_CONTROLLER_BUTTON_Y, kPad + kLT,
        kPad + SDL_CONTROLLER_BUTTON_B, kPad + SDL_CONTROLLER_BUTTON_X, kPad + SDL_CONTROLLER_BUTTON_BACK,
        kPad + SDL_CONTROLLER_BUTTON_RIGHTSTICK, kPad + SDL_CONTROLLER_BUTTON_LEFTSTICK};
    for (int i = 0; i < CControlCount; i++) p[i] = d[i];
}

void defaultSticks(int s[SCount]) {   // left stick: forward / back + drift, right stick: height + turn
    s[SForward] = SDL_CONTROLLER_AXIS_LEFTY * 2; s[STurn] = SDL_CONTROLLER_AXIS_RIGHTX * 2;
    s[SDrift] = SDL_CONTROLLER_AXIS_LEFTX * 2; s[SHeight] = SDL_CONTROLLER_AXIS_RIGHTY * 2;
}

namespace profile {

std::string folder() {
    static std::string dir;
    if (dir.empty()) {
        if (const char* e = std::getenv("XA_SAVEDIR")) dir = e;
        else if (char* b = SDL_GetBasePath()) { dir = std::string(b) + "SAVEGAME"; SDL_free(b); }
        else dir = "SAVEGAME";
        std::error_code ec; fs::create_directories(dir, ec);
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    }
    return dir;
}

static std::string fileName(int p, int l, int m) {
    char n[32]; std::snprintf(n, sizeof n, "P%01dL%02dM%02d.SAV", p, l, m);
    return folder() + n;
}

bool write(const SaveGame& s) {
    std::ofstream f(fileName(s.pilot, s.level, s.mission), std::ios::trunc);
    if (!f) return false;
    f << "OPENASSAULT_SAVE 1\n";
    f << "pilot " << s.pilot << "\nlevel " << s.level << "\nmission " << s.mission << "\nfile " << s.missionFile << "\n";
    f << "score " << s.score << "\nticks " << s.playTicks << "\n";
    const SaveArms& a = s.arms;
    f << "arms " << a.sel << ' ' << a.ammo[0] << ' ' << a.ammo[1] << ' ' << a.ammo[2] << ' ' << a.level[0] << ' ' << a.level[1] << ' ' << a.level[2]
      << ' ' << int(a.weapon3) << ' ' << a.rockets << ' ' << a.smartbombs << ' ' << a.thunderbolts << ' ' << a.shield << ' ' << a.magma << "\n";
    if (s.hasWorld) {
        f << "heli " << s.heliPos.x << ' ' << s.heliPos.y << ' ' << s.heliPos.z << ' ' << s.heliYaw << "\n";
        f << "timer " << s.missionTimer << ' ' << s.missionTimerStart << ' ' << int(s.timerShown) << ' ' << (s.lastMissionFile.empty() ? "-" : s.lastMissionFile) << "\n";
        f << "sets"; for (const auto& x : s.activeSets) f << ' ' << x; f << "\n";
        f << "alive "; for (uint8_t v : s.alive) f << char('0' + v); f << "\n";
        f << "shielddown "; for (uint8_t v : s.shieldDown) f << char('0' + v); f << "\n";
        f << "taken"; for (int t : s.taken) f << ' ' << t; f << "\n";
        if (!s.scape.empty()) f << "scape " << s.scape << "\n";
        f << "tank " << int(s.tank) << "\n";
    }
    return bool(f);
}

bool read(int p, int l, int m, SaveGame& s) {
    std::ifstream f(fileName(p, l, m));
    if (!f) return false;
    std::string line, key;
    if (!std::getline(f, line) || line.rfind("OPENASSAULT_SAVE", 0) != 0) return false;
    s = SaveGame{};
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        if (!(ls >> key)) continue;
        if (key == "pilot") ls >> s.pilot;
        else if (key == "level") ls >> s.level;
        else if (key == "mission") ls >> s.mission;
        else if (key == "file") ls >> s.missionFile;
        else if (key == "score") ls >> s.score;
        else if (key == "ticks") ls >> s.playTicks;
        else if (key == "arms") {
            SaveArms& a = s.arms; int w3 = 0;
            ls >> a.sel >> a.ammo[0] >> a.ammo[1] >> a.ammo[2] >> a.level[0] >> a.level[1] >> a.level[2] >> w3 >> a.rockets >> a.smartbombs >> a.thunderbolts >> a.shield >> a.magma;
            a.weapon3 = w3 != 0;
        } else if (key == "heli") { ls >> s.heliPos.x >> s.heliPos.y >> s.heliPos.z >> s.heliYaw; s.hasWorld = true; }
        else if (key == "timer") { int ts = 0; ls >> s.missionTimer >> s.missionTimerStart >> ts >> s.lastMissionFile; s.timerShown = ts != 0; if (s.lastMissionFile == "-") s.lastMissionFile.clear(); }
        else if (key == "sets") { std::string x; while (ls >> x) s.activeSets.push_back(x); }
        else if (key == "alive" || key == "shielddown") { std::string v; ls >> v; auto& out = key == "alive" ? s.alive : s.shieldDown; for (char c : v) out.push_back(uint8_t(c - '0')); }
        else if (key == "scape") ls >> s.scape;
        else if (key == "tank") { int v = 0; ls >> v; s.tank = v != 0; }
        else if (key == "taken") { int t; while (ls >> t) s.taken.push_back(t); }
    }
    return true;
}

std::vector<std::pair<int, int>> list(int pilot) {
    std::vector<std::pair<int, int>> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder(), ec)) {
        std::string n = e.path().filename().string();
        int p, l, m; char ext[8] = {0};
        if (std::sscanf(n.c_str(), "P%1dL%2dM%2d.%3s", &p, &l, &m, ext) == 4 && p == pilot && std::strcmp(ext, "SAV") == 0) out.push_back({l, m});
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool latest(int pilot, SaveGame& s) {
    auto v = list(pilot);
    while (!v.empty()) { if (read(pilot, v.back().first, v.back().second, s)) return true; v.pop_back(); }
    return false;
}

void erase(int pilot) {
    for (auto [l, m] : list(pilot)) { std::error_code ec; fs::remove(fileName(pilot, l, m), ec); }
}

// HISCORE.BIN like the original: 10 x {char name[16]; int32 score; int32 difficulty}
std::vector<HiScore> loadTop() {
    std::vector<HiScore> t;
    std::ifstream f(folder() + "HISCORE.BIN", std::ios::binary);
    if (f) {
        for (int i = 0; i < 10; i++) {
            char rec[24];
            if (!f.read(rec, 24)) { t.clear(); break; }
            HiScore h; h.name.assign(rec, strnlen(rec, 16));
            std::memcpy(&h.score, rec + 16, 4); std::memcpy(&h.diff, rec + 20, 4);
            t.push_back(h);
        }
    }
    if (t.size() != 10) {      // defaults (0xa1e88)
        t = {{"VIPER", 500000, 3}, {"MERLIN", 450000, 3}, {"MAVERICK", 400000, 3}, {"GOOSE", 350000, 3}, {"SLICK", 150000, 2},
             {"FLATBEE", 100000, 2}, {"HAMMER", 80000, 2}, {"WOLFMAN", 60000, 1}, {"KIRE", 40000, 1}, {"BANDIT", 20000, 0}};
    }
    return t;
}

void saveTop(const std::vector<HiScore>& t) {
    std::ofstream f(folder() + "HISCORE.BIN", std::ios::binary | std::ios::trunc);
    for (size_t i = 0; i < 10 && i < t.size(); i++) {
        char rec[24] = {0};
        std::strncpy(rec, t[i].name.c_str(), 15);
        std::memcpy(rec + 16, &t[i].score, 4); std::memcpy(rec + 20, &t[i].diff, 4);
        f.write(rec, 24);
    }
}

int topRank(int score) {
    auto t = loadTop();
    for (int i = 0; i < 10; i++) if (score > t[size_t(i)].score) return i;
    return -1;
}

Settings loadSettings() {
    Settings s;
    std::ifstream f(folder() + "OPENASSAULT.CFG");
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq); int v = std::atoi(line.c_str() + eq + 1);
        if (k == "PILOT") s.pilot = std::clamp(v, 0, 3);
        else if (k == "MUSICVOLUME") s.musicVolume = std::clamp(v, 0, 100);
        else if (k == "SFXVOLUME") s.sfxVolume = std::clamp(v, 0, 100);
        else if (k == "MUSIC") s.music = v != 0;
        else if (k == "BRIEFING") s.briefing = std::clamp(v, 0, 2);
        else if (k == "SPEAK") s.speech = v != 0;
        else if (k == "DETAILSIGHT") s.viewRange = std::clamp(v, 0, 2);
        else if (k == "FULLSCREEN") s.fullscreen = v != 0;
        else if (k == "HDTEXTURES") s.hdTextures = v != 0;
        else if (k == "WINDOWSIZE") { int w = 0, h = 0; if (std::sscanf(line.c_str() + eq + 1, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 200) { s.windowW = w; s.windowH = h; } }
        else if (k == "FULLSCREENSIZE") { int w = 0, h = 0; if (std::sscanf(line.c_str() + eq + 1, "%dx%d", &w, &h) == 2) { s.fullW = w >= 320 ? w : 0; s.fullH = w >= 320 ? h : 0; } }
        else if (k == "RUMBLE") s.rumble = std::clamp(v, 0, 2);
        else if (k == "VIEWDISTANCE") s.viewDistance = std::clamp(v, 50, 300);
        else if (k == "LANGUAGE") { std::string l = line.substr(eq + 1); while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back(); if (!l.empty()) s.language = l; }
        else if (k.rfind("STICK", 0) == 0) { int id = std::atoi(k.c_str() + 5); if (id >= 0 && id < SCount) s.stick[id] = std::clamp(v, -1, 7); }
        else if (k.rfind("PAD", 0) == 0) { int id = std::atoi(k.c_str() + 3); if (id >= 0 && id < CControlCount) s.pad[id] = v; }
        else if (k.rfind("KEY", 0) == 0) {
            int id = std::atoi(k.c_str() + 3), a = 0, b = 0;
            if (id >= 0 && id < CControlCount && std::sscanf(line.c_str() + eq + 1, "%d,%d", &a, &b) == 2) { s.keys[id][0] = a; s.keys[id][1] = b; }
        }
    }
    return s;
}

void saveSettings(const Settings& s) {
    std::ofstream f(folder() + "OPENASSAULT.CFG", std::ios::trunc);
    f << "PILOT=" << s.pilot << "\nMUSICVOLUME=" << s.musicVolume << "\nSFXVOLUME=" << s.sfxVolume << "\nMUSIC=" << int(s.music)
      << "\nBRIEFING=" << s.briefing << "\nSPEAK=" << int(s.speech) << "\nDETAILSIGHT=" << s.viewRange << "\nVIEWDISTANCE=" << s.viewDistance << "\nFULLSCREEN=" << int(s.fullscreen) << "\nWINDOWSIZE=" << s.windowW << "x" << s.windowH << "\nFULLSCREENSIZE=" << s.fullW << "x" << s.fullH << "\nHDTEXTURES=" << int(s.hdTextures) << "\nRUMBLE=" << s.rumble << "\nLANGUAGE=" << s.language << "\n";
    for (int i = 0; i < CControlCount; i++) f << "KEY" << i << "=" << s.keys[i][0] << "," << s.keys[i][1] << "\n";
    for (int i = 0; i < CControlCount; i++) f << "PAD" << i << "=" << s.pad[i] << "\n";
    for (int i = 0; i < SCount; i++) f << "STICK" << i << "=" << s.stick[i] << "\n";
}

} // namespace profile
} // namespace xa
