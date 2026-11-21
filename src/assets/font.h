#pragma once
#include "../core/fs.h"
#include <vector>
#include <string>
#include <cstdint>

namespace xa {

// MENU\FONT\*.DAT: a TGA strip with 256 equal cells (character code = cell index).
// Magenta (255,0,255) is transparent; glyph width = last opaque column of the cell.
struct Font {
    int texW = 0, texH = 0, cellW = 0;
    std::vector<uint8_t> rgba;
    int x[256] = {0}, w[256] = {0};
    bool load(const GameFS& fs, const std::string& path);
    int textWidth(const std::string& s, int spacing = 1) const;
    void addHungarian();   // builds É Á Ó Ő Ű ő ű into the free cells 0x91 0x92 0x98 0x9b 0x9c 0x9d 0x9e
};

// *.CHR text tables: "[n]{text}" entries (text may span lines; ';' comments a line).
class TextDB {
public:
    bool load(const GameFS& fs, const std::string& path);
    const std::string& get(int id) const;
private:
    std::vector<std::pair<int, std::string>> entries_;
};

} // namespace xa
