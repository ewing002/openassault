import struct,sys,collections
d=open(sys.argv[1],'rb').read()
off=struct.unpack_from('<I',d,0x3c)[0]
# DOS4GW bound: find 'LE' signature
i=d.find(b'LE\x00\x00')
print('LE at',hex(i),'e_lfanew',hex(off))
le=i
h=lambda o,f='<I':struct.unpack_from(f,d,le+o)[0]
print('cpu',h(8,'<H'),'os',h(0xa,'<H'),'pages',h(0x14),'eip obj',h(0x18),'eip',hex(h(0x1c)),'esp obj',h(0x20),'pagesize',h(0x28),'lastpage',h(0x2c))
nobj=h(0x44); objtab=h(0x40)
print('objs',nobj,'fixup',hex(h(0x30)),'imp mod cnt',h(0x70),'impmod',hex(h(0x6c)))
for k in range(nobj):
    vs,base,fl,pi,pc,_=struct.unpack_from('<6I',d,le+objtab+k*24)
    print(k+1,hex(vs),hex(base),hex(fl),pi,pc)
# import modules
im=h(0x6c);n=h(0x70);p=le+im
for k in range(n):
    l=d[p];print('import',d[p+1:p+1+l]);p+=1+l
