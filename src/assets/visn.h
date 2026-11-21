// VISN texture decoder - reimplementation of the Extreme Assault (1997) texture codec.
// A JPEG-like format: 8x8 blocks, 3 planes (Cb, Cr, Y), canonical Huffman,
// AAN IDCT, output RGB565.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace xa {

// Decodes a VISN stream into RGB565 pixels. Returns false on malformed input.
// width/height receive the image size in pixels.
bool visnDecode(const uint8_t* data, size_t size, std::vector<uint16_t>& out,
                int& width, int& height);

inline bool isVisn(const uint8_t* d, size_t n) {
    return n >= 14 && d[0] == 'V' && d[1] == 'I' && d[2] == 'S' && d[3] == 'N';
}

} // namespace xa
