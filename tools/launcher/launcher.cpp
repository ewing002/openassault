// OpenAssault launcher (Windows): starts the game and does the translation / texture jobs with buttons, so nobody
// needs a command prompt. It sits next to openassault.exe, changes only the settings file the game reads
// (<exe dir>\SAVEGAME\OPENASSAULT.CFG: LANGUAGE, HDTEXTURES, FULLSCREEN) and runs openassault.exe with
// --export-lang / --extract. Its own texts are English and translated like the game's: lang\<code>\PORT.TXT.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace {

enum Id { IdPlay = 100, IdLang, IdHd, IdFull, IdCode, IdMakeLang, IdLangFolder, IdGuide, IdExtract, IdHdFolder,
          IdTitle, IdGroupSettings, IdGroupLang, IdGroupTex, IdLangLabel, IdCodeLabel, IdTexHint, IdLangHint, IdWinLabel, IdWinSize, IdFullLabel, IdFullSize };

std::wstring g_exeDir;        // folder of the launcher and openassault.exe (ends with '\')
std::wstring g_langRoot;      // the lang folder the game uses (may not exist yet)
std::wstring g_gameRoot;      // the folder with DATA (HDTEX goes there)
std::wstring g_cfg;           // SAVEGAME\OPENASSAULT.CFG
std::vector<std::wstring> g_langs;            // codes, "EN" first
std::map<std::string, std::string> g_tr;      // English -> UTF-8 translation of the chosen language
HWND g_wnd;
HFONT g_font, g_bold, g_big;
std::map<int, HWND> g_ctl;
std::map<int, std::string> g_text;            // English text of each control

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
bool isDir(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
bool isFile(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
std::wstring parent(std::wstring p) {
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    size_t c = p.find_last_of(L"\\/");
    return c == std::wstring::npos ? std::wstring() : p.substr(0, c + 1);
}
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t p = s.find_first_not_of(" \t");
    return p == std::string::npos ? std::string() : s.substr(p);
}
std::string upper(std::string s) { for (auto& c : s) c = char(toupper(static_cast<unsigned char>(c))); return s; }
std::string readUtf8(const std::wstring& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s; s << f.rdbuf();
    std::string t = s.str();
    if (t.size() >= 3 && t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
    return t;
}

// ---- texts ----
std::wstring tr(const std::string& english) {
    auto it = g_tr.find(english);
    return wide(it != g_tr.end() ? it->second : english);
}
std::wstring langDir(const std::wstring& code) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_langRoot + L"*").c_str(), &fd);
    std::wstring found;
    if (h == INVALID_HANDLE_VALUE) return found;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && _wcsicmp(fd.cFileName, code.c_str()) == 0) found = g_langRoot + fd.cFileName + L"\\";
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
void loadTexts(const std::wstring& code) {
    g_tr.clear();
    if (_wcsicmp(code.c_str(), L"EN") == 0) return;
    std::wstring d = langDir(code);
    if (d.empty()) return;
    std::istringstream in(readUtf8(d + L"PORT.TXT"));
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find(" = ");
        if (eq != std::string::npos) g_tr[trim(line.substr(0, eq))] = trim(line.substr(eq + 3));
    }
}
std::wstring languageName(const std::wstring& code) {
    if (_wcsicmp(code.c_str(), L"EN") == 0) return L"English";
    std::istringstream in(readUtf8(langDir(code) + L"PORT.TXT"));
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("@name = ", 0) == 0) return wide(trim(line.substr(8))) + L" (" + code + L")";
    }
    return code;
}
void findLanguages() {
    g_langs = {L"EN"};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_langRoot + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> v;
    do {
        std::wstring n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L".." && _wcsicmp(n.c_str(), L"EN") != 0) {
            for (auto& c : n) c = wchar_t(towupper(c));
            v.push_back(n);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end());
    g_langs.insert(g_langs.end(), v.begin(), v.end());
}

// ---- settings file (only our keys change, the others stay) ----
std::string cfgGet(const std::string& key, const std::string& def) {
    std::istringstream in(readUtf8(g_cfg));
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos && upper(trim(line.substr(0, eq))) == key) return trim(line.substr(eq + 1));
    }
    return def;
}
void cfgSet(const std::string& key, const std::string& value) {
    std::istringstream in(readUtf8(g_cfg));
    std::string line, out;
    bool done = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos && upper(trim(line.substr(0, eq))) == key) { line = key + "=" + value; done = true; }
        out += line + "\n";
    }
    if (!done) out += key + "=" + value + "\n";
    CreateDirectoryW((g_exeDir + L"SAVEGAME").c_str(), nullptr);
    std::ofstream f(g_cfg, std::ios::binary | std::ios::trunc);
    f << out;
}

// ---- running openassault.exe ----
std::wstring gameExe() { return g_exeDir + L"openassault.exe"; }
// runs the game hidden with arguments, returns its exit code and console output
int runGame(const std::wstring& args, std::string& output) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + gameExe() + L"\" " + args;
    SetEnvironmentVariableW(L"XA_NOBOX", L"1");
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, g_exeDir.c_str(), &si, &pi);
    SetEnvironmentVariableW(L"XA_NOBOX", nullptr);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); output = "openassault.exe could not be started"; return -1; }
    char buf[512]; DWORD n;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) output.append(buf, n);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return int(code);
}
void openFolder(const std::wstring& p) { ShellExecuteW(g_wnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void message(const std::wstring& text, bool error) {
    MessageBoxW(g_wnd, text.c_str(), L"OpenAssault", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

// ---- window ----
void relabel() {
    for (auto& [id, text] : g_text) SetWindowTextW(g_ctl[id], tr(text).c_str());
    SetWindowTextW(g_wnd, L"OpenAssault");
}
HWND add(int id, const wchar_t* cls, const std::string& text, DWORD style, HFONT font = nullptr) {
    HWND h = CreateWindowExW(0, cls, tr(text).c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd, HMENU(INT_PTR(id)), nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(font ? font : g_font), TRUE);
    g_ctl[id] = h;
    if (!text.empty()) g_text[id] = text;
    return h;
}
void layout() {
    HDC dc = GetDC(g_wnd); const int dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(g_wnd, dc);
    auto S = [&](int v) { return MulDiv(v, dpi, 96); };
    auto at = [&](int id, int x, int y, int w, int h) { MoveWindow(g_ctl[id], S(x), S(y), S(w), S(h), TRUE); };
    at(IdTitle, 16, 10, 408, 34);
    at(IdPlay, 16, 50, 408, 44);
    at(IdGroupSettings, 16, 104, 408, 160);
    at(IdLangLabel, 30, 128, 150, 20); at(IdLang, 185, 124, 225, 300);
    at(IdHd, 30, 156, 380, 22); at(IdFull, 30, 180, 380, 22);
    at(IdWinLabel, 30, 208, 150, 20); at(IdWinSize, 185, 204, 225, 300);
    at(IdFullLabel, 30, 236, 150, 20); at(IdFullSize, 185, 232, 225, 300);
    at(IdGroupLang, 16, 272, 408, 142);
    at(IdLangHint, 30, 292, 380, 34);
    at(IdCodeLabel, 30, 334, 120, 20); at(IdCode, 150, 330, 60, 24); at(IdMakeLang, 218, 329, 192, 28);
    at(IdLangFolder, 30, 368, 186, 28); at(IdGuide, 224, 368, 186, 28);
    at(IdGroupTex, 16, 422, 408, 124);
    at(IdTexHint, 30, 442, 380, 48);
    at(IdExtract, 30, 502, 186, 30); at(IdHdFolder, 224, 502, 186, 30);
    RECT r{0, 0, S(440), S(560)};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    const int ww = r.right - r.left, wh = r.bottom - r.top;
    SetWindowPos(g_wnd, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    static bool centred = false;                       // once, at start: the middle of the desktop's work area
    if (!centred) {
        centred = true;
        RECT wa{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(g_wnd, nullptr, wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}
std::wstring currentLang() {
    int i = int(SendMessageW(g_ctl[IdLang], CB_GETCURSEL, 0, 0));
    return i >= 0 && size_t(i) < g_langs.size() ? g_langs[size_t(i)] : L"EN";
}
void fillLanguages(const std::wstring& select) {
    findLanguages();
    HWND c = g_ctl[IdLang];
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (size_t i = 0; i < g_langs.size(); i++) {
        SendMessageW(c, CB_ADDSTRING, 0, LPARAM(languageName(g_langs[i]).c_str()));
        if (_wcsicmp(g_langs[i].c_str(), select.c_str()) == 0) sel = int(i);
    }
    SendMessageW(c, CB_SETCURSEL, WPARAM(sel), 0);
}
std::vector<std::pair<int, int>> g_winModes, g_fullModes;
void findModes() {
    DEVMODEW dm{}; dm.dmSize = sizeof dm;
    std::set<std::pair<int, int>> s;
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++)
        if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480 && dm.dmBitsPerPel >= 32) s.insert({int(dm.dmPelsWidth), int(dm.dmPelsHeight)});
    g_fullModes.assign(s.begin(), s.end());
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_winModes.clear();
    for (std::pair<int, int> p : {std::pair<int, int>{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1366, 768},
                                  {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}})
        if (p.first <= sw && p.second <= sh) g_winModes.push_back(p);
    if (g_winModes.empty()) g_winModes.push_back({1280, 720});
}
std::pair<int, int> cfgSize(const std::string& key) {
    int w = 0, h = 0;
    std::sscanf(cfgGet(key, "0x0").c_str(), "%dx%d", &w, &h);
    return {w, h};
}
std::wstring sizeText(std::pair<int, int> p) { return std::to_wstring(p.first) + L" x " + std::to_wstring(p.second); }
// window sizes: the listed ones (a saved size outside the list stays as an extra entry); fullscreen: Desktop + the display's modes
void fillModes() {
    HWND c = g_ctl[IdWinSize];
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    std::pair<int, int> cur = cfgSize("WINDOWSIZE");
    if (cur.first < 320) cur = {1280, 720};
    if (std::find(g_winModes.begin(), g_winModes.end(), cur) == g_winModes.end()) g_winModes.push_back(cur);
    int sel = 0;
    for (size_t i = 0; i < g_winModes.size(); i++) {
        SendMessageW(c, CB_ADDSTRING, 0, LPARAM(sizeText(g_winModes[i]).c_str()));
        if (g_winModes[i] == cur) sel = int(i);
    }
    SendMessageW(c, CB_SETCURSEL, WPARAM(sel), 0);
    c = g_ctl[IdFullSize];
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    SendMessageW(c, CB_ADDSTRING, 0, LPARAM(tr("Desktop").c_str()));
    std::pair<int, int> f = cfgSize("FULLSCREENSIZE");
    sel = 0;
    for (size_t i = 0; i < g_fullModes.size(); i++) {
        SendMessageW(c, CB_ADDSTRING, 0, LPARAM(sizeText(g_fullModes[i]).c_str()));
        if (g_fullModes[i] == f) sel = int(i) + 1;
    }
    SendMessageW(c, CB_SETCURSEL, WPARAM(sel), 0);
}
void create() {
    NONCLIENTMETRICSW m{}; m.cbSize = sizeof m;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    g_font = CreateFontIndirectW(&m.lfMessageFont);
    LOGFONTW b = m.lfMessageFont; b.lfWeight = FW_BOLD; g_bold = CreateFontIndirectW(&b);
    LOGFONTW t = b; t.lfHeight = t.lfHeight * 2; g_big = CreateFontIndirectW(&t);
    add(IdTitle, L"STATIC", "", SS_CENTER, g_big); SetWindowTextW(g_ctl[IdTitle], L"OpenAssault");
    add(IdPlay, L"BUTTON", "Play", BS_DEFPUSHBUTTON | WS_TABSTOP, g_bold);
    add(IdGroupSettings, L"BUTTON", "Settings", BS_GROUPBOX);
    add(IdLangLabel, L"STATIC", "Language", 0);
    add(IdLang, L"COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    add(IdHd, L"BUTTON", "HD textures", BS_AUTOCHECKBOX | WS_TABSTOP);
    add(IdFull, L"BUTTON", "Fullscreen", BS_AUTOCHECKBOX | WS_TABSTOP);
    add(IdWinLabel, L"STATIC", "Window size", 0);
    add(IdWinSize, L"COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    add(IdFullLabel, L"STATIC", "Fullscreen size", 0);
    add(IdFullSize, L"COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    add(IdGroupLang, L"BUTTON", "Translation", BS_GROUPBOX);
    add(IdLangHint, L"STATIC", "Makes a copy of the original English texts for a new language; translate the files in it.", 0);
    add(IdCodeLabel, L"STATIC", "Language code", 0);
    add(IdCode, L"EDIT", "", WS_BORDER | ES_UPPERCASE | WS_TABSTOP);
    SendMessageW(g_ctl[IdCode], EM_SETLIMITTEXT, 8, 0);
    add(IdMakeLang, L"BUTTON", "Create translation", BS_PUSHBUTTON | WS_TABSTOP);
    add(IdLangFolder, L"BUTTON", "Open translations folder", BS_PUSHBUTTON | WS_TABSTOP);
    add(IdGuide, L"BUTTON", "Translation guide", BS_PUSHBUTTON | WS_TABSTOP);
    add(IdGroupTex, L"BUTTON", "Textures", BS_GROUPBOX);
    add(IdTexHint, L"STATIC", "Exports the game's textures as PNG pictures. Edit or upscale them and put them into the HDTEX folder under the same name: they are used when HD textures are on.", 0);
    add(IdExtract, L"BUTTON", "Export textures", BS_PUSHBUTTON | WS_TABSTOP);
    add(IdHdFolder, L"BUTTON", "Open HDTEX folder", BS_PUSHBUTTON | WS_TABSTOP);
    fillLanguages(wide(cfgGet("LANGUAGE", "HU")));
    SendMessageW(g_ctl[IdHd], BM_SETCHECK, cfgGet("HDTEXTURES", "1") != "0" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_ctl[IdFull], BM_SETCHECK, cfgGet("FULLSCREEN", "0") != "0" ? BST_CHECKED : BST_UNCHECKED, 0);
    if (!isDir(g_gameRoot + L"HDTEX")) EnableWindow(g_ctl[IdHd], FALSE);
    findModes();
    loadTexts(currentLang());
    fillModes();
    relabel();
    layout();
}

void command(int id, int code) {
    switch (id) {
    case IdPlay: {
        if (!isFile(gameExe())) { message(tr("openassault.exe is not next to the launcher."), true); return; }
        STARTUPINFOW si{}; si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + gameExe() + L"\"";
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            DestroyWindow(g_wnd);
        } else message(tr("openassault.exe could not be started."), true);
        return;
    }
    case IdLang:
        if (code == CBN_SELCHANGE) { cfgSet("LANGUAGE", narrow(currentLang())); loadTexts(currentLang()); relabel(); fillModes(); layout(); }
        return;
    case IdHd: cfgSet("HDTEXTURES", SendMessageW(g_ctl[IdHd], BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0"); return;
    case IdWinSize:
        if (code == CBN_SELCHANGE) {
            int i = int(SendMessageW(g_ctl[IdWinSize], CB_GETCURSEL, 0, 0));
            if (i >= 0 && size_t(i) < g_winModes.size()) cfgSet("WINDOWSIZE", std::to_string(g_winModes[size_t(i)].first) + "x" + std::to_string(g_winModes[size_t(i)].second));
        }
        return;
    case IdFullSize:
        if (code == CBN_SELCHANGE) {
            int i = int(SendMessageW(g_ctl[IdFullSize], CB_GETCURSEL, 0, 0));       // 0 = desktop
            if (i == 0) cfgSet("FULLSCREENSIZE", "0x0");
            else if (i > 0 && size_t(i) <= g_fullModes.size()) cfgSet("FULLSCREENSIZE", std::to_string(g_fullModes[size_t(i) - 1].first) + "x" + std::to_string(g_fullModes[size_t(i) - 1].second));
        }
        return;
    case IdFull: cfgSet("FULLSCREEN", SendMessageW(g_ctl[IdFull], BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0"); return;
    case IdMakeLang: {
        wchar_t buf[16] = {};
        GetWindowTextW(g_ctl[IdCode], buf, 16);
        std::wstring c = buf;
        bool valid = c.size() >= 2 && c.size() <= 8 && _wcsicmp(c.c_str(), L"EN") != 0 &&
                     std::all_of(c.begin(), c.end(), [](wchar_t ch) { return (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_'; });
        if (!valid) { message(tr("Type a code of 2-8 letters for the new language, e.g. DE (EN is the original)."), true); return; }
        if (!langDir(c).empty()) {
            message(tr("This language already exists; its folder opens now.") , false);
            openFolder(langDir(c));
            return;
        }
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        std::string out;
        int r = runGame(L"--export-lang " + c, out);
        if (r != 0) { message(tr("The translation could not be created:") + L"\n\n" + wide(out), true); return; }
        fillLanguages(currentLang());
        message(tr("The original texts are copied. Translate the files in the folder that opens now (see the translation guide), then choose the language here."), false);
        if (!langDir(c).empty()) openFolder(langDir(c));
        return;
    }
    case IdLangFolder:
        CreateDirectoryW(g_langRoot.c_str(), nullptr);
        openFolder(g_langRoot);
        return;
    case IdGuide:
        if (isFile(g_langRoot + L"README.TXT")) ShellExecuteW(g_wnd, L"open", (g_langRoot + L"README.TXT").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else message(tr("The translation guide (README.TXT in the lang folder) is missing."), true);
        return;
    case IdExtract: {
        std::wstring dir = parent(g_langRoot) + L"textures";
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        std::string out;
        int r = runGame(L"--extract \"" + dir + L"\"", out);
        if (r != 0) { message(tr("The textures could not be exported:") + L"\n\n" + wide(out), true); return; }
        message(tr("The textures are exported (INDEX.TXT lists which level uses them). Put your edited copies into the HDTEX folder under the same name."), false);
        openFolder(dir);
        return;
    }
    case IdHdFolder:
        CreateDirectoryW((g_gameRoot + L"HDTEX").c_str(), nullptr);
        EnableWindow(g_ctl[IdHd], TRUE);
        openFolder(g_gameRoot + L"HDTEX");
        return;
    }
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: g_wnd = w; create(); return 0;
    case WM_COMMAND: command(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = HDC(wp); SetBkMode(dc, TRANSPARENT);
        return LRESULT(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

void findFolders() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    g_exeDir = parent(std::wstring(buf, n));
    g_cfg = g_exeDir + L"SAVEGAME\\OPENASSAULT.CFG";
    // the same search as the game: lang next to the exe, in its parent, then its grandparent
    g_langRoot.clear();
    for (const std::wstring& d : {g_exeDir, parent(g_exeDir), parent(parent(g_exeDir))})
        if (g_langRoot.empty() && !d.empty() && isDir(d + L"lang")) g_langRoot = d + L"lang\\";
    if (g_langRoot.empty()) g_langRoot = g_exeDir + L"lang\\";
    // the game data: the first folder upwards with DATA
    g_gameRoot = g_exeDir;
    for (std::wstring d = g_exeDir; !d.empty(); d = parent(d))
        if (isDir(d + L"DATA")) { g_gameRoot = d; break; }
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    findFolders();
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc; wc.hInstance = inst; wc.lpszClassName = L"OpenAssaultLauncher";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"OpenAssault", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 460, 540, nullptr, nullptr, inst, nullptr);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(w, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
