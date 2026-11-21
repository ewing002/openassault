#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace xa {

// Access to the original game directory. All lookups are case-insensitive so the
// DOS-era upper/lower case mix works on Linux too.
class GameFS {
public:
    explicit GameFS(std::string root) : root_(std::move(root)) {}
    // Resolves a DOS style relative path ("DATA\\LEVEL01\\WORLD.DIR") to a real path.
    std::optional<std::string> resolve(const std::string& rel) const;
    bool read(const std::string& rel, std::vector<uint8_t>& out) const;
    const std::string& root() const { return root_; }
private:
    std::string root_;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const void* data, size_t size);

// Text files with the leading '3DI'/'[' are plain; *.TYP/*.DEF are XOR 0xFF.
std::string decodeText(const std::vector<uint8_t>& raw);

} // namespace xa
