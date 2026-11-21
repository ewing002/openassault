#pragma once
#include "textures.h"
#include <cstdint>
#include <vector>
#include <string>

namespace xa {

// HD texture pack (OpenAssault addition): HDTEX\<hash>.jpg next to DATA, one picture per 256x256 texture page,
// used only when Options > HD textures is on. The shipped pack is 512x512 JPEG made with Real-ESRGAN
// (realesr-general-x4v3, 4x, then Lanczos down to 2x). <hash> = FNV-1a 64 of the page's RGB565 data (lower case
// hex), so identical pages of different levels share one file; --extract writes the original pages under the same
// names (PNG), so edited or upscaled copies can go into HDTEX\. .jpg .png .jpeg .tga .bmp of any size are read
// (resampled to `size`). The file's alpha channel is the transparency; without one (JPEG) the colour key
// (0x0000) comes from the original page.
uint64_t pageHash(const TexturePage& p);
std::string pageFileName(const TexturePage& p);   // <hash> without extension
bool hdPackPresent(const GameFS& fs);             // an HDTEX folder exists
// RGBA of one page at `size` (2x = 512): the HD file if there is one, else a smooth upscale of the original
bool hdPage(const GameFS& fs, const TexturePage& p, int size, std::vector<uint8_t>& rgba, bool& fromPack);

} // namespace xa
