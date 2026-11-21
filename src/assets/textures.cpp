#include "textures.h"
#include "visn.h"
#include <sstream>
#include <algorithm>

namespace xa {

bool TextureLib::load(const GameFS& fs, const std::string& levelDir) {
    std::vector<uint8_t> dirRaw, lib;
    if (!fs.read(levelDir + "\\TEXTURES\\TEXTURE.DIR", dirRaw) || !fs.read(levelDir + "\\TEXTURES\\TEXTURE.LIB", lib)) {
        error = "missing TEXTURE.DIR/LIB in " + levelDir; return false;
    }
    std::istringstream in(decodeText(dirRaw));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string name; int kind, index; long off, size; long stamp;
        if (!(ls >> name >> kind >> index >> off >> size >> stamp)) continue;
        if (index < 0 || off < 0 || size <= 0 || size_t(off + size) > lib.size()) continue;
        if (size_t(index) >= pages_.size()) pages_.resize(size_t(index) + 1);
        TexturePage& p = pages_[size_t(index)];
        p.name = name;
        const uint8_t* d = lib.data() + off;
        if (isVisn(d, size_t(size))) {
            if (!visnDecode(d, size_t(size), p.rgb565, p.width, p.height)) { error = "bad VISN " + name; continue; }
        } else if (size == 256 * 256 * 2) {
            p.width = p.height = 256;
            p.rgb565.resize(256 * 256);
            for (size_t i = 0; i < p.rgb565.size(); i++) p.rgb565[i] = uint16_t(d[2 * i] | (d[2 * i + 1] << 8));
        }
    }
    return !pages_.empty();
}

void rgb565ToRgba(const std::vector<uint16_t>& src, std::vector<uint8_t>& dst, bool colorKey) {
    dst.resize(src.size() * 4);
    for (size_t i = 0; i < src.size(); i++) {
        uint16_t c = src[i];
        int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        dst[4 * i + 0] = uint8_t((r * 527 + 23) >> 6);
        dst[4 * i + 1] = uint8_t((g * 259 + 33) >> 6);
        dst[4 * i + 2] = uint8_t((b * 527 + 23) >> 6);
        dst[4 * i + 3] = (colorKey && c == 0) ? 0 : 255;
    }
}

bool writeTga(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> f(18 + size_t(w) * h * 4);
    f[2] = 2; f[12] = uint8_t(w); f[13] = uint8_t(w >> 8); f[14] = uint8_t(h); f[15] = uint8_t(h >> 8);
    f[16] = 32; f[17] = 0x28; // top-left origin, 8 alpha bits
    for (size_t i = 0; i < size_t(w) * h; i++) {
        f[18 + 4 * i + 0] = rgba[4 * i + 2]; f[18 + 4 * i + 1] = rgba[4 * i + 1];
        f[18 + 4 * i + 2] = rgba[4 * i + 0]; f[18 + 4 * i + 3] = rgba[4 * i + 3];
    }
    return writeFile(path, f.data(), f.size());
}

// Minimal PNG writer (RGBA8, stored deflate blocks: larger files, but no zlib needed)
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    static uint32_t crcTable[256];
    if (!crcTable[1])
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[n] = c;
        }
    std::vector<uint8_t> f = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto be32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(f, uint32_t(data.size()));
        size_t start = f.size();
        f.insert(f.end(), type, type + 4);
        f.insert(f.end(), data.begin(), data.end());
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = start; i < f.size(); i++) c = crcTable[(c ^ f[i]) & 0xFF] ^ (c >> 8);
        be32(f, c ^ 0xFFFFFFFFu);
    };
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w)); be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});              // 8 bit, RGBA
    chunk("IHDR", ihdr);
    std::vector<uint8_t> raw;                               // filter byte 0 + row
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t i = 0; i < raw.size() || i == 0; i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    return writeFile(path, f.data(), f.size());
}

} // namespace xa
