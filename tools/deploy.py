"""Install a freshly built openassault.exe into the player's game folder.

usage: py tools/deploy.py <path\\to\\openassault.exe> [game root, default: the folder above openassault-src]
Copies the exe (and OpenAssaultLauncher.exe from the same build folder) to <root>\\openassault\\ and
<root>\\openassault\\bin\\, the translations (lang\\) to <root>\\openassault\\lang, and replaces them inside
<root>\\OpenAssault.zip (the distributable package: OpenAssault/openassault.exe, OpenAssaultLauncher.exe, lang/).
"""
import os, shutil, sys, zipfile, hashlib

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # openassault-src
exe = sys.argv[1]
root = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(here)
langsrc = os.path.join(here, "lang")
launcher = os.path.join(os.path.dirname(os.path.abspath(exe)), "OpenAssaultLauncher.exe")
programs = {"openassault.exe": open(exe, "rb").read()}
if os.path.exists(launcher):
    programs["OpenAssaultLauncher.exe"] = open(launcher, "rb").read()
for d in (os.path.join(root, "openassault"), os.path.join(root, "openassault", "bin")):
    os.makedirs(d, exist_ok=True)
    for name, data in programs.items():
        with open(os.path.join(d, name), "wb") as f:
            f.write(data)
dst = os.path.join(root, "openassault", "lang")
if os.path.isdir(langsrc):
    # overwrite only the shipped files: lang/ may also hold the player's own translations (--export-lang)
    shutil.copytree(langsrc, dst, dirs_exist_ok=True)
src = os.path.join(root, "OpenAssault.zip")
if os.path.exists(src):
    tmp = src + ".new"
    replaced = {"OpenAssault/" + n for n in programs}
    with zipfile.ZipFile(src) as a, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as b:
        for i in a.infolist():
            if i.filename.startswith("OpenAssault/lang/") or i.filename in replaced:
                continue
            b.writestr(i, a.read(i.filename), compress_type=i.compress_type)
        for name, data in programs.items():
            b.writestr("OpenAssault/" + name, data)
        for base, _, files in os.walk(langsrc):
            for f in files:
                p = os.path.join(base, f)
                b.write(p, "OpenAssault/lang/" + os.path.relpath(p, langsrc).replace(os.sep, "/"))
    os.replace(tmp, src)
print("deployed", ", ".join(f"{n} {hashlib.md5(d).hexdigest()}" for n, d in programs.items()), "to", root)
