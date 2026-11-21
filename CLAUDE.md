# OpenAssault – handover notes for Claude Code

A native rewrite of Blue Byte's **Extreme Assault** (1997, DOS / 3Dfx) in C++17 / SDL2 / OpenGL 3.3, for Windows
and Linux. Goal: **behave exactly like the original**. Every gameplay rule is taken from reverse engineering
`XA_3DFX.EXE`, not invented. The original game data (DATA, DATA.SFX, MUSIC) is read at run time; the program
contains no original data.

Scope: single player only. Network play (DATA\NETWORK: ARCTIC, CITY, DUEL, HIGHLAND; NET_* types) is out of scope
by decision; do not implement it.

Write reports in English; code comments are in English.

## Working with the user
- "go ahead" = continue with the next step, then build, test, deploy and report briefly.
- The user runs the Windows exe at `<game folder>\openassault\bin\openassault.exe` (the game root holds DATA and the
  exe searches upwards for the DATA folder).
- On deployment the new exe goes to `openassault\openassault.exe` and `openassault\bin\openassault.exe`, and the
  `OpenAssault/openassault.exe` entry of `<game folder>\OpenAssault.zip` is replaced (the zip is the distributable:
  exe + required data + HDTEX).
- Settings and saves: `<exe folder>\SAVEGAME\` (OPENASSAULT.CFG, P{p}L{ll}M{mm}.SAV, HISCORE.BIN).

## Build
```
# Linux
sudo apt install libsdl2-dev cmake g++ ninja-build
cmake -B build -G Ninja && cmake --build build
# Windows (MSVC or MinGW) – CMake downloads SDL2 if it is not installed
cmake -B build && cmake --build build --config Release
# Windows exe on Linux (MinGW-w64), static link
cmake -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_PREFIX_PATH=<SDL2-2.30.9-mingw>/x86_64-w64-mingw32
```
A new .cpp file must be added to `CMakeLists.txt` (`xacore`: SDL/GL-free data and simulation code; `openassault`:
the rest). Third-party code: `third_party/stb_vorbis.c`, `third_party/stb_image.h` (JPEG of the HD textures).

## Windows
The project was originally developed on Linux (native build, MinGW cross-compilation for the Windows exe, tests with
Xvfb and wine). The MSVC build (VS 2022 Build Tools) has worked since 2026-11-16 without changes. The C/C++ runtime
is linked statically (`CMAKE_MSVC_RUNTIME_LIBRARY`), so the exe needs no Visual C++ redistributable; SDL's ccache
support is disabled. On the user's machine the `python` command is the Store alias: **use `py`** (3.14, with
capstone, numpy, pillow). Instead of the "Developer PowerShell" the `vcvars64.bat` works:
`cmd /c "<VS>\VC\Auxiliary\Build\vcvars64.bat && cmake --build build"`.

Tools to install (PowerShell, `winget`):
```
winget install Git.Git
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Python.Python.3.12
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
pip install capstone numpy pillow          # re\ scripts and tools\hdtex
# only if Ghidra is needed: JDK 21 (winget install EclipseAdoptium.Temurin.21.JDK) + Ghidra 11.1.2 unpacked, GHIDRA environment variable
```
Alternative to MSVC: MSYS2 / WinLibs MinGW-w64 (`g++`), with the same CMake commands.

Build from the "Developer PowerShell for VS 2022" (the first build downloads and statically builds SDL2 2.30.9, internet needed):
```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                     # -> build\openassault.exe, build\xa_check.exe
```

Testing – **always with a separate save folder**, so the user's settings and saves are not overwritten:
```
$env:XA_SAVEDIR = "$env:TEMP\oa-test"
.\build\openassault.exe --game "<game folder>" --level 1 --brief 10 --shot "$env:TEMP\b.bmp"
.\build\openassault.exe --game "<game folder>" --level 1 --autoaim --fly 0.3,0,0.3,0,6 --shot "$env:TEMP\t.bmp"
$env:XA_AUTOGOAL = "2"; .\build\openassault.exe --game "<game folder>" --pilot 1 --level 3 --fly 0,0,0,0,900 --shot "$env:TEMP\x.bmp"
Remove-Item Env:XA_AUTOGOAL
```
A `--shot` run uses a hidden window, saves the image and exits; the output (e.g. `Saved ... (heli x,y,z ...)`, the
mission chain lines) goes to the console. To view a BMP, convert it to PNG:
`python -c "from PIL import Image; Image.open(r'$env:TEMP\b.bmp').save(r'$env:TEMP\b.png')"`, then Read it. In PowerShell
the `XA_*` variables are set with `$env:NAME = "value"`.

Installing for the user (exe into the two places + update of the distributable `OpenAssault.zip`):
```
python tools\deploy.py build\openassault.exe "<game folder>"
```
If the game is running, the exe is locked; the user must quit it first.

Reverse engineering on Windows: search `re\obj2.asm` with `Select-String` / grep, `python re\disx.py <from> <to>`,
Ghidra: `re\ghidra\run.bat out.c c3b67b`.
Git: `.gitattributes` enforces LF line endings (`*.bat` is CRLF).

## Source layout
| File | Contents |
|---|---|
| `src/main.cpp` | menu (`runMenu`), game loop (`runGame`), camera, briefing views, HUD assembly, test switches. A large file. |
| `src/core/fs.*` | `GameFS`: case-insensitive file lookup in the game folder |
| `src/assets/visn.*` | VISN texture codec (bit-exact) and the encoded text files (`decodeText`) |
| `src/assets/textures.*`, `hdtex.*` | TEXTURE.DIR/LIB pages; HD pack (HDTEX\\<FNV-1a hash>.jpg, 512×512) |
| `src/assets/model.*`, `world.*`, `typ.*`, `def.*`, `path.*`, `sprites.*`, `font.*` | PROTOTYP models, WORLD.BIN, OBJECT.TYP, ENEMYn.DEF, PTH paths, sprites, fonts |
| `src/game/heli.*` | original flight model (helicopter and tank) |
| `src/game/combat.*` | weapons, damage, extras, hostages, targets, explosions |
| `src/game/enemies.*` | enemy and friendly units, paths, shooting, crashing, sounds |
| `src/game/mission.*` | MISS_nn.SCR missions, goals, actions (LOADMISSION etc.) |
| `src/game/briefing.*` | briefing script (camera, text, ZOOM, HELIMOTION) |
| `src/game/profile.*` | saves, top ten, settings, key/controller mapping |
| `src/game/pad.*` | SDL GameController: buttons, sticks, menu translation, rumble |
| `src/game/collision.*`, `pose.*` | collision, part poses |
| `src/render/*` | OpenGL 3.3 renderer (texture array, fog, sprites, HUD quads, sub-viewports) |
| `src/ui/hud.*`, `text.*` | original cockpit HUD (GLOBAL\\COCKPIT), text drawing |
| `src/audio/*` | SDL mixer, WAV, OGG music |
| `tools/` | `xa_check` (data checker), `hdtex/` (making the HD pack), model diagnostics |
| `re/` | reverse-engineering material (see below) |

## Reverse engineering (re/)
- The files made from the original program (`xa3dfx.img`, `obj2.asm`, `obj3.asm`, `visn.asm`) are **not in git**
  (`.gitignore` excludes them; the GitHub repository is private, but original data must not be committed either).
  They are generated from the user's own `XA_3DFX.EXE`: `py re\mkre.py [XA_3DFX.EXE]` (needs capstone); the output is
  byte-identical.
- `re/xa3dfx.img`: the LE objects of `XA_3DFX.EXE` laid out at their linear addresses (made by `re/flat.py`), so
  file offset = virtual address. To search for a string: `d.find(b'...')`; the reference is a `mov reg, <address>`.
- `re/obj2.asm`: the full disassembly of the code object (`address: instruction`), e.g. `grep -n "0x1ebda0" re/obj2.asm`.
  To disassemble a range again: `python3 re/disx.py <from> <to>` (capstone).
- Ghidra (headless) decompilation: `re/ghidra/Decomp.java` + `run.sh out.c <address>` or `c<address>` (= the function
  containing it). Large functions (e.g. 0x2eba1, the HUD / game loop) time out; read their asm instead.
- Important globals and tables: type table `0x1ece2c + idx*0x114` (+0x2 behaviour, +0x38 flags: 0x10 UNSELECTIBLE,
  0x40 adv_friend, 0x200 FRIENDUNSELECTIBLE); object records with stride `idx*0x146` (energy: `[idx*0x146 + 0x21c5bc]`);
  selected target `0x253800`; screen size `0x253668` (0x140 = lores).
- In the code the behaviours are commented with the original address (e.g. `// 0x3b67b`).

## Testing (headless)
On Linux with Xvfb: `Xvfb :96 &`, `DISPLAY=:96 SDL_AUDIODRIVER=dummy ./openassault --game <game> ...`
- `--level N --mission N --pilot 0..3` starts directly; `--shot f.bmp` saves a screenshot and exits;
  `--brief <s>` shows the image at that second of the briefing (or of the flight after it), sped up;
  `--fly f,s,l,y,sec` scripted flight; `--autoaim`, `--fire`, `--cockpit` / `--chase`, `--cam`, `--size WxH`.
- `XA_AUTOGOAL=2`: completes the mission goals automatically – the test of the whole mission chain
  (24 runs: pilots 0–3 × levels 1–6 all finish).
- `XA_MENUPAGE=<n> XA_MENUSHOT=f.bmp`: image of a menu page (1 Main, 2 Areas, 5 Pilot, 7 Options, 8 Sound, 9 TopTen, 10 Detail, 11 Keys).
- `XA_SAVEDIR=<folder>`: separate settings/saves; `XA_VPAD=lx,ly,rx,ry,lt,rt`: virtual controller;
  `XA_PADDEBUG`, `XA_DEBUG` (briefing commands), `XA_ZOOMK`, `XA_LOOKAT`, `XA_LOADSAVE`, `XA_SAVETEST`, `XA_VEH`.
- `XA_WATCH=<world index>` points the camera at an object (offset: `XA_WATCHOFS=x,y,z`). `XA_WATCHSIDE=1` views it from
  the side, so the direction of motion points right on screen.
- `XA_NEAR=x,y,r` lists the world objects around a point (type, behaviour, stock, angles, release).
- `XA_HELIPOS=x,y,z` moves the helicopter to the given point once, after the briefing (e.g. next to a mission's
  targets so that they activate).
- The height of ground units above the terrain (`EnemySystem::measureGround`) can only be measured on the terrain set
  of the current area, so after every area change (SCAPESWITCH) the units that are still standing and not yet measured
  are measured again. Earlier, the units of SWITCH_TO_seg2 missions (e.g. L1 MISS_97) stayed unmeasured and did not follow the terrain.
- Testing the Windows exe on Linux: `wine openassault.exe --game . ...` (with the same switches).

## Finished features (the 13-step plan + extras)
Data loading and rendering; original flight model (helicopter/tank, SCALESPEED, magma); weapons, extras, hostages,
shield; enemies; missions and *_WHEN_DESTROYED events; vehicle change (TANK_BASE/HELI_BASE); briefing on the cockpit
monitor (ZOOM from outside onto the monitor, D6-MAIN frame), cut-scenes from outside in full screen; original HUD and
target marks (circle + strokes, TARGET.DAT + energy bar, VORHLT); menus, pilots (difficulty), save/load, top ten;
cheats; view distance (F6) and view-distance multiplier (F7/F8, menu); briefing sounds (SPEAK.CHR; in sound mode only
the text without sound is shown); key remapping; full screen (menu, Alt+Enter); controller (buttons and sticks can be
set, rumble Off/Low/High); HD textures (Real-ESRGAN, switchable); anisotropic filtering.

## Assumptions / open questions
- SPIDER / SPIDER360:
  - Done: walking on walls and ceilings, and jumping (`spiderMove`, 0x77534).
  - Done: walks only on GROUND objects, other objects turn it (±12·tick).
  - Done: collision with moving units (−12·tick); the same for vehicles. The player has no collision with them in the original either.
  - Done: the leg animation (0x7623c, `spiderLegs`; the part angles are passed to the pose through `Renderer::Instance::vars`).
    Checked with screenshots of fixed frames, not yet while walking.
  - Since 2026-11-16 the tilt approaches the original a0 / a1 angles (0x73a10 per angle); the placement angles give
    exactly the surface normal, which confirms the order.
  - Two SPIDER360 on the L3 crater wall used to fall through the ground when the player was in another area: the port
    switched off collision of hidden terrain sets, while the original queries see all areas. Fixed
    (`setScapeHidden` / `includeHiddenScape`).
  - Not yet checked in play, only in screenshots.
- The missing *ACTIVATIONAREA keys have, since 2026-11-16, the original per-behaviour default values (earlier: the path
  starts at once, only a hit wakes the unit, range 220). This may affect the waking and range of every enemy; not tested in play.
- ESEL / ESEL_AS / BARRACUDA (0x7635c):
  - Done: GROUND terrain, terrain tilt, too-steep slopes, falling, the water model (WATER_TYP) and the 2/3 speed.
  - Also done: felling trees, ship water detection by the WATER list, and ±12 turning around obstacles.
  - Checked: the L1 convoy reaches the water and switches its model.
  - Not checked: felling trees and obstacles. The automatic run barely moves the vehicles; the `XA_VEH=1` trace shows them.
- IMPERATOR beam radius: the damage is the `cause_damage` value of the IMPERATOR_BEAM DEF. There is no such block, so it is 0,
  as in the original on a fresh start; the user decided this on 2026-11-16. In the original the value left in memory
  between levels could have caused damage. The beam is only active when there is a clear line from the barrel to the
  player. On contact BMBLITZ.WAV plays (0x17); not yet heard in play.
- ROTATE_PART (rotors, radars) rotation direction: it may be mirrored after the angle fix. Deliberately not changed,
  because it cannot be compared with the original, and the pose code is also used by turret aiming.
- PLUTO (L6 mother ship): since 2026-11-16 the rescue pods of the death sequence (FLASH2 = ESCAPEPOD) and the FINALFACE
  pieces appear as models (checked in a screenshot). The lightning was only checked with numbers (XA_TRACE), not in play.
- ACTIVATION_WHEN_DESTROYED dormancy (0x59d1c): a dormant unit stands still and does not shoot; it is not separately
  verified that moving elements (BLOCK/MOVINGWALL) behave exactly the same in 0x85854.
- Enemy target circle colour: the original code 0x5acb (grey); based on the user's video we draw it in bright white.
- Friendly unit marking (blue circle, TARGETF.DAT, blue bar): checked from code only, not in play.
- The selected target is drawn above the cockpit panel; not checked in the original.
- Target selection (0x253800): since 2026-11-16 works as in the original. The target stays, the two "Lock Target" keys
  (G, H) choose it, and there is no target at start.
  - Since 2026-11-16 the candidates are all living objects of the mission set (except UNSELECTIBLE and SHIELD_ENEMY),
    extras included. Not yet checked in play when an extra or a building is selected.
  - The `--autoaim` test switch selects by itself.
- Vibration and controller were tested only with a virtual controller.

## Multilingual support (OpenAssault)
- `lang/<CODE>/` (in the repository; installed as `<game>\openassault\lang`, in the zip as `OpenAssault/lang`): translations
  of the original `[n]{text}` tables at their original paths (`DATA\MENU\TEXT\GFX.CHR`, `DATA\GLOBAL\TEXT\INGAME.CHR`,
  `DATA\LEVELnn\MISSIONS\MISSION.CHR`), in UTF-8. Existing ids replace the original; missing ones stay English.
  The port's own texts: `lang/<CODE>/PORT.TXT` ("English = translation", `@name = ` is the name shown in the menu).
- `src/core/lang.*`: finds the `lang` folder (exe folder, its parent, grandparent), `overlay` (called by TextDB::load),
  `tr` (port texts), `toFont` (UTF-8 → font codes). Hungarian by default (`OPENASSAULT.CFG LANGUAGE=HU`); Options →
  Language switches it (the menu texts change at once); `XA_LANG=EN` for tests. EN is the original text, unchanged.
- Letters: the Hungarian letters missing from the CP850 font sets (É Á Ó Ő Ű ő ű) are built at load time
  (`Font::addHungarian`: the plain letter + the acute of ó, doubled for the long ones) into the free cells
  0x91 0x92 0x98 0x9b 0x9c 0x9d 0x9e.
- `py tools\langcheck.py HU`: untranslated ids, unknown characters, scrolling texts longer than 128 characters, and the
  PORT.TXT keys checked against the list `lang::portStrings()`. **A new `lang::tr()` text must be added to that list too.**
- Template for a new language: `openassault.exe --export-lang DE` → `lang\DE\` with the original English tables (from the
  user's game data, CP850 → UTF-8, same layout) and a "English = English" PORT.TXT; an existing folder is never
  overwritten. The original text is not part of the repository or the zip. Translator guide: `lang/README.TXT` (English).
  `XA_NOBOX=1` skips the message box for tests.
- Speech (SPEAK_E) stays English; labels burnt into the graphics (e.g. the HUD "SCORE") cannot be translated.
- Besides Y/Z, I also counts as "yes" for the questions.

## Launcher (OpenAssaultLauncher.exe, Windows only)
- `tools/launcher/launcher.cpp`: plain Win32 (comctl32 v6 manifest, DPI aware), runs next to openassault.exe.
  Functions: Play, Language (the `lang` folders + `@name`), HD textures and Fullscreen. They are written to the keys
  LANGUAGE / HDTEXTURES / FULLSCREEN of `<exe>\SAVEGAME\OPENASSAULT.CFG`; the other lines are kept.
  Create translation (`--export-lang`), Export textures (`--extract <parent of lang>\textures`), opening folders and the
  guide. It runs openassault.exe hidden with `XA_NOBOX=1` and shows its own message from the exit code.
- Its texts are English and are translated together with the game's: `lang/<CODE>/PORT.TXT`. Their list is at the end
  of `lang::portStrings()`; new texts must be added there too.
- `deploy.py` also puts the launcher next to the exe and into the zip.
- UI test: start, `PrintWindow` screenshot, `BM_CLICK` on the buttons. Control ids: 100 Play, 101 Lang,
  102 HD, 103 Full, 104 Code, 105 MakeLang, 106 LangFolder, 107 Guide, 108 Extract, 109 HdFolder. Careful: the
  folder buttons open Explorer on the user's screen.

## HD texture pack
`tools/hdtex/`: `dump.cpp` writes the unique pages of the levels (1045 pages, 756 unique), `esr.py` runs the
realesr-general-x4v3 network with plain numpy (the `.pth` weights file comes from the Real-ESRGAN GitHub release),
`batch.py` enlarges 4x, then Lanczos to 512, JPEG 92. About 6 s per page on CPU; `batch.py <k> <n>` can be split into slices.
The pack is the `HDTEX\` folder in the game root; `HDTEX.TXT` marks its presence.

Own HD textures: `--extract <folder>` → `<hash>.png` + `INDEX.TXT` (all six levels); an edited picture goes to
`HDTEX\<hash>.png|tga|bmp|jpg` and is loaded **only with HD on** (the user's request: the original textures must not be
replaced without HD). Any size; alpha = transparency.
`XA_NOBOX=1` skips the message box. When testing, put only .png/.tga/.bmp files into HDTEX (do not overwrite the
pack's .jpg files), and delete them afterwards.
