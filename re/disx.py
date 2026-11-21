import capstone,sys
img=open(__import__('os').path.join(__import__('os').path.dirname(__file__),'xa3dfx.img'),'rb').read()
a=int(sys.argv[1],16);b=int(sys.argv[2],16)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.skipdata=True
for i in md.disasm(img[a:b],a): print('%x: %-8s %s'%(i.address,i.mnemonic,i.op_str))
