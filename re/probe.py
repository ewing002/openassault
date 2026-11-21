import sys; sys.argv=['x']
exec(open('visn_emu.py').read().split("if __name__")[0])
import unicorn as U, struct, collections
lib=open('/mnt/user-data/uploads/t/DATA/LEVEL01/TEXTURES/TEXTURE.LIB','rb').read()
d=Dec(); st=collections.Counter()
def hk(uc,addr,size,ud):
    if addr in (0xe71e0,0xe735a,0xe745b):
        al=uc.reg_read(X.UC_X86_REG_EAX)&0xff
        st[(hex(addr),'sym>=node' if (addr==0xe745b and al>=0x11) else 'ok')]+=1
    if addr in(0xe722e,0xe73a8,0xe747b):
        cl=uc.reg_read(X.UC_X86_REG_ECX)&0xff; al=uc.reg_read(X.UC_X86_REG_EAX)&0xff
        st[(hex(addr),'slow cl=%d'%cl)]+=1
    if addr==0xe7427: st[('count',uc.reg_read(X.UC_X86_REG_EBP)>>20)]+=1
d.m.hook_add(U.UC_HOOK_CODE,hk,begin=0xe7100,end=0xe7500)
n=0
for l in open('/mnt/user-data/uploads/t/DATA/LEVEL01/TEXTURES/TEXTURE.DIR'):
    p=l.split()
    if len(p)==6:
        o,sz=int(p[3]),int(p[4]); raw=lib[o:o+sz]
        if raw[:4]==b'VISN':
            d.run(raw); n+=1
            if n>=12: break
for k,v in sorted(st.items(),key=str): print(k,v)
print(bytes(d.m.mem_read(0x18a298,17)).hex())
