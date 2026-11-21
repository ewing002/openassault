"""Makes the release zip: the programs and the translations only, never game data.

usage: py tools/package.py <build folder> [out.zip, default: OpenAssault-release.zip in the build folder]
Contains OpenAssault/openassault.exe, OpenAssault/OpenAssaultLauncher.exe, OpenAssault/lang/ (all translations),
OpenAssault/README.txt (how to install) and the licence-free notes. The game data (DATA, DATA.SFX, MUSIC, HDTEX)
is not included: the player copies it from their own Extreme Assault installation.
"""
import os, sys, zipfile

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # openassault-src
build = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(build, "OpenAssault-release.zip")
files = ["openassault.exe", "OpenAssaultLauncher.exe"]
for f in files:
    if not os.path.exists(os.path.join(build, f)):
        sys.exit(f"missing {f} in {build}")

INSTALL = """OpenAssault - a Blue Byte Extreme Assault (1997) port
=====================================================

Requirements: your own copy of Extreme Assault. The programs do not contain any game data. The game folder needs:
  DATA        required
  DATA.SFX    required
  MUSIC       optional: without it the game has no music. If present it must hold the CD audio of the GOG
              release as OGG files named track_NN.ogg (track_02.ogg ... track_20.ogg)
  HDTEX       optional: HD textures

1. Copy the OpenAssault folder next to your Extreme Assault folder, or into it.
   The game folder is the one that contains DATA\\ (the program searches upwards for it).
2. Start OpenAssaultLauncher.exe: Play, language (Hungarian by default), HD textures, fullscreen.
3. Translations: see lang\\README.TXT. Textures: the launcher exports them as PNG; put edited copies into
   the HDTEX folder (they are used when HD textures are on).

Source code and build instructions: see the project repository (README.md).
"""

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for f in files:
        z.write(os.path.join(build, f), "OpenAssault/" + f)
    langsrc = os.path.join(here, "lang")
    for base, _, names in os.walk(langsrc):
        for n in names:
            p = os.path.join(base, n)
            z.write(p, "OpenAssault/lang/" + os.path.relpath(p, langsrc).replace(os.sep, "/"))
    z.writestr("OpenAssault/README.txt", INSTALL)
print("packaged", out)
