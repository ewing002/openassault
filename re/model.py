import struct,sys
class Err(Exception): pass
def u16(b,p): return struct.unpack_from('<H',b,p)[0]
def s16(b,p): return struct.unpack_from('<h',b,p)[0]
def s32(b,p): return struct.unpack_from('<i',b,p)[0]
def parse_tree(b,p,nv,polys,depth=0):
    if depth>64: raise Err('deep')
    k=s16(b,p)
    if k==-1:
        nrm=s16(b,p+2); d=s32(b,p+4); size=u16(b,p+8)
        q=parse_tree(b,p+10,nv,polys,depth+1)
        q=parse_tree(b,q,nv,polys,depth+1)
        return q
    cnt=k; p+=2
    if cnt<0 or cnt>2000: raise Err('cnt %d'%cnt)
    for i in range(cnt):
        typ=u16(b,p); nrm=s16(b,p+2); d=s32(b,p+4)
        L=u16(b,p+8); col=u16(b,p+10); n=u16(b,p+12)
        if n<3 or n>32: raise Err('n %d at %x'%(n,p))
        idx=struct.unpack_from('<%dH'%n,b,p+14)
        if max(idx)>=nv: raise Err('idx %d nv %d typ %d'%(max(idx),nv,typ))
        uv=None;tex=None
        if L==14+2*n+4*n+4:
            uv=[struct.unpack_from('<HH',b,p+14+2*n+4*j) for j in range(n)]
            tex=struct.unpack_from('<I',b,p+14+6*n)[0]
        elif L==14+4*n+2:
            uv=None; tex=None
        elif L!=14+2*n: raise Err('L %d n %d typ %d at %x'%(L,n,typ,p))
        polys.append(dict(typ=typ,col=col,idx=idx,uv=uv,tex=tex,nrm=nrm))
        p+=L
    return p
def parse(b):
    p=0x32; parts=[]; cur=None; hdr=[]
    while True:
        t=u16(b,p)
        if t==0xffff: break
        v=s32(b,p+2)
        if t==0: cur={}; parts.append(cur)
        if cur is None: hdr.append((t,v)); p+=6; continue
        cur.setdefault(t,[]).append(p+2+v if t not in(0x48,) else v)
        p+=6
        if p>len(b): raise Err('chunk overrun')
    allv=[]; polys=[]; trees=[]
    for part in parts:
        if 0 in part:
            vp=part[0][0]; nv=u16(b,vp)
            part['vbase']=len(allv)
            allv+= [struct.unpack_from('<3i',b,vp+2+12*i) for i in range(nv)]
        trees+=part.get(0x14,[])
    for pp in trees: parse_tree(b,pp,len(allv),polys)
    res=dict(verts=allv,polys=polys,parts=parts,hdr=hdr)
    return res
def load_lib(dirpath):
    din=open(dirpath+'/PROTOTYP.DIN','rb').read(); b=open(dirpath+'/PROTOTYP.BIN','rb').read()
    ents=[(din[i:i+14].split(b'\0')[0].decode(),struct.unpack_from('<I',din,i+14)[0]) for i in range(0,len(din),18)]
    offs=[e[1] for e in ents]+[len(b)]
    return {n:b[o:offs[k+1]] for k,(n,o) in enumerate(ents)}
if __name__=='__main__':
    lib=load_lib(sys.argv[1]); ok=0; import collections; errs=collections.Counter(); tags=collections.Counter()
    for n,b in lib.items():
        try:
            r=parse(b); ok+=1
            tags[len(r['parts'])]+=1
        except Exception as e: errs[str(e)[:20]]+=1; print(n,e)
    print(ok,len(lib),errs.most_common(10)); print(tags.most_common(10))
