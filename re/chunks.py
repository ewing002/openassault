import struct,sys
L='/mnt/user-data/uploads/t/DATA/LEVEL01/OBJECTS'
din=open(L+'/PROTOTYP.DIN','rb').read(); B=open(L+'/PROTOTYP.BIN','rb').read()
ents=[(din[i:i+14].split(b'\0')[0].decode(),struct.unpack_from('<I',din,i+14)[0]) for i in range(0,len(din),18)]
idx={n:i for i,(n,o) in enumerate(ents)}
def chunks(o):
    p=o+0x32; out=[]
    while True:
        t=struct.unpack_from('<H',B,p)[0]
        if t==0xffff: break
        v=struct.unpack_from('<i',B,p+2)[0]
        out.append((p,t,v,p+2+v)); p+=6
    return out
if __name__=='__main__':
  for n in sys.argv[1:]:
    o=ents[idx[n]][1]
    print('==',n,hex(o))
    for p,t,v,tg in chunks(o):
        extra=''
        if t in(0x40,0x44): extra=B[tg:tg+40].hex()
        if t==0: extra='nv=%d'%struct.unpack_from('<H',B,tg)[0]
        print('  %04x tag %02x val %d tgt %x %s'%(p-o,t,v,tg-o,extra))
