# OpenAssault

A native rewrite of Blue Byte's **Extreme Assault** (1997) for modern Windows and Linux, using OpenGL 3.3.
It does not use DOSBox and does not emulate anything. The program reads the original game data
(`DATA\LEVELxx`) and draws it with its own engine.

**The game data is not included.** You need your own copy of Extreme Assault; the programs read its files at run time.

## Requirements
The program contains no game data; you need your own copy of Extreme Assault. The game folder must contain:
- `DATA` – required (levels, models, textures, texts).
- `DATA.SFX` – required (sounds and speech).
- `MUSIC` – optional. Without it the game runs without music. If it is there, the program expects the CD audio of the
  GOG release in it as OGG files named `track_NN.ogg` (two-digit number: `track_02.ogg` … `track_20.ogg`; the case does not
  matter). Which track plays in which level is given by the `CDAUDIOTRACKS` line of the game's `INIT.CFG`.
- `HDTEX` – optional, the HD texture pack.

## Status
The 13-step plan and the extras are done. The detailed list, the translation system, the testing method and the
reverse-engineering workflow are in `CLAUDE.md`.

## Running
Put `openassault.exe` into the game folder or a subfolder of it (e.g. `...\Extreme Assault\openassault\bin\`).
```
openassault.exe [--level 1..6] [--mission 1..98] [--fullscreen] [--size 1920x1080] [--game <game folder>] [--nomenu]
```
The easiest way is `OpenAssaultLauncher.exe` (Windows): it starts the game with a button, and sets the language,
HD textures, fullscreen, the window size and the fullscreen resolution (Desktop = the desktop's resolution). It also creates a
new translation template and exports the textures, without a command line. In the game the same settings are on the
Options > Graphic Detail page (`OPENASSAULT.CFG`: `WINDOWSIZE=1280x720`, `FULLSCREENSIZE=0x0`); the `--size` switch
overrides the window size once.
Without parameters the main menu starts (arrows/mouse + Enter); `--level` / `--mission` start a mission directly.
Texts are shown in Hungarian by default (the `lang` folder next to the exe); English can be selected under
Options > Language.

- **Translations:** `openassault.exe --export-lang DE` writes `lang\DE\` with the original English texts read from
  your game data. The steps are in `lang\README.TXT`.
- **Textures:** `openassault.exe --extract <folder>` writes the level textures as PNG (`<hash>.png`, list in
  `INDEX.TXT`). An edited or upscaled picture with the same name goes into the game's `HDTEX\` folder; it is used only
  when HD textures are on. Any size of PNG/JPG/TGA/BMP works; the alpha channel is the transparency.

## Controls
The default layout. The keys, mouse buttons and controller buttons can be changed on the Options > Controls Setup page
(the stick assignment and inversion too).

| Function | Keyboard | Mouse | Controller |
|---|---|---|---|
| Forward / back | W, Up / S, Down | | left stick up/down, D-pad up/down |
| Turn | Left, Right | move the mouse horizontally | right stick left/right, D-pad left/right |
| Drift (sideways) | A / D | | left stick left/right |
| Climb / descend | Space, E / Ctrl, Q | | right stick up/down, RB / LB |
| Boost | Shift | | press the left stick (L3) |
| Fire (the guns shoot forward) | F | left button | RT |
| Change weapon | Tab, 1 / 2 / 3 | wheel | Y |
| Guided rocket (at the selected target) | R | right button | LT |
| Smart bomb / thunderbolt | B / T | | B / X |
| Lock target (two buttons) | G / H | middle button | R3 / L3 (press the stick) |
| Cockpit / external view | F2, V | | Back |

- **Mouse:** moving it horizontally turns the helicopter, and it accelerates and stops like the controller's stick. It does not aim
  vertically: there is no free aiming in the original either, the weapons aim at the selected target on their own within ±13°.
- **External view:** look around: hold Alt (the A button on the controller) and move the mouse (right stick); it returns when released.
  Fixed sides: F3 steps through behind, right, front, left; Alt (or A) + arrow (or D-pad) picks one directly.
- **Menus:** arrows / mouse + Enter, Esc goes back; on the controller the left stick or D-pad moves, A = Enter, B = back. Questions are
  answered with I or Y (yes), N (no); on the controller with A / B.
- **In the game:** Esc = abort the mission (a question), Start on the controller. The briefing is skipped with Space, Esc, Enter or a
  mouse click (A on the controller).
- **Other keys:** F1 free camera (WASD, Q/E, right mouse button to look around), F6 view range, F7 / F8 decrease / increase the
  view-distance multiplier, F12 screenshot, Alt+Enter fullscreen / window.

## Building
Windows, one click: `build.bat` (checks the tools, offers to install the missing ones, and puts the finished programs
into `dist\OpenAssault\`).

Windows (Visual Studio 2022 or MinGW): `cmake -B build && cmake --build build --config Release`
(CMake downloads SDL2 automatically if it is not installed).
Linux: `sudo apt install libsdl2-dev cmake g++`, then the same commands.
Windows exe on Linux: `cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_PREFIX_PATH=<SDL2-mingw>/x86_64-w64-mingw32`.

