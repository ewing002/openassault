import unicorn as U, unicorn.x86_const as X, struct, sys
from PIL import Image
img=open('/root/xa/xa3dfx.img','rb').read()
class Dec:
    def __init__(s):
        m=U.Uc(U.UC_ARCH_X86,U.UC_MODE_32); s.m=m
        m.mem_map(0,0x1000000); m.mem_write(0,img)
        m.mem_write(0x7f0000,b'\xd0\xea\x0d\x00'); m.reg_write(X.UC_X86_REG_ESP,0x7f0000); m.reg_write(X.UC_X86_REG_EAX,0x565); m.emu_start(0xe82b0,0xdead0)
    def run(s,data,w=256,entry=0xe6d2c):
        m=s.m; SRC=0x800000; DST=0xa00000; STK=0x7f0000
        m.mem_write(SRC,data+b'\0'*64)
        m.mem_write(DST,b'\0'*(w*w*4))
        m.mem_write(STK,struct.pack('<I',0xdead0))
        m.reg_write(X.UC_X86_REG_ESP,STK); m.reg_write(X.UC_X86_REG_EAX,w)
        m.reg_write(X.UC_X86_REG_ESI,SRC); m.reg_write(X.UC_X86_REG_EDI,DST)
        m.emu_start(entry,0xdead0,count=200000000)
        return bytes(m.mem_read(DST,w*w*2))
def to_png(raw,w,h,path):
    import numpy as np
    a=np.frombuffer(raw[:w*h*2],dtype='<u2').reshape(h,w)
    r=((a>>11)&31)*255//31; g=((a>>5)&63)*255//63; b=(a&31)*255//31
    Image.fromarray(np.dstack([r,g,b]).astype('uint8')).save(path)
if __name__=='__main__':
    lib=open('/mnt/user-data/uploads/t/DATA/LEVEL01/TEXTURES/TEXTURE.LIB','rb').read()
    d=Dec()
    for l in open('/mnt/user-data/uploads/t/DATA/LEVEL01/TEXTURES/TEXTURE.DIR'):
        p=l.split()
        if len(p)==6 and p[0] in sys.argv[1:]:
            o,sz=int(p[3]),int(p[4])
            raw=lib[o:o+sz]
            out=d.run(raw) if raw[:4]==b'VISN' else raw
            to_png(out,256,256,'/root/xa/'+p[0]+'.png'); print('ok',p[0])
