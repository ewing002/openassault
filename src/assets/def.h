#pragma once
#include "../core/fs.h"
#include <string>
#include <map>

namespace xa {

// ENEMY.DEF: "[BEGIN] NAME ... key value ... [END]" blocks with numeric parameters
// (get_damage = hit points, score, shoot_*, speed ...). The level file overrides GLOBAL.
class DefDB {
public:
    // difficulty = pilot index 0..3: GLOBAL\\ENEMYn.DEF and LEVELxx\\ENEMYn.DEF (0x5d1f4; n empty for 0)
    void load(const GameFS& fs, const std::string& levelDir, int difficulty = 0);
    // Looks up 'name' (e.g. "BLITZ_ENEMY.invade"), falling back to the part before '.'.
    int get(const std::string& name, const std::string& key, int fallback) const;
    bool has(const std::string& name) const;
    std::string str(const std::string& name, const std::string& key) const;   // non-numeric values (soundeffekt x.wav)
private:
    void parse(const std::string& text);
    const std::map<std::string, int>* find(const std::string& name) const;
    std::map<std::string, std::map<std::string, int>> blocks_;
    std::map<std::string, std::map<std::string, std::string>> strs_;
};

} // namespace xa
