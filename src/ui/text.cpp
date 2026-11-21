#include "text.h"
#include <sstream>
#include <algorithm>

namespace xa {

float TextPainter::width(const Face& f, const std::string& s, float scale) const {
    return f.font ? float(f.font->textWidth(s)) * scale : 0.f;
}

float TextPainter::draw(std::vector<Renderer::HudQuad>& out, const Face& f, const std::string& s, float x, float y,
                        const uint8_t rgba[4], float scale) const {
    if (!f.font || f.tex < 0) return 0;
    const Font& fn = *f.font;
    float cx = x;
    for (unsigned char c : s) {
        int gw = fn.w[c];
        if (c != ' ' && gw > 0) {
            Renderer::HudQuad q{cx * sx, y * sy, (cx + float(gw) * scale) * sx, (y + float(fn.texH) * scale) * sy,
                                {rgba[0], rgba[1], rgba[2], rgba[3]}};
            q.font = f.tex;
            q.u0 = float(fn.x[c]) / float(fn.texW); q.u1 = float(fn.x[c] + gw) / float(fn.texW);
            q.v0 = 0; q.v1 = 1;
            out.push_back(q);
        }
        cx += float(gw + 1) * scale;
    }
    return cx - x;
}

void TextPainter::drawClipped(std::vector<Renderer::HudQuad>& out, const Face& f, const std::string& s, float x, float y,
                              const uint8_t rgba[4], float clipX0, float clipX1) const {
    if (!f.font || f.tex < 0) return;
    const Font& fn = *f.font;
    float cx = x;
    for (unsigned char c : s) {
        const int gw = fn.w[c];
        const float x0 = cx, x1 = cx + float(gw);
        cx += float(gw + 1);
        if (c == ' ' || gw <= 0 || x1 <= clipX0 || x0 >= clipX1) continue;
        const float a = std::max(x0, clipX0), b = std::min(x1, clipX1);
        const float u0 = float(fn.x[c]) / float(fn.texW), du = float(gw) / float(fn.texW);
        Renderer::HudQuad q{a * sx, y * sy, b * sx, (y + float(fn.texH)) * sy, {rgba[0], rgba[1], rgba[2], rgba[3]}};
        q.font = f.tex;
        q.u0 = u0 + du * (a - x0) / float(gw); q.u1 = u0 + du * (b - x0) / float(gw);
        q.v0 = 0; q.v1 = 1;
        out.push_back(q);
    }
}

std::vector<std::string> TextPainter::wrap(const Face& f, const std::string& s, float maxWidth, float scale) const {
    std::vector<std::string> lines;
    std::istringstream in(s);
    std::string word, line;
    while (in >> word) {
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(f, t, scale) > maxWidth) { lines.push_back(line); line = word; }
        else line = t;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

} // namespace xa
