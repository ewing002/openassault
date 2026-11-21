#include "mission.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>

namespace xa {

static std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper((unsigned char)c)); return s; }

bool MissionFile::readFile(const GameFS& fs, const std::string& levelDir, int pilot, const std::string& name, std::vector<uint8_t>& raw) {
    if (pilot > 0 && fs.read(levelDir + "\\MISSION" + std::to_string(pilot + 1) + "\\" + name, raw)) return true;
    return fs.read(levelDir + "\\MISSIONS\\" + name, raw);
}

bool MissionFile::load(const GameFS& fs, const std::string& levelDir, const std::string& name, int pilotIndex) {
    std::vector<uint8_t> raw;
    if (!readFile(fs, levelDir, pilotIndex, name, raw)) return false;
    *this = MissionFile{};
    pilot = pilotIndex;
    file = upper(name);
    // Some files put a header's value on the next line ("[SCAPE]" / "SWITCH_START"): join those.
    std::vector<std::string> lines;
    {
        std::istringstream in0(decodeText(raw)); std::string l;
        static const char* valued[] = {"[SCAPE]", "[BORDER]", "[ENEMYSCAPE]", "[MUSIC]", "[MISSIONTIME]", "[SCALEADVSHOOTS]",
                                       "[SCALEENEMYSHOOTS]", "[SCROLLTEXT]", "[LOADMISSION]", "[LOADSUBMISSION]", "[LOADNEXTMISSION]",
                                       "[BRIEFINGMUSIC]"};
        bool pending = false;
        while (std::getline(in0, l)) {
            if (auto c = l.find(';'); c != std::string::npos) l.resize(c);
            std::istringstream ls(l); std::string a, b; ls >> a; ls >> b;
            if (a.empty()) continue;
            if (pending && a[0] != '[' && upper(a) != "IF") { lines.back() += " " + l; pending = false; continue; }
            pending = false;
            lines.push_back(l);
            if (b.empty()) for (const char* v : valued) if (upper(a) == v) pending = true;
        }
    }
    bool inBriefing = false, inGoals = false;
    Rule* rule = nullptr; bool inCond = false;
    for (std::string line : lines) {
        std::istringstream ls(line);
        std::string w;
        if (!(ls >> w)) continue;
        std::string W = upper(w);
        if (W == "[BRIEFING]") { inBriefing = true; continue; }
        if (W == "[ENDBRIEFING]") { inBriefing = false; continue; }
        if (inBriefing) { briefing.push_back(line); continue; }
        if (W == "[ENEMYSCAPE]") { std::string s; while (ls >> s) enemySets.push_back(upper(s)); continue; }
        if (W == "[MISSIONTIME]") { std::string t; ls >> t; int m = 0, sec = 0; std::sscanf(t.c_str(), "%d:%d", &m, &sec); timeLimit = m * 60 + sec; continue; }
        if (W == "[SCALEADVSHOOTS]") { int v; if (ls >> v) scaleAdv = float(v) / 100.f; continue; }
        if (W == "[SCALEENEMYSHOOTS]") { int v; if (ls >> v) scaleEnemy = float(v) / 100.f; continue; }
        if (W == "[MUSIC]") { ls >> music; continue; }
        if (W == "[BORDER]") { ls >> border; continue; }
        if (W == "[SCAPE]") { ls >> scape; continue; }
        if (W == "[SCROLLTEXT]") { ls >> scrollText; continue; }
        if (W == "[NOMISSIONBORDER]") { noBorder = true; continue; }
        if (W == "[SHOWMISSIONTEXTALWAYS]") { showTextAlways = true; continue; }
        if (W == "[DONOTSAVEGAME]") { noSave = true; continue; }
        if (W == "[SCALESPEED]") {
            std::string veh, what; int v = 100; ls >> veh >> what >> v;
            veh = upper(veh); what = upper(what);
            int vi = veh == "CHEAT" ? 0 : veh == "HELI" ? 1 : veh == "TANK" ? 2 : -1;
            int wi = what == "FORWARD" ? 0 : what == "ROTATE" ? 1 : what == "SLIDE" ? 2 : what == "HEIGHT" ? 3 : -1;
            if (vi >= 0 && wi >= 0) scaleSpeed[vi][wi] = v;
            continue;
        }
        if (W == "[MISSIONGOALS]") { inGoals = true; continue; }
        if (W == "[ENDMISSIONGOALS]") { inGoals = false; continue; }
        if (!inGoals) continue;
        if (W == "[SUCCESSFULL]" || W == "[FAILED]") {
            rules.push_back({}); rule = &rules.back(); rule->failed = W == "[FAILED]"; inCond = true; continue;
        }
        if (W == "[ENDSUCCESSFULL]" || W == "[ENDFAILED]") { inCond = false; continue; }
        if (W == "IF" && rule && inCond) {
            Cond c; std::string a, b; ls >> a; a = upper(a);
            if (a == "DESTROYED" || a == "RESCUED") {
                // "[NR n] TYP name[,name...]": NR = how many must be destroyed / rescued (default: all)
                c.kind = a == "DESTROYED" ? Cond::Destroyed : Cond::Rescued;
                while (ls >> b) {
                    b = upper(b);
                    if (b == "NR") ls >> c.nr;
                    else if (b == "TYP") ls >> c.typ;
                }
            } else if (a == "COLLIDED") {
                std::string typ, with, shape; ls >> b >> c.typ >> with >> shape;
                ls >> c.sphere[0] >> c.sphere[1] >> c.sphere[2] >> c.sphere[3];
                c.kind = Cond::Collided;
            } else if (a == "PATHLASTPOSREACHED") { ls >> b >> c.typ; c.kind = Cond::PathEnd; }
            else if (a == "ORDER") {
                // "ORDER n <condition>": goals to be met in sequence; the condition itself follows
                int n = 0; ls >> n;
                std::string restLine; std::getline(ls, restLine);
                std::istringstream rl(restLine); std::string a2; rl >> a2; a2 = upper(a2);
                if (a2 == "DESTROYED" || a2 == "RESCUED") {
                    c.kind = a2 == "DESTROYED" ? Cond::Destroyed : Cond::Rescued;
                    while (rl >> b) { b = upper(b); if (b == "NR") rl >> c.nr; else if (b == "TYP") rl >> c.typ; }
                } else if (a2 == "COLLIDED") {
                    std::string with, shape; rl >> b >> c.typ >> with >> shape;
                    rl >> c.sphere[0] >> c.sphere[1] >> c.sphere[2] >> c.sphere[3];
                    c.kind = Cond::Collided;
                } else if (a2 == "PATHLASTPOSREACHED") { rl >> b >> c.typ; c.kind = Cond::PathEnd; }
                c.order = n;
            }
            rule->conds.push_back(c);
            continue;
        }
        if (W[0] == '[' && rule && !inCond && rule->action.empty() && W != "[GOAL]" && W != "[ENDGOAL]") {
            rule->action = W; ls >> rule->next; rule->next = upper(rule->next);
        }
    }
    // A [CONTINUE] block continues the success block before it: its conditions have to be met
    // after that block's (ORDER) and the earlier block's action (e.g. [LOADMISSION]) is taken then.
    // Examples: L1 MISS_05 "destroy the invaders, then fly into the tunnel"; L2 MISS_99.
    for (size_t i = 1; i < rules.size(); i++) {
        if (rules[i].action != "[CONTINUE]" || rules[i].failed) continue;
        size_t j = i;
        while (j > 0) { j--; if (!rules[j].failed && rules[j].action != "[CONTINUE]") break; }
        if (rules[j].failed || rules[j].action == "[CONTINUE]") continue;
        int maxOrder = 0;
        for (auto& c : rules[j].conds) { if (c.order < 0) c.order = 0; maxOrder = std::max(maxOrder, c.order); }
        for (auto c : rules[i].conds) { if (c.order <= maxOrder) c.order = maxOrder + 1; rules[j].conds.push_back(c); }
        rules[i].conds.clear();
    }
    return true;
}

} // namespace xa
