#pragma once
#include "../assets/font.h"
#include "../render/renderer.h"
#include <string>
#include <vector>

namespace xa {

// Text drawing in a virtual 640x480 screen (like the original), scaled to the window.
class TextPainter {
public:
    struct Face { const Font* font = nullptr; int tex = -1; };
    Face small, big;
    float sx = 1, sy = 1;     // window pixels per virtual pixel
    void begin(int w, int h) { sx = float(w) / 640.f; sy = float(h) / 480.f; }
    // x,y virtual coordinates; returns width in virtual pixels
    float draw(std::vector<Renderer::HudQuad>& out, const Face& f, const std::string& s, float x, float y,
               const uint8_t rgba[4], float scale = 1.f) const;
    // like draw, but glyphs are cut at the virtual x range [clipX0, clipX1] (scrolling ticker)
    void drawClipped(std::vector<Renderer::HudQuad>& out, const Face& f, const std::string& s, float x, float y,
                     const uint8_t rgba[4], float clipX0, float clipX1) const;
    float width(const Face& f, const std::string& s, float scale = 1.f) const;
    std::vector<std::string> wrap(const Face& f, const std::string& s, float maxWidth, float scale = 1.f) const;
    void box(std::vector<Renderer::HudQuad>& out, float x0, float y0, float x1, float y1, const uint8_t rgba[4]) const {
        Renderer::HudQuad q{x0 * sx, y0 * sy, x1 * sx, y1 * sy, {rgba[0], rgba[1], rgba[2], rgba[3]}};
        out.push_back(q);
    }
};

} // namespace xa
