import struct,sys
def load(path):
    d=open(path,'rb').read()
    le=d.find(b'LE\x00\x00')
    h=lambda o,f='<I':struct.unpack_from(f,d,le+o)[0]
    nobj=h(0x44); objtab=h(0x40); dpo=h(0x80); ps=h(0x28); npages=h(0x14); last=h(0x2c)
    fpt=le+h(0x68); frt=le+h(0x6c)
    objs=[]
    for k in range(nobj):
        vs,base,fl,pi,pc,_=struct.unpack_from('<6I',d,le+objtab+k*24); objs.append([vs,base,fl,pi,pc])
    top=max(o[1]+o[0] for o in objs)
    img=bytearray(top+0x10000)
    for vs,base,fl,pi,pc in objs:
        for p in range(pc):
            pg=pi+p
            sz=last if pg==npages else ps
            off=dpo+(pg-1)*ps
            img[base+p*ps:base+p*ps+sz]=d[off:off+sz]
    nfix=0
    for vs,base,fl,pi,pc in objs:
        for p in range(pc):
            pg=pi+p
            a=struct.unpack_from('<I',d,fpt+(pg-1)*4)[0]; b=struct.unpack_from('<I',d,fpt+pg*4)[0]
            q=frt+a; e=frt+b
            while q<e:
                st=d[q]; fl=d[q+1]; q+=2
                if st&0x20:
                    cnt=d[q];q+=1
                else:
                    srcs=[struct.unpack_from('<h',d,q)[0]];q+=2
                if fl&3!=0: raise Exception('unsupported fixup flags %x'%fl)
                if fl&0x40: obj=struct.unpack_from('<H',d,q)[0];q+=2
                else: obj=d[q];q+=1
                if (st&0xf)==2: toff=0
                elif fl&0x10: toff=struct.unpack_from('<I',d,q)[0];q+=4
                else: toff=struct.unpack_from('<H',d,q)[0];q+=2
                if st&0x20:
                    srcs=[struct.unpack_from('<h',d,q+2*i)[0] for i in range(cnt)];q+=2*cnt
                tgt=objs[obj-1][1]+toff
                for s in srcs:
                    sa=base+p*ps+s
                    t=st&0xf
                    if t==7: struct.pack_into('<I',img,sa,tgt)
                    elif t==8: struct.pack_into('<i',img,sa,tgt-(sa+4))
                    elif t==2: struct.pack_into('<H',img,sa,obj)  # selector placeholder
                    elif t==6: struct.pack_into('<IH',img,sa,tgt,obj)
                    elif t==3: struct.pack_into('<HH',img,sa,toff&0xffff,obj)
                    elif t==5: struct.pack_into('<H',img,sa,toff&0xffff)
                    else: raise Exception('type %d'%t)
                    nfix+=1
    eip=objs[h(0x18)-1][1]+h(0x1c); esp=objs[h(0x20)-1][1]+h(0x24)
    return img,objs,eip,esp,nfix
if __name__=='__main__':
    img,objs,eip,esp,n=load(sys.argv[1])
    open(sys.argv[2],'wb').write(img); print(hex(eip),hex(esp),n,[ (hex(o[1]),hex(o[0])) for o in objs])
