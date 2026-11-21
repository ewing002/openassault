#pragma once
#include "../core/fs.h"
#include <string>
#include <vector>
#include <cstdint>

namespace xa {

struct TexturePage {
    std::string name;
    int width = 0, height = 0;
    std::vector<uint16_t> rgb565;   // decoded pixels, row-major
};

// TEXTURES\TEXTURE.DIR + TEXTURE.LIB of a level. Pages are referenced by index
// from polygon records.
class TextureLib {
public:
    bool load(const GameFS& fs, const std::string& levelDir);
    const std::vector<TexturePage>& pages() const { return pages_; }
    std::string error;
private:
    std::vector<TexturePage> pages_;
};

// RGB565 -> RGBA8 (alpha 0 for colour key 0x0000)
void rgb565ToRgba(const std::vector<uint16_t>& src, std::vector<uint8_t>& dst, bool colorKey);
bool writeTga(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba);
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba);

} // namespace xa
