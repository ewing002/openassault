#include "hud.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace xa {

static void key565(int r, int g, int b, uint8_t* d) {
    d[0] = uint8_t(r); d[1] = uint8_t(g); d[2] = uint8_t(b);
    d[3] = (r > 240 && g < 16 && b > 240) ? 0 : 255;
}

bool Hud::loadTga(const GameFS& fs, Renderer& r, const char* path, Image& img, int markerCell) {
    std::vector<uint8_t> f;
    if (!fs.read(path, f) || f.size() < 18 || f[2] != 2) return false;
    int w = f[12] | (f[13] << 8), h = f[14] | (f[15] << 8), bpp = f[16] / 8;
    size_t off = 18 + f[0];
    if ((bpp != 2 && bpp != 3) || f.size() < off + size_t(w) * h * bpp) return false;
    bool topDown = (f[17] & 0x20) != 0;
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) {
        int sy = topDown ? y : h - 1 - y;
        for (int x = 0; x < w; x++) {
            const uint8_t* s = &f[off + (size_t(sy) * w + x) * bpp];
            uint8_t* d = &rgba[(size_t(y) * w + x) * 4];
            if (bpp == 3) key565(s[2], s[1], s[0], d);
            else { int v = s[0] | (s[1] << 8); key565(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31, d); }
        }
    }
    // digit strips: column 0 of every cell holds a red/yellow marker; make it transparent so
    // texture filtering cannot bleed it into the neighbouring glyph
    if (markerCell > 0) for (int y = 0; y < h; y++) for (int x = 0; x < w; x += markerCell) rgba[(size_t(y) * w + x) * 4 + 3] = 0;
    img.w = w; img.h = h; img.tex = r.uploadFont(rgba.data(), w, h);
    return true;
}

bool Hud::loadRle(const GameFS& fs, Renderer& r, const char* path, Image& img) {
    // *.RLE: s16 tokens, negative = skip (transparent), positive n = n RGB565 pixels; 1024 x 480, 640 used.
    std::vector<uint8_t> f;
    if (!fs.read(path, f)) return false;
    const int stride = 1024, H = 480, W = 640;
    std::vector<uint8_t> rgba(size_t(W) * H * 4, 0);
    size_t i = 0, px = 0, n = f.size() / 2;
    auto rd = [&](size_t k) { return int16_t(f[2 * k] | (f[2 * k + 1] << 8)); };
    while (i < n && px < size_t(stride) * H) {
        int t = rd(i++);
        if (t < 0) { px += size_t(-t); continue; }
        for (int k = 0; k < t && i < n; k++, px++) {
            uint16_t c = uint16_t(rd(i++));
            size_t x = px % stride, y = px / stride;
            if (x >= size_t(W) || y >= size_t(H)) continue;
            uint8_t* d = &rgba[(y * W + x) * 4];
            d[0] = uint8_t(((c >> 11) & 31) * 255 / 31); d[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
            d[2] = uint8_t((c & 31) * 255 / 31); d[3] = 255;
        }
    }
    img.w = W; img.h = H; img.tex = r.uploadFont(rgba.data(), W, H);
    return true;
}

void Hud::briefingFrame(std::vector<Renderer::HudQuad>& out, int w, int h) const {
    const uint8_t shade[4] = {255, 255, 255, 140};
    if (monitorShadow_.tex >= 0) blit(out, monitorShadow_, 0, 0, 640, 480, 0, 0, Center, w, h, shade);
    if (monitor_.tex >= 0) blit(out, monitor_, 0, 0, 640, 480, 0, 0, Center, w, h);
}

bool Hud::load(const GameFS& fs, Renderer& r) {
    loadRle(fs, r, "DATA\\GLOBAL\\COCKPIT\\3DFX\\HI.RLE", panel_);
    loadRle(fs, r, "DATA\\GLOBAL\\COCKPIT\\3DFX\\D6-MAIN.RLE", monitor_);
    loadRle(fs, r, "DATA\\GLOBAL\\COCKPIT\\3DFX\\D6-SHW.RLE", monitorShadow_);
    const char* base = "DATA\\GLOBAL\\COCKPIT\\ITEM\\HIRES\\";
    auto L = [&](const char* n, Image& img) { loadTga(fs, r, (std::string(base) + n).c_str(), img); };
    loadTga(fs, r, (std::string(base) + "CPDIGITS.DAT").c_str(), digits_, 13); L("SHIELD.DAT", shield_); L("HAIRCR.DAT", hair_); L("TARGET.DAT", target_);
    L("TARGETF.DAT", targetF_); L("VORHLT.DAT", lead_); L("ROCKET.DAT", rocket_);
    loadTga(fs, r, "DATA\\GLOBAL\\COCKPIT\\ITEM\\LORES\\DIGITSLO.DAT", digitsLo_, 7);
    L("WEAPON1.DAT", ammo12_); L("WEAPON3.DAT", ammo3_);
    L("1BIS3.DAT", sheet_[0]); L("2BIS3.DAT", sheet_[1]); L("3BIS3.DAT", sheet_[2]);
    return ok();
}

void Hud::blit(std::vector<Renderer::HudQuad>& out, const Image& img, float sx0, float sy0, float sx1, float sy1,
               float dx, float dy, Anchor a, int w, int h, const uint8_t* tint, float scaleMul) const {
    if (img.tex < 0) return;
    float s = float(h) / 480.f;
    float x = a == Left ? dx * s : a == Right ? float(w) - (640.f - dx) * s : float(w) * 0.5f + (dx - 320.f) * s;
    float y = dy * s;
    Renderer::HudQuad q{x, y, x + (sx1 - sx0) * s * scaleMul, y + (sy1 - sy0) * s * scaleMul, {255, 255, 255, 255}};
    if (tint) std::copy(tint, tint + 4, q.rgba);
    q.font = img.tex;
    q.u0 = sx0 / float(img.w); q.u1 = sx1 / float(img.w); q.v0 = sy0 / float(img.h); q.v1 = sy1 / float(img.h);
    out.push_back(q);
}

void Hud::number(std::vector<Renderer::HudQuad>& out, int value, int digits, float rightX, float y, Anchor a, int w, int h) const {
    if (digits_.tex < 0) return;
    std::string s = std::to_string(std::max(0, value));
    if (digits > 0) while (int(s.size()) < digits) s = "0" + s;
    const float dw = 13.f;
    float x = rightX - dw * float(s.size());
    for (char c : s) { float u = float(c - '0') * dw; blit(out, digits_, u, 0, u + dw, 16, x, y, a, w, h); x += dw; }
}

// Original number routine (0x44ee8): fixed digit count with leading zeros, drawn from x to the right.
void Hud::numberLeft(std::vector<Renderer::HudQuad>& out, const Image& font, float dw, float dh, int value, int digits, float x, float y, Anchor a, int w, int h) const {
    if (font.tex < 0) return;
    std::string s = std::to_string(std::max(0, value));
    while (int(s.size()) < digits) s = "0" + s;
    if (int(s.size()) > digits) s = s.substr(s.size() - size_t(digits));
    // column 0 of every cell is a red/yellow cell marker, not part of the glyph
    for (char c : s) { float u = float(c - '0') * dw; blit(out, font, u + 1, 0, u + dw, dh, x + 1, y, a, w, h); x += dw; }
}

void Hud::build(std::vector<Renderer::HudQuad>& out, const State& st, int w, int h) const {
    if (!ok()) return;
    const float s = float(h) / 480.f;
    // panels (source rect, anchor)
    blit(out, panel_, 0, 0, 160, 80, 0, 0, Left, w, h);
    blit(out, panel_, 224, 0, 449, 35, 224, 0, Center, w, h);
    blit(out, panel_, 520, 0, 640, 80, 520, 0, Right, w, h);
    blit(out, panel_, 190, 100, 450, 310, 190, 100, Center, w, h);
    blit(out, panel_, 0, 340, 320, 480, 0, 340, Left, w, h);
    blit(out, panel_, 320, 340, 640, 480, 320, 340, Right, w, h);
    // wider than 4:3: the two bottom halves leave a gap; the strip the mission ticker runs in (y 454..478 in
    // 640x480) is continuous in the original, so its middle piece is stretched across the gap
    if (panel_.tex >= 0 && float(w) > float(h) * 4.f / 3.f + 1.f) {
        const float x0 = 320.f * s, x1 = float(w) - 320.f * s;
        Renderer::HudQuad q{x0, 454.f * s, x1, 478.f * s, {255, 255, 255, 255}};
        q.font = panel_.tex;
        q.u0 = 300.f / float(panel_.w); q.u1 = 340.f / float(panel_.w); q.v0 = 454.f / float(panel_.h); q.v1 = 478.f / float(panel_.h);
        out.push_back(q);
    }
    frame_++;
    // shield bar (RE 0x3edb8): width = img_w * shield / 9900 at (275,10); blinks while immortal
    if (shield_.tex >= 0 && !(st.shieldBlink && (frame_ & 32))) {
        float fw = float(shield_.w) * std::clamp(st.shield, 0.f, 1.f);
        blit(out, shield_, 0, 0, fw, float(shield_.h), 275, 10, Center, w, h);
    }
    // weapon panel (RE 0x3ee4e): ammo bars width = img_w * min(ammo,4000)/4000,
    // weapon icons from 1BIS3/2BIS3/3BIS3 by level, right column = selected weapon.
    if (ammo12_.tex >= 0) {
        for (int k = 0; k < 2; k++) {
            float fw = float(ammo12_.w) * float(std::min(st.ammo[k], 4000)) / 4000.f;
            if (fw > 0) blit(out, ammo12_, 0, 0, fw, float(ammo12_.h), 44, k ? 31.f : 11.f, Left, w, h);
        }
    }
    if (ammo3_.tex >= 0 && st.weapon3) {
        float fw = float(ammo3_.w) * float(std::min(st.ammo[2], 4000)) / 4000.f;
        if (fw > 0) blit(out, ammo3_, 0, 0, fw, 6, 45, 52, Left, w, h);
    }
    {
        // src y / height / dst y per weapon row (original blit parameters)
        const float sy[3] = {0, 29, 55}, sh[3] = {23, 20, 23}, dy[3] = {7, 30, 50};
        for (int k = 0; k < 3; k++) {
            int sheet = k < 2 ? std::clamp(st.level[k], 0, 2) : (st.weapon3 ? 1 : 0);
            const Image& img = sheet_[sheet];
            if (img.tex < 0) continue;
            float sx = st.weapon == k ? 35.f : 0.f;
            blit(out, img, sx, sy[k], sx + 35.f, sy[k] + sh[k], 9, dy[k], Left, w, h);
        }
    }
    // rockets (RE 0x3f30f): one icon per rocket at x 537 + 15 i, y 11
    for (int i = 0; i < std::min(st.rockets, 6); i++) blit(out, rocket_, 0, 0, float(rocket_.w), float(rocket_.h), 537.f + float(i) * 15.f, 11, Right, w, h);
    // smart bombs and thunderbolts: 2 digits at (569,35) and (569,56)
    numberLeft(out, digits_, 13, 16, st.smartbombs, 2, 569, 35, Right, w, h);
    numberLeft(out, digits_, 13, 16, st.thunderbolts, 2, 569, 56, Right, w, h);
    if (st.magma > 0) numberLeft(out, digits_, 13, 16, st.magma / 100, 3, 569, 77, Right, w, h);   // MAGMA fuel / 100 (0x3e67b)
    // ammunition / 10, small digits (DIGITSLO) at (130,16) (130,36) (130,56)
    for (int k = 0; k < 3; k++) if (k < 2 || st.weapon3) numberLeft(out, digitsLo_, 7, 6, st.ammo[k] / 10, 3, 130, 16.f + 20.f * float(k), Left, w, h);
    // score (7 digits at 56,437) and time (12,418 / 43,418)
    numberLeft(out, digits_, 13, 16, st.score, 7, 56, 437, Left, w, h);
    if (st.timeLeft >= 0) {
        numberLeft(out, digits_, 13, 16, st.timeLeft / 60, 2, 12, 418, Left, w, h);
        numberLeft(out, digits_, 13, 16, st.timeLeft % 60, 2, 43, 418, Left, w, h);
    }
    // radar (bottom right, centre 581,401, radius 45)
    {
        float cx = float(w) - (640.f - 581.f) * s, cy = 401.f * s, rad = 44.f * s;
        for (const auto& b : st.radar) {
            float px = cx + b.x * rad, py = cy - b.y * rad;
            float r = (b.enemy || b.hostage ? 2.2f : 1.6f) * s;
            uint8_t c[4] = {uint8_t(b.enemy ? 255 : 120), uint8_t(b.enemy ? 60 : 255), uint8_t(b.enemy ? 40 : 120), 255};
            if (b.hostage) { c[0] = 255; c[1] = 230; c[2] = 60; }     // hostages: yellow
            out.push_back({px - r, py - r, px + r, py + r, {c[0], c[1], c[2], c[3]}});
        }
    }
    // target marks, lead point and crosshair (window pixel positions, scaled sprites)
    auto at = [&](const Image& img, float x, float y) {
        if (img.tex < 0) return;
        Renderer::HudQuad q{x - float(img.w) * s * 0.5f, y - float(img.h) * s * 0.5f, x + float(img.w) * s * 0.5f, y + float(img.h) * s * 0.5f, {255, 255, 255, 255}};
        q.font = img.tex; q.u0 = 0; q.u1 = 1; q.v0 = 0; q.v1 = 1;
        out.push_back(q);
    };
    auto rect = [&](float x0, float y0, float x1, float y1, const uint8_t* c) { out.push_back({x0, y0, x1, y1, {c[0], c[1], c[2], c[3]}}); };
    const float px = std::max(1.f, s);                                   // one 640x480 pixel
    // 0x44190: circle (0x443ec) plus four ticks pointing inwards, r / 3 long
    auto circle = [&](float cx, float cy, float r, const uint8_t* c) {
        int n = std::max(16, int(r * 6.2832f / px));
        for (int i = 0; i < n; i++) {
            float a = float(i) * 6.2832f / float(n), x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
            rect(x - px * 0.5f, y - px * 0.5f, x + px * 0.5f, y + px * 0.5f, c);
        }
        float t = r / 3.f;
        rect(cx - r, cy - px * 0.5f, cx - r + t, cy + px * 0.5f, c); rect(cx + r - t, cy - px * 0.5f, cx + r, cy + px * 0.5f, c);
        rect(cx - px * 0.5f, cy - r, cx + px * 0.5f, cy - r + t, c); rect(cx - px * 0.5f, cy + r - t, cx + px * 0.5f, cy + r, c);
    };
    // energy bar left of a sprite: grey frame 7 x 30 (0x8410), fill 5 wide, up to 28 high
    auto bar = [&](const Image& img, float x, float y, float energy, const uint8_t* fill) {
        const uint8_t grey[4] = {132, 130, 132, 255};
        float x0 = x - float(img.w) * s * 0.5f, y0 = y - float(img.h) * s * 0.5f;
        float fx0 = x0 - 8 * s, fx1 = fx0 + 7 * s, fy1 = y0 + 30 * s;
        rect(fx0, y0, fx1, y0 + px, grey); rect(fx0, fy1 - px, fx1, fy1, grey);
        rect(fx0, y0, fx0 + px, fy1, grey); rect(fx1 - px, y0, fx1, fy1, grey);
        int n = int(std::clamp(energy, 0.f, 1.f) * 28.f);
        if (n > 0) rect(x0 - 7 * s, y0 + float(1 + 28 - n) * s, x0 - 2 * s, y0 + 29 * s, fill);
    };
    const uint8_t white[4] = {235, 235, 235, 255}, blue[4] = {40, 80, 255, 255}, yellow[4] = {255, 255, 0, 255}, blueBar[4] = {0, 0, 255, 255};
    for (const auto& m : st.marks) {
        circle(m.x, m.y, std::clamp(m.radius, 6.f, 128.f) * s, m.kind == MEnemy ? white : blue);
        if (m.kind == MFriend) { at(targetF_, m.x, m.y); bar(targetF_, m.x, m.y, m.energy, blueBar); }
    }
    for (const auto& m : st.marks)
        if (m.locked) { at(target_, m.x, m.y); bar(target_, m.x, m.y, m.energy, yellow); }
    if (st.leadValid) at(lead_, st.leadX, st.leadY);          // VORHLT.DAT; HAIRCR.DAT is loaded but never drawn
}

} // namespace xa
