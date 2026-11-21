"""Checks a translation in lang/<code>/ against the original game texts.

usage: py tools/langcheck.py [code, default HU] [game root, default: the folder above openassault-src]
Reports original [n]{text} entries without a translation, translations of unknown ids, characters the fonts
cannot show and [SCROLLTEXT] mission texts of 128 characters or more (the ticker drops those, 0x35be3).
"""
import glob, os, re, sys

code = sys.argv[1] if len(sys.argv) > 1 else 'HU'
here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
root = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(here)
langdir = os.path.join(here, 'lang', code)
OK = set(chr(c) for c in range(32, 127)) | set('áéíóöőúüűÁÉÍÓÖŐÚÜŰäÄßèÈ®„”“’‘–—…')

def table(text):
    text = '\n'.join(l for l in text.splitlines() if not l.lstrip().startswith(';'))
    return {int(m.group(1)): m.group(2).replace('\n', ' ') for m in re.finditer(r'\[(\d+)\]\{(.*?)\}', text, re.S)}

def original(path):
    d = open(path, 'rb').read()
    return table((bytes(255 - b for b in d) if d[:1] >= b'\x80' else d).decode('cp850'))

problems = 0
for tr_path in sorted(glob.glob(os.path.join(langdir, '**', '*.CHR'), recursive=True)):
    rel = os.path.relpath(tr_path, langdir)
    orig = original(os.path.join(root, rel))
    tr = table(open(tr_path, encoding='utf-8-sig').read())
    missing = [i for i, t in orig.items() if t.strip() and i not in tr]
    unknown = [i for i in tr if i not in orig]
    bad = sorted(set(c for t in tr.values() for c in t if c not in OK))
    print(f'{rel}: {len(tr)} of {len(orig)} entries')
    if missing: print('  untranslated:', missing)
    if unknown: print('  unknown ids:', unknown); problems += 1
    if bad: print('  characters without a glyph:', ''.join(bad)); problems += 1
    m = re.match(r'DATA\\(LEVEL\d\d)\\MISSIONS\\MISSION.CHR', rel)
    if m:
        ids = set()
        for scr in glob.glob(os.path.join(root, 'DATA', m.group(1), 'MISSION*', '*.SCR')):
            d = open(scr, 'rb').read()
            s = (bytes(255 - b for b in d) if d[:1] >= b'\x80' else d).decode('cp850')
            ids |= {int(x) for x in re.findall(r'\[SCROLLTEXT\]\s*(\d+)', s)}
        for i in sorted(ids):
            if i in tr and len(tr[i]) >= 128:
                print(f'  ticker text {i} is {len(tr[i])} characters (max 127)'); problems += 1

# PORT.TXT keys against lang::portStrings() in src/core/lang.cpp
src = open(os.path.join(here, 'src', 'core', 'lang.cpp'), encoding='utf-8').read()
block = re.search(r'portStrings\(\) \{(.*?)\};', src, re.S).group(1)
known = re.findall(r'"([^"]*)"', block)
keys = {}
port = os.path.join(langdir, 'PORT.TXT')
if os.path.exists(port):
    for line in open(port, encoding='utf-8-sig'):
        line = line.strip()
        if line and not line.startswith('#') and ' = ' in line:
            k, v = line.split(' = ', 1)
            keys[k.strip()] = v.strip()
missing = [k for k in known if k not in keys]
unknown = [k for k in keys if k not in known and k != '@name']
bad = sorted(set(c for v in keys.values() for c in v if c not in OK))
print(f'PORT.TXT: {len(keys)} lines, {len(known)} port strings')
if missing: print('  untranslated:', missing)
if unknown: print('  unknown keys:', unknown); problems += 1
if bad: print('  characters without a glyph:', ''.join(bad)); problems += 1
print('problems:', problems)
sys.exit(1 if problems else 0)
