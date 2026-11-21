#include "lang.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace xa::lang {

namespace {
std::string g_code = "EN";
fs::path g_root;                                  // the lang folder
std::map<std::string, std::string> g_port;        // English -> font-coded translation

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream s; s << f.rdbuf();
    std::string t = s.str();
    if (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t.erase(0, 3);
    return t;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t p = s.find_first_not_of(" \t");
    return p == std::string::npos ? std::string() : s.substr(p);
}

std::string upperAscii(std::string s) { for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }

fs::path langDir(const std::string& code) {
    if (g_root.empty()) return {};
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(g_root, ec))
        if (e.is_directory() && upperAscii(e.path().filename().string()) == upperAscii(code)) return e.path();
    return {};
}

// finds a file below dir whatever the case of each component (the game opens DATA\LEVEL01\... paths)
fs::path findCaseless(const fs::path& dir, const std::string& rel) {
    fs::path cur = dir;
    std::string part;
    std::istringstream in(rel);
    std::vector<std::string> parts;
    for (char c : rel) { if (c == '\\' || c == '/') { if (!part.empty()) parts.push_back(part); part.clear(); } else part += c; }
    if (!part.empty()) parts.push_back(part);
    for (const auto& p : parts) {
        std::error_code ec;
        fs::path found;
        for (const auto& e : fs::directory_iterator(cur, ec))
            if (upperAscii(e.path().filename().string()) == upperAscii(p)) { found = e.path(); break; }
        if (found.empty()) return {};
        cur = found;
    }
    return cur;
}

void loadPort() {
    g_port.clear();
    fs::path d = langDir(g_code);
    if (d.empty()) return;
    std::istringstream in(readFile(d / "PORT.TXT"));
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find(" = ");
        if (eq == std::string::npos) continue;
        g_port[trim(line.substr(0, eq))] = toFont(trim(line.substr(eq + 3)));
    }
}
} // namespace

void init(const std::string& exeDir) {
    g_root.clear();
    fs::path base(exeDir);
    for (fs::path p : {base / "lang", base.parent_path() / "lang", base.parent_path().parent_path() / "lang"}) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) { g_root = p; break; }
    }
    loadPort();
}

void set(const std::string& code) {
    g_code = code.empty() ? "EN" : upperAscii(code);
    if (g_code != "EN" && langDir(g_code).empty()) g_code = "EN";
    loadPort();
}

const std::string& code() { return g_code; }

std::vector<std::string> available() {
    std::vector<std::string> v{"EN"};
    std::error_code ec;
    if (!g_root.empty())
        for (const auto& e : fs::directory_iterator(g_root, ec))
            if (e.is_directory() && upperAscii(e.path().filename().string()) != "EN") v.push_back(upperAscii(e.path().filename().string()));
    std::sort(v.begin() + 1, v.end());
    return v;
}

std::string displayName(const std::string& code) {
    if (upperAscii(code) == "EN") return "English";
    fs::path d = langDir(code);
    std::istringstream in(readFile(d / "PORT.TXT"));
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("@name = ", 0) == 0) return toFont(trim(line.substr(8)));
    }
    return code;
}

std::string nextLanguage(const std::string& code) {
    auto v = available();
    auto it = std::find(v.begin(), v.end(), upperAscii(code));
    if (it == v.end() || ++it == v.end()) return v.front();
    return *it;
}

void overlay(const std::string& relPath, std::vector<std::pair<int, std::string>>& entries) {
    if (g_code == "EN") return;
    fs::path d = langDir(g_code);
    if (d.empty()) return;
    fs::path f = findCaseless(d, relPath);
    if (f.empty()) return;
    std::vector<std::pair<int, std::string>> tr;
    parseTable(toFont(readFile(f)), tr);
    for (auto& [id, text] : tr) {
        auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.first == id; });
        if (it != entries.end()) it->second = text; else entries.push_back({id, text});
    }
}

const std::string& tr(const std::string& english) {
    auto it = g_port.find(english);
    return it != g_port.end() ? it->second : english;
}

std::string toFont(const std::string& s) {
    static const std::map<unsigned, char> table = {
        {0xE1, char(0xA0)}, {0xE9, char(0x82)}, {0xED, char(0xA1)}, {0xF3, char(0xA2)}, {0xF6, char(0x94)}, {0x151, char(0x9D)},
        {0xFA, char(0xA3)}, {0xFC, char(0x81)}, {0x171, char(0x9E)},
        {0xC1, char(0x92)}, {0xC9, char(0x91)}, {0xCD, char(0xD6)}, {0xD3, char(0x98)}, {0xD6, char(0x99)}, {0x150, char(0x9B)},
        {0xDA, char(0xE9)}, {0xDC, char(0x9A)}, {0x170, char(0x9C)},
        {0xE4, char(0x84)}, {0xC4, char(0x8E)}, {0xDF, char(0xE1)}, {0xE8, char(0x8A)}, {0xC8, char(0x90)}, {0xAE, char(0xA8)},
        {0x201E, '"'}, {0x201D, '"'}, {0x201C, '"'}, {0x2019, '\''}, {0x2018, '\''}, {0x2013, '-'}, {0x2014, '-'}, {0xA0, ' '}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp; size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
        else { cp = '?'; n = 1; }
        i += n;
        if (cp < 0x80) { out += char(cp); continue; }
        if (cp == 0x2026) { out += "..."; continue; }
        auto it = table.find(cp);
        out += it != table.end() ? it->second : '?';
    }
    return out;
}

const std::vector<std::string>& portStrings() {
    static const std::vector<std::string> v = {
        // menus
        "Mission", "Rumble", "Language", "Mouse", "View distance", "HD textures", "Not installed", "Fullscreen",
        "Window size", "Fullscreen size", "Desktop",
        "CHEAT CODES ACTIVATED", "ASSAULT MODE ACTIVATED",
        // game
        "SPACE / ESC: SKIP BRIEFING", "SIOUX DESTROYED",
        // weapon names (AMMUNITION FOR %s)
        "RAZOR GUN", "LASER CANNON", "FIRE FLASH",
        // controller setup
        "Stick forward/back", "Stick turn", "Stick drift", "Stick height",
        "LEFT X", "LEFT Y", "RIGHT X", "RIGHT Y", "LEFT X INV", "LEFT Y INV", "RIGHT X INV", "RIGHT Y INV",
        "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT",
        // OpenAssaultLauncher.exe (tools/launcher; also uses Language, HD textures, Fullscreen)
        "Play", "Settings", "Translation", "Textures", "Language code", "Create translation",
        "Open translations folder", "Translation guide", "Export textures", "Open HDTEX folder",
        "Makes a copy of the original English texts for a new language; translate the files in it.",
        "Exports the game's textures as PNG pictures. Edit or upscale them and put them into the HDTEX folder under the same name: they are used when HD textures are on.",
        "openassault.exe is not next to the launcher.", "openassault.exe could not be started.",
        "Type a code of 2-8 letters for the new language, e.g. DE (EN is the original).",
        "This language already exists; its folder opens now.", "The translation could not be created:",
        "The original texts are copied. Translate the files in the folder that opens now (see the translation guide), then choose the language here.",
        "The translation guide (README.TXT in the lang folder) is missing.", "The textures could not be exported:",
        "The textures are exported (INDEX.TXT lists which level uses them). Put your edited copies into the HDTEX folder under the same name."};
    return v;
}

namespace {
// the original text tables the game shows (SPEAK.CHR only maps texts to voices)
const char* const kTables[] = {"DATA/MENU/TEXT/GFX.CHR", "DATA/GLOBAL/TEXT/INGAME.CHR",
    "DATA/LEVEL01/MISSIONS/MISSION.CHR", "DATA/LEVEL02/MISSIONS/MISSION.CHR", "DATA/LEVEL03/MISSIONS/MISSION.CHR",
    "DATA/LEVEL04/MISSIONS/MISSION.CHR", "DATA/LEVEL05/MISSIONS/MISSION.CHR", "DATA/LEVEL06/MISSIONS/MISSION.CHR"};

std::string cp850ToUtf8(const std::string& s) {
    static const unsigned short hi[128] = {
    0xc7, 0xfc, 0xe9, 0xe2, 0xe4, 0xe0, 0xe5, 0xe7, 0xea, 0xeb, 0xe8, 0xef, 0xee, 0xec, 0xc4, 0xc5,
    0xc9, 0xe6, 0xc6, 0xf4, 0xf6, 0xf2, 0xfb, 0xf9, 0xff, 0xd6, 0xdc, 0xf8, 0xa3, 0xd8, 0xd7, 0x192,
    0xe1, 0xed, 0xf3, 0xfa, 0xf1, 0xd1, 0xaa, 0xba, 0xbf, 0xae, 0xac, 0xbd, 0xbc, 0xa1, 0xab, 0xbb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0xc1, 0xc2, 0xc0, 0xa9, 0x2563, 0x2551, 0x2557, 0x255d, 0xa2, 0xa5, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0xe3, 0xc3, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0xa4,
    0xf0, 0xd0, 0xca, 0xcb, 0xc8, 0x131, 0xcd, 0xce, 0xcf, 0x2518, 0x250c, 0x2588, 0x2584, 0xa6, 0xcc, 0x2580,
    0xd3, 0xdf, 0xd4, 0xd2, 0xf5, 0xd5, 0xb5, 0xfe, 0xde, 0xda, 0xdb, 0xd9, 0xfd, 0xdd, 0xaf, 0xb4,
    0xad, 0xb1, 0x2017, 0xbe, 0xb6, 0xa7, 0xf7, 0xb8, 0xb0, 0xa8, 0xb7, 0xb9, 0xb3, 0xb2, 0x25a0, 0xa0,
    };
    std::string out;
    for (unsigned char c : s) {
        unsigned cp = c < 0x80 ? c : hi[c - 0x80];
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    return out;
}
} // namespace

bool exportTemplate(const std::string& codeIn, const std::string& exeDir,
                    const std::function<bool(const std::string&, std::string&)>& readOriginal, std::string& message) {
    std::string c = upperAscii(trim(codeIn));
    if (c.empty() || c == "EN" || c.find_first_of("/:.\x5c") != std::string::npos) {
        message = "Give a new language code, e.g. --export-lang DE (EN is the original text).";
        return false;
    }
    fs::path root = g_root.empty() ? fs::path(exeDir) / "lang" : g_root;
    std::error_code ec;
    if (!langDir(c).empty() || fs::exists(root / c, ec)) {
        message = "lang/" + c + " already exists; it is not overwritten.";
        return false;
    }
    fs::path dir = root / c;
    int written = 0;
    for (const char* t : kTables) {
        std::string text;
        if (!readOriginal(t, text)) continue;
        fs::path out = dir;
        std::string part;
        for (const char* p = t; *p; p++) { if (*p == '/') { out /= part; part.clear(); } else part += *p; }
        fs::create_directories(out, ec);
        std::ofstream f(out / part, std::ios::binary);
        f << cp850ToUtf8(text);
        if (f) written++;
    }
    fs::create_directories(dir, ec);
    std::ofstream port(dir / "PORT.TXT", std::ios::binary);
    const char nl = '\n';
    port << "# OpenAssault - the port's own texts (UTF-8). Format: English text = translation" << nl
         << "# Translate only the right side. Lines starting with # are comments." << nl
         << "# @name is the language name shown in Options > Language." << nl
         << "# The DATA folder holds the original game texts: translate the text inside each [n]{...}, keep the" << nl
         << "# numbers and braces. Entries you delete stay English. Lines starting with ; are comments." << nl
         << "@name = " << c << nl << nl;
    for (const auto& s : portStrings()) port << s << " = " << s << nl;
    if (!port) { message = "Could not write " + (dir / "PORT.TXT").string(); return false; }
    message = "Translation template written to " + dir.string() + " (" + std::to_string(written) +
              " text tables + PORT.TXT). Translate the texts, then choose the language in Options > Language.";
    return written > 0;
}

void parseTable(const std::string& t, std::vector<std::pair<int, std::string>>& out) {
    std::string clean; std::istringstream in(t); std::string line;
    while (std::getline(in, line)) {
        size_t p = line.find_first_not_of(" \t");
        if (p != std::string::npos && line[p] == ';') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        clean += line + "\n";
    }
    size_t i = 0;
    while ((i = clean.find('[', i)) != std::string::npos) {
        size_t e = clean.find(']', i);
        if (e == std::string::npos) break;
        std::string num = clean.substr(i + 1, e - i - 1);
        bool digits = !num.empty() && std::all_of(num.begin(), num.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        size_t ob = e + 1;
        if (!digits || ob >= clean.size() || clean[ob] != '{') { i = e; continue; }
        size_t cb = clean.find('}', ob);
        if (cb == std::string::npos) break;
        std::string text = clean.substr(ob + 1, cb - ob - 1);
        for (auto& c : text) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        out.push_back({std::stoi(num), text});
        i = cb;
    }
}

} // namespace xa::lang
