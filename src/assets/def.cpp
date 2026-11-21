#include "def.h"
#include <sstream>
#include <cctype>
#include <cstdlib>

namespace xa {

static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

void DefDB::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line, cur;
    while (std::getline(in, line)) {
        if (auto c = line.find(';'); c != std::string::npos) line.resize(c);
        std::istringstream ls(line);
        std::string a, b;
        if (!(ls >> a)) continue;
        if (a == "[BEGIN]") { ls >> cur; cur = lower(cur); blocks_[cur]; continue; }
        if (a == "[END]") { cur.clear(); continue; }
        if (cur.empty()) continue;
        if (ls >> b) {
            char* end = nullptr;
            long v = std::strtol(b.c_str(), &end, 10);
            if (end && *end == 0) blocks_[cur][lower(a)] = int(v);
            else strs_[cur][lower(a)] = b;
        } else blocks_[cur][lower(a)] = 1;   // flags such as SOUNDEFFEKT_ONLY_WHEN_MOVING
    }
}

void DefDB::load(const GameFS& fs, const std::string& levelDir, int difficulty) {
    std::vector<uint8_t> raw;
    std::string n = difficulty > 0 && difficulty <= 3 ? "ENEMY" + std::to_string(difficulty) + ".DEF" : "ENEMY.DEF";
    auto one = [&](const std::string& dir) {
        if (fs.read(dir + "\\" + n, raw) || fs.read(dir + "\\ENEMY.DEF", raw)) parse(decodeText(raw));
    };
    one("DATA\\GLOBAL");
    one(levelDir);
}

const std::map<std::string, int>* DefDB::find(const std::string& name) const {
    auto it = blocks_.find(lower(name));
    if (it != blocks_.end()) return &it->second;
    auto dot = name.find('.');
    if (dot != std::string::npos) { it = blocks_.find(lower(name.substr(0, dot))); if (it != blocks_.end()) return &it->second; }
    return nullptr;
}

bool DefDB::has(const std::string& name) const { return find(name) != nullptr; }

int DefDB::get(const std::string& name, const std::string& key, int fallback) const {
    const auto* b = find(name);
    if (!b) return fallback;
    auto it = b->find(lower(key));
    if (it != b->end()) return it->second;
    // variants ("BLITZ_ENEMY.invade") inherit the keys they do not set from the base block
    auto dot = name.find('.');
    if (dot != std::string::npos) {
        auto bb = blocks_.find(lower(name.substr(0, dot)));
        if (bb != blocks_.end() && &bb->second != b) { auto it2 = bb->second.find(lower(key)); if (it2 != bb->second.end()) return it2->second; }
    }
    return fallback;
}

std::string DefDB::str(const std::string& name, const std::string& key) const {
    std::string n = lower(name);
    for (int pass = 0; pass < 2; pass++) {
        auto it = strs_.find(n);
        if (it != strs_.end()) { auto k = it->second.find(lower(key)); if (k != it->second.end()) return k->second; }
        if (blocks_.count(n) && pass == 0 && n.find('.') == std::string::npos) break;
        auto dot = n.find('.'); if (dot == std::string::npos) break;
        n = n.substr(0, dot);
    }
    return {};
}

} // namespace xa
