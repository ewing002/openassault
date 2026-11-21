#include "pad.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace xa {
namespace pad {

namespace {
std::vector<SDL_GameController*> pads;
int last = -1;
bool trig[2] = {false, false};               // LT / RT down (any controller)
int stickDir = 0;                            // left stick as menu arrows: 0 none, else the SDL keycode held
bool ready = false;

void openAll() {
    for (auto* p : pads) SDL_GameControllerClose(p);
    pads.clear();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) if (SDL_GameController* p = SDL_GameControllerOpen(i)) pads.push_back(p);
}
float raw(SDL_GameControllerAxis a) {
    float best = 0;
    for (auto* p : pads) {
        float v = float(SDL_GameControllerGetAxis(p, a)) / 32767.f;
        if (std::fabs(v) > std::fabs(best)) best = v;
    }
    return std::clamp(best, -1.f, 1.f);
}
}

void init() {
    if (ready) return;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return;
    ready = true;
    if (const char* v = std::getenv("XA_VPAD")) {      // testing: a virtual controller with fixed axes "lx,ly,rx,ry,lt,rt" (-1..1)
        SDL_VirtualJoystickDesc d{}; d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION; d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        d.naxes = SDL_CONTROLLER_AXIS_MAX; d.nbuttons = SDL_CONTROLLER_BUTTON_MAX; d.name = "OpenAssault test pad";
        int idx = SDL_JoystickAttachVirtualEx(&d);
        openAll();
        if (SDL_Joystick* j = idx >= 0 ? SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(idx)) : nullptr) {
            float a[6] = {0, 0, 0, 0, -1, -1};
            std::sscanf(v, "%f,%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
            for (int i = 0; i < 6; i++) SDL_JoystickSetVirtualAxis(j, i, Sint16(std::clamp(a[i], -1.f, 1.f) * 32767.f));
            SDL_JoystickUpdate();
        }
    }
    if (pads.empty()) openAll();
    std::printf("Controllers: %zu\n", pads.size());
}

void event(const SDL_Event& e) {
    last = -1;
    if (!ready) return;
    if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) { openAll(); return; }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) { last = e.cbutton.button; return; }
    if (e.type == SDL_CONTROLLERAXISMOTION &&
        (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        int t = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0 : 1;
        bool down = raw(SDL_GameControllerAxis(e.caxis.axis)) > 0.5f;
        if (down && !trig[t]) last = t ? kRT : kLT;
        trig[t] = down;
    }
}

int lastPressed() { return last; }
bool connected() { return !pads.empty(); }

bool held(int b) {
    if (b == kLT) return raw(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f;
    if (b == kRT) return raw(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f;
    if (b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX) return false;
    for (auto* p : pads) if (SDL_GameControllerGetButton(p, SDL_GameControllerButton(b))) return true;
    return false;
}

float axis(SDL_GameControllerAxis a) {
    const float dz = 0.18f;
    float v = raw(a);
    float m = std::fabs(v);
    if (m < dz) return 0;
    return (v > 0 ? 1.f : -1.f) * (m - dz) / (1.f - dz);
}

std::string name(int b) {
    static const char* n[] = {"A", "B", "X", "Y", "BACK", "GUIDE", "START", "L3", "R3", "LB", "RB",
                              "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "MISC", "PADDLE1", "PADDLE2", "PADDLE3", "PADDLE4", "TOUCHPAD"};
    if (b == kLT) return "LT";
    if (b == kRT) return "RT";
    if (b >= 0 && b < int(sizeof n / sizeof *n)) return n[b];
    return "?";
}

std::string stickName(int b) {
    if (b < 0) return "-";
    static const char* n[] = {"LEFT X", "LEFT Y", "RIGHT X", "RIGHT Y"};
    return std::string(n[(b / 2) & 3]) + ((b & 1) ? " INV" : "");
}

int stickMoved(const SDL_Event& e) {
    if (e.type != SDL_CONTROLLERAXISMOTION || e.caxis.axis > SDL_CONTROLLER_AXIS_RIGHTY) return -1;
    return std::abs(int(e.caxis.value)) > 24000 ? int(e.caxis.axis) : -1;
}

namespace { float rumbleScale = 1.f, curStrength = 0; Uint32 curEnd = 0; }
void setRumbleScale(float s) { rumbleScale = std::clamp(s, 0.f, 1.f); if (rumbleScale <= 0) for (auto* p : pads) SDL_GameControllerRumble(p, 0, 0, 0); }

void rumble(float s, int ms) {
    s = std::clamp(s, 0.f, 1.f) * rumbleScale;
    if (s <= 0 || pads.empty()) return;
    Uint32 now = SDL_GetTicks();
    if (now < curEnd && s < curStrength) return;
    curStrength = s; curEnd = now + Uint32(ms);
    Uint16 lo = Uint16(s * 65535.f), hi = Uint16(std::min(1.f, s * 0.8f) * 65535.f);   // low / high frequency motor
    for (auto* p : pads) SDL_GameControllerRumble(p, lo, hi, Uint32(ms));
}

SDL_Keycode menuKey(const SDL_Event& e, bool inGame) {
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_START: return inGame ? SDLK_ESCAPE : SDLK_RETURN;
        case SDL_CONTROLLER_BUTTON_A: return inGame ? SDLK_SPACE : SDLK_RETURN;
        case SDL_CONTROLLER_BUTTON_B: return inGame ? 0 : SDLK_ESCAPE;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return inGame ? 0 : SDLK_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return inGame ? 0 : SDLK_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return inGame ? 0 : SDLK_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return inGame ? 0 : SDLK_RIGHT;
        default: return 0;
        }
    }
    if (!inGame && e.type == SDL_CONTROLLERAXISMOTION &&
        (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)) {
        float x = raw(SDL_CONTROLLER_AXIS_LEFTX), y = raw(SDL_CONTROLLER_AXIS_LEFTY);
        int dir = 0;
        if (std::fabs(y) > 0.6f && std::fabs(y) >= std::fabs(x)) dir = y < 0 ? SDLK_UP : SDLK_DOWN;
        else if (std::fabs(x) > 0.6f) dir = x < 0 ? SDLK_LEFT : SDLK_RIGHT;
        else if (std::fabs(x) < 0.3f && std::fabs(y) < 0.3f) dir = 0;
        else dir = stickDir;                     // between the thresholds: keep
        int pressedNow = dir != stickDir ? dir : 0;
        stickDir = dir;
        return pressedNow;
    }
    return 0;
}

} // namespace pad
} // namespace xa
