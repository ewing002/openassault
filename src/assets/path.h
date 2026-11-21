#pragma once
#include "../core/fs.h"
#include <string>
#include <vector>
#include <map>

namespace xa {

// PATH\*.PTH: "POS x y z TIME t FLAG f SPEED s" lines (game units, speed per tick).
struct PathPoint { float pos[3]; int time = 0, flag = 0, speed = 0; };

class PathDB {
public:
    void setLevel(const GameFS& fs, const std::string& levelDir) { fs_ = &fs; dir_ = levelDir; }
    const std::vector<PathPoint>* get(const std::string& name);
private:
    const GameFS* fs_ = nullptr;
    std::string dir_;
    std::map<std::string, std::vector<PathPoint>> cache_;
};

} // namespace xa
