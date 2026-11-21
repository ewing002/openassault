#include "fs.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;
namespace xa {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::optional<std::string> GameFS::resolve(const std::string& rel) const {
    fs::path cur(root_);
    std::string part;
    auto step = [&](const std::string& name) -> bool {
        if (name.empty() || name == ".") return true;
        fs::path direct = cur / name;
        std::error_code ec;
        if (fs::exists(direct, ec)) { cur = direct; return true; }
        std::string want = lower(name);
        for (auto& e : fs::directory_iterator(cur, ec)) {
            if (lower(e.path().filename().string()) == want) { cur = e.path(); return true; }
        }
        return false;
    };
    for (char c : rel) {
        if (c == '\\' || c == '/') { if (!step(part)) return std::nullopt; part.clear(); }
        else part += c;
    }
    if (!step(part)) return std::nullopt;
    return cur.string();
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    f.seekg(0);
    out.resize(size_t(n));
    if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return bool(f);
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(f);
}

bool GameFS::read(const std::string& rel, std::vector<uint8_t>& out) const {
    auto p = resolve(rel);
    return p && readFile(*p, out);
}

std::string decodeText(const std::vector<uint8_t>& raw) {
    std::string s(raw.begin(), raw.end());
    // Encrypted files never start with a printable ASCII char; plain ones do.
    if (!raw.empty() && raw[0] >= 0x80)
        for (auto& c : s) c = char(~static_cast<unsigned char>(c));
    return s;
}

} // namespace xa
