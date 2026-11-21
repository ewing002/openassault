import struct,sys,collections,capstone
d=open(sys.argv[1],'rb').read()
le=d.find(b'LE\x00\x00')
h=lambda o,f='<I':struct.unpack_from(f,d,le+o)[0]
nobj=h(0x44); objtab=h(0x40); dpo=h(0x80); ps=h(0x28)
n=h(0x74); p=le+h(0x70); mods=[]
for k in range(n):
    l=d[p];mods.append(d[p+1:p+1+l]);p+=1+l
print('import modules',mods)
objs=[]
for k in range(nobj):
    vs,base,fl,pi,pc,_=struct.unpack_from('<6I',d,le+objtab+k*24); objs.append((vs,base,fl,pi,pc))
vs,base,fl,pi,pc=objs[1]
code=d[dpo+(pi-1)*ps:dpo+(pi-1+pc)*ps][:vs]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.skipdata=True
c=collections.Counter(); ints=collections.Counter()
for ins in md.disasm(code,base):
    m=ins.mnemonic
    if m in('int','in','out','ins','outs','insb','outsb','insw','outsw','insd','outsd','cli','sti','lgdt','iretd','iret','hlt'):
        c[m]+=1
        if m=='int': ints[ins.op_str]+=1
    if m.startswith('mov') and ('ds' in ins.op_str.split(',')[0] or 'es' in ins.op_str.split(',')[0] or 'fs' in ins.op_str.split(',')[0] or 'gs' in ins.op_str.split(',')[0]) and ins.op_str.split(',')[0].strip() in ('ds','es','fs','gs'):
        c['segload']+=1
print(c); print(ints.most_common(20))
