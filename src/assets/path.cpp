#include "path.h"
#include <sstream>
#include <cctype>

namespace xa {

const std::vector<PathPoint>* PathDB::get(const std::string& name) {
    std::string key = name;
    for (auto& c : key) c = char(std::toupper((unsigned char)c));
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.empty() ? nullptr : &it->second;
    std::vector<PathPoint>& pts = cache_[key];
    std::vector<uint8_t> raw;
    if (fs_ && fs_->read(dir_ + "\\PATH\\" + key, raw)) {
        std::istringstream in(decodeText(raw));
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string w;
            PathPoint p{}; bool have = false;
            while (ls >> w) {
                if (w == "POS") { ls >> p.pos[0] >> p.pos[1] >> p.pos[2]; have = true; }
                else if (w == "TIME") ls >> p.time;
                else if (w == "FLAG") ls >> p.flag;
                else if (w == "SPEED") ls >> p.speed;
            }
            if (have) pts.push_back(p);
        }
    }
    return pts.empty() ? nullptr : &pts;
}

} // namespace xa
