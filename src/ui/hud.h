#pragma once
#include "../core/fs.h"
#include "../render/renderer.h"
#include <vector>

namespace xa {

// The original cockpit HUD: GLOBAL\COCKPIT\3DFX\HI.RLE (640x480 overlay, 16-bit RLE)
// plus the items in GLOBAL\COCKPIT\ITEM\HIRES (digits, shield bar, crosshair, target marks).
// Panels are anchored to the screen corners so wide screens work.
class Hud {
public:
    struct Blip { float x, y; bool enemy; bool hostage = false; };   // radar: -1..1 relative, forward = up
    // target marks (0x3c1b0): a circle with four ticks per unit (radius = projected size / 3, 6..128 in 640x480),
    // grey for enemies, blue for ADV_FRIEND units (with TARGETF.DAT and a blue energy bar unless FRIENDUNSELECTIBLE);
    // the locked target gets TARGET.DAT and a yellow energy bar (0x3b67b)
    enum MarkKind { MEnemy, MFriend, MFriendPlain };
    struct Mark { float x, y; bool locked; float radius = 10; int kind = MEnemy; float energy = 1; };   // window pixels
    struct State {
        float shield = 1;                              // 0..1
        bool shieldBlink = false;                      // immortal extra active
        int weapon = 0, ammo[3] = {0, 0, 0}, level[3] = {0, 0, 0};
        bool weapon3 = false;
        int smartbombs = 0, thunderbolts = 0, magma = 0;
        int rockets = 0, score = 0, timeLeft = -1;
        bool aimValid = false; float aimX = 0, aimY = 0;   // window pixels
        bool leadValid = false; float leadX = 0, leadY = 0;
        std::vector<Mark> marks;
        std::vector<Blip> radar;
    };
    bool load(const GameFS& fs, Renderer& r);
    void build(std::vector<Renderer::HudQuad>& out, const State& s, int w, int h) const;
    bool ok() const { return panel_.tex >= 0; }
    // Briefing monitor (0x2f385: the cockpit picture is D6-MAIN.RLE while a briefing runs, D6-SHW.RLE its shadow).
    // The 3D picture is shown inside x 21..619, y 22..460 of the 640x480 frame.
    void briefingFrame(std::vector<Renderer::HudQuad>& out, int w, int h) const;
    static constexpr float kMonX0 = 21, kMonY0 = 22, kMonX1 = 619, kMonY1 = 460;
private:
    struct Image { int tex = -1, w = 0, h = 0; };
    bool loadTga(const GameFS& fs, Renderer& r, const char* path, Image& img, int markerCell = 0);
    enum Anchor { Left, Center, Right };
    void blit(std::vector<Renderer::HudQuad>& out, const Image& img, float sx0, float sy0, float sx1, float sy1,
              float dx, float dy, Anchor a, int w, int h, const uint8_t* tint = nullptr, float scaleMul = 1) const;
    void number(std::vector<Renderer::HudQuad>& out, int value, int digits, float rightX, float y, Anchor a, int w, int h) const;
    void numberLeft(std::vector<Renderer::HudQuad>& out, const Image& font, float dw, float dh, int value, int digits, float x, float y, Anchor a, int w, int h) const;
    Image monitor_, monitorShadow_;
    bool loadRle(const GameFS& fs, Renderer& r, const char* path, Image& img);
    Image panel_, digitsLo_, digits_, shield_, hair_, target_, targetF_, lead_, rocket_, ammo12_, ammo3_, sheet_[3];
    mutable unsigned frame_ = 0;
};

} // namespace xa
