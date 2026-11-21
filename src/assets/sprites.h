#pragma once
#include "../core/fs.h"
#include <vector>
#include <cstdint>

namespace xa {

// Explosion / smoke animations: DATA\GLOBAL\EXPL*\EXPLO.nnn, uncompressed 24-bit TGA
// frames (black = transparent, drawn additively). All frames are padded to 64x64.
struct SpriteAnim { int firstLayer = 0, frames = 0; };

class SpriteLib {
public:
    enum Kind { AirExplosion, GroundExplosion, Smoke, SmallExplosion, Count };
    static constexpr int kSize = 64;
    bool load(const GameFS& fs);
    const SpriteAnim& anim(Kind k) const { return anims_[k]; }
    const std::vector<uint8_t>& rgba() const { return rgba_; }   // layers * 64*64*4
    int layers() const { return layers_; }
private:
    SpriteAnim anims_[Count];
    std::vector<uint8_t> rgba_;
    int layers_ = 0;
};

} // namespace xa
