#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace xa::lang {

// Text language of the port (OpenAssault addition; the original shipped one language per release).
// "EN" uses the original text files only. Any other code overlays each original [n]{text} table with
// lang/<code>/<the same relative path> (UTF-8; entries present there replace the original ones) and translates
// the port's own strings through lang/<code>/PORT.TXT ("English text = translation" lines, '#' comments,
// "@name = <language name>" for the menu).
void init(const std::string& exeDir);              // finds the lang folder: <exe dir>/lang, then <exe dir>/../lang
void set(const std::string& code);
const std::string& code();
std::vector<std::string> available();              // "EN" first, then every folder of lang/
std::string displayName(const std::string& code);  // font-coded
std::string nextLanguage(const std::string& code);

// Replaces / adds entries of an original text table (relPath as the game opens it, e.g. DATA\MENU\TEXT\GFX.CHR).
void overlay(const std::string& relPath, std::vector<std::pair<int, std::string>>& entries);
// The port's own UI strings (English in the code); returns the translation, font-coded, or the input.
const std::string& tr(const std::string& english);

// UTF-8 -> font cell codes: code page 850 plus the Hungarian cells Font::addHungarian builds
// (É 0x91, Á 0x92, Ó 0x98, Ő 0x9b, Ű 0x9c, ő 0x9d, ű 0x9e). Unknown characters become '?'.
std::string toFont(const std::string& utf8);

// Every English string the port passes to tr() (the PORT.TXT keys).
const std::vector<std::string>& portStrings();
// Translation template for a new language: writes lang/<code>/ (next to the exe when there is no lang folder yet)
// with the original English text tables at their game paths (CP850 -> UTF-8, layout and ';' comments kept) and a
// PORT.TXT whose lines are "English = English". readOriginal returns the decoded (CP850) text of a game file.
// Never overwrites: fails when the folder exists or code is EN. message says what was written or why it failed.
bool exportTemplate(const std::string& code, const std::string& exeDir,
                    const std::function<bool(const std::string& relPath, std::string& text)>& readOriginal, std::string& message);

// [n]{text} parser shared with TextDB (text may span lines; lines starting with ';' are comments)
void parseTable(const std::string& text, std::vector<std::pair<int, std::string>>& out);

} // namespace xa::lang
