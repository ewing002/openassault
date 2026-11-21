#pragma once
#include "../core/fs.h"
#include <string>
#include <vector>

namespace xa {

// MISSIONS\MISS_nn.SCR: enemy sets, goals and follow-up missions. Briefing scripts
// (cameras, texts) are parsed later.
struct MissionFile {
    struct Cond {
        enum Kind { Destroyed, DestroyedNr, Collided, PathEnd, Rescued, Order, Unknown } kind = Unknown;
        std::string typ; int nr = 0; float sphere[4] = {0, 0, 0, 0};   // sphere in game units
        int order = -1;                 // ORDER n: sequence position (later goals only count after earlier ones)
    };
    struct Rule { bool failed = false; std::vector<Cond> conds; std::string action, next; int stage = 0; };
    std::string file;
    std::vector<std::string> enemySets;
    std::vector<Rule> rules;
    int timeLimit = -1;            // seconds
    float scaleAdv = 1, scaleEnemy = 1;
    std::string music;
    std::string border;
    std::string scape;             // [SCAPE]: scape switch (TYP name) applied at the mission start            // *.BRD: mission area polygon (DSTPOS x y of a camera file)
    std::vector<std::string> briefing;   // raw script lines between [BRIEFING] and [ENDBRIEFING]
    int scrollText = -1;                 // [SCROLLTEXT] n: mission goal text id
    bool noBorder = false;               // [NOMISSIONBORDER] (0x51930)
    bool showTextAlways = false;         // [SHOWMISSIONTEXTALWAYS] (0x5194e)
    bool noSave = false;                 // [DONOTSAVEGAME] (0x5196c): no savegame for this mission
    // [SCALESPEED] HELI|TANK FORWARD|ROTATE|SLIDE|HEIGHT percent (0x513fc): the player's movement
    // rates are multiplied by these (stay set for the rest of the level). -1 = not given.
    int scaleSpeed[3][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
    int pilot = 0;                       // pilot (difficulty) the file was loaded for
    // 0x5d9b7: LEVELxx\MISSIONn\ (n = pilot + 1) holds the pilot's own version of a mission file or
    // briefing camera; anything not there comes from LEVELxx\MISSIONS\.
    static bool readFile(const GameFS& fs, const std::string& levelDir, int pilot, const std::string& name, std::vector<uint8_t>& raw);
    bool load(const GameFS& fs, const std::string& levelDir, const std::string& name, int pilot = 0);
};

} // namespace xa
