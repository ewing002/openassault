#include "hdtex.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "../../third_party/stb_image.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace xa {

uint64_t pageHash(const TexturePage& p) {
    uint64_t h = 1469598103934665603ull;
    for (uint16_t v : p.rgb565) { h ^= v & 0xff; h *= 1099511628211ull; h ^= v >> 8; h *= 1099511628211ull; }
    return h;
}

std::string pageFileName(const TexturePage& p) {
    char name[32]; std::snprintf(name, sizeof name, "%016llx", (unsigned long long)pageHash(p));
    return name;
}

bool hdPackPresent(const GameFS& fs) {
    return fs.resolve("HDTEX").has_value();
}

namespace {
// any-size RGBA -> size x size: area average when shrinking (an exact copy at the same size), bilinear when enlarging
void resample(const uint8_t* in, int w, int h, int size, std::vector<uint8_t>& out) {
    out.assign(size_t(size) * size * 4, 0);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float acc[4] = {0, 0, 0, 0};
            if (w >= size && h >= size) {
                int x0 = x * w / size, x1 = std::max(x0 + 1, (x + 1) * w / size);
                int y0 = y * h / size, y1 = std::max(y0 + 1, (y + 1) * h / size);
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++)
                        for (int c = 0; c < 4; c++) acc[c] += in[(size_t(yy) * w + xx) * 4 + c];
                float n = float((x1 - x0) * (y1 - y0));
                for (float& v : acc) v /= n;
            } else {
                float fx = (x + 0.5f) * w / size - 0.5f, fy = (y + 0.5f) * h / size - 0.5f;
                int x0 = std::clamp(int(std::floor(fx)), 0, w - 1), y0 = std::clamp(int(std::floor(fy)), 0, h - 1);
                int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
                float ax = std::clamp(fx - x0, 0.f, 1.f), ay = std::clamp(fy - y0, 0.f, 1.f);
                for (int c = 0; c < 4; c++) {
                    auto s = [&](int xx, int yy) { return float(in[(size_t(yy) * w + xx) * 4 + c]); };
                    acc[c] = (s(x0, y0) * (1 - ax) + s(x1, y0) * ax) * (1 - ay) + (s(x0, y1) * (1 - ax) + s(x1, y1) * ax) * ay;
                }
            }
            for (int c = 0; c < 4; c++) out[(size_t(y) * size + x) * 4 + c] = uint8_t(std::clamp(acc[c] + 0.5f, 0.f, 255.f));
        }
}
} // namespace

bool hdPage(const GameFS& fs, const TexturePage& p, int size, std::vector<uint8_t>& rgba, bool& fromPack) {
    fromPack = false;
    if (p.width != 256 || p.height != 256) return false;
    const int k = size / 256;
    std::vector<uint8_t> src; rgb565ToRgba(p.rgb565, src, true);
    bool fileAlpha = false;
    std::vector<uint8_t> file;
    const std::string base = "HDTEX\\" + pageFileName(p);
    for (const char* ext : {".png", ".tga", ".bmp", ".jpg", ".jpeg"}) {   // own pictures before the shipped JPEGs
        if (!fs.read(base + ext, file)) continue;
        int w = 0, h = 0, n = 0;
        if (uint8_t* px = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4)) {
            resample(px, w, h, size, rgba);
            stbi_image_free(px);
            fromPack = true;
            fileAlpha = n == 2 || n == 4;
            break;
        }
        std::printf("HDTEX: %s%s cannot be read (%s)\n", base.c_str(), ext, stbi_failure_reason());
    }
    if (!fromPack) {                      // bilinear upscale of the original
        rgba.assign(size_t(size) * size * 4, 0);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                float fx = (x + 0.5f) / k - 0.5f, fy = (y + 0.5f) / k - 0.5f;
                int x0 = std::clamp(int(fx), 0, 255), y0 = std::clamp(int(fy), 0, 255);
                int x1 = std::min(x0 + 1, 255), y1 = std::min(y0 + 1, 255);
                float ax = std::clamp(fx - x0, 0.f, 1.f), ay = std::clamp(fy - y0, 0.f, 1.f);
                for (int c = 0; c < 3; c++) {
                    auto s = [&](int xx, int yy) { return float(src[(size_t(yy) * 256 + xx) * 4 + c]); };
                    float v = (s(x0, y0) * (1 - ax) + s(x1, y0) * ax) * (1 - ay) + (s(x0, y1) * (1 - ax) + s(x1, y1) * ax) * ay;
                    rgba[(size_t(y) * size + x) * 4 + c] = uint8_t(v + 0.5f);
                }
            }
    }
    if (!fileAlpha)                       // colour key from the original page
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
                rgba[(size_t(y) * size + x) * 4 + 3] = src[(size_t(y / k) * 256 + x / k) * 4 + 3];
    return true;
}

} // namespace xa
