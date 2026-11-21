#pragma once
#include <SDL.h>
#include <string>

namespace xa {

// Game controller support (OpenAssault addition; the original only knew keyboard, mouse and joystick).
// Every SDL game controller (XInput, DualShock / DualSense, Switch Pro ...) is opened automatically.
// Buttons are "virtual buttons": SDL_GameControllerButton 0..20, plus kLT / kRT for the triggers.
// A control binding stores kPad + virtual button (Settings::pad).
namespace pad {
constexpr int kPad = 2000;
constexpr int kLT = 30, kRT = 31, kCount = 32;

void init();                                 // opens the subsystem and all connected controllers
void event(const SDL_Event& e);              // call first for every event: hot-plug, trigger edges
int lastPressed();                           // virtual button pressed by the last event, -1 = none
bool held(int vbutton);
float axis(SDL_GameControllerAxis a);        // -1..1 with dead zone, strongest of all controllers
bool connected();
std::string name(int vbutton);               // "A", "RT", "DPAD UP" ...
void rumble(float strength, int ms);         // a weaker pulse does not cut a running stronger one
void setRumbleScale(float s);                // Options > Rumble: 0 off, 0.5 low, 1 high
std::string stickName(int binding);          // "LEFT Y", "RIGHT X INV", "-"
int stickMoved(const SDL_Event& e);          // stick axis pushed far by this event (SDL axis), -1 = none
// UI translation of the last event: menus get arrows / Enter / Esc, the game Esc (Start) and Space (A)
SDL_Keycode menuKey(const SDL_Event& e, bool inGame);
}

} // namespace xa
