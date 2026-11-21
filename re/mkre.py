"""Builds the reverse engineering files from your own copy of the game (they are not in the repository).

usage: py re/mkre.py [path\\to\\XA_3DFX.EXE, default: the game folder above openassault-src]
Writes re/xa3dfx.img (the LE objects at their linear addresses, file offset = address; re/flat.py) and the
capstone listings re/obj2.asm (code object), re/obj3.asm (object 3) and re/visn.asm (the VISN codec in object 3).
Needs: pip install capstone
"""
import os, sys
import capstone

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import flat

exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'XA_3DFX.EXE')
img, objs, eip, esp, nfix = flat.load(exe)
open(os.path.join(here, 'xa3dfx.img'), 'wb').write(img)
print('xa3dfx.img', len(img), 'bytes,', nfix, 'fixups')

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.skipdata = True
def listing(name, a, b):
    with open(os.path.join(here, name), 'w', newline='\n') as f:
        for i in md.disasm(bytes(img[a:b]), a):
            f.write('%x: %-8s %s\n' % (i.address, i.mnemonic, i.op_str))
    print(name, hex(a), hex(b))

o2, o3 = objs[1], objs[2]        # [virtual size, base, ...]
listing('obj2.asm', o2[1], o2[1] + o2[0])
listing('obj3.asm', o3[1], o3[1] + o3[0])
listing('visn.asm', 0xe6d20, o3[1] + o3[0])
