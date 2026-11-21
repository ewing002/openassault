import sys,subprocess; sys.argv=['x']
exec(open('/root/xa/visn_emu.py').read().split("if __name__")[0])
lib=open('/mnt/user-data/uploads/t/DATA/LEVEL01/TEXTURES/TEXTURE.LIB','rb').read()
d=Dec(); tot=0; bad=0; maxdiff=0
for l in open('/mnt/user-data/uploads/t/DATA/LEVEL01/TEXTURES/TEXTURE.DIR'):
    p=l.split()
    if len(p)==6:
        o,sz=int(p[3]),int(p[4]); raw=lib[o:o+sz]
        if raw[:4]!=b'VISN': continue
        ref=d.run(raw)
        open('/tmp/v.bin','wb').write(raw)
        r=subprocess.run(['/tmp/visn_test','/tmp/v.bin','/tmp/o.bin'])
        mine=open('/tmp/o.bin','rb').read() if r.returncode==0 else b''
        tot+=1
        if mine!=ref:
            bad+=1
            nd=sum(1 for i in range(0,min(len(mine),len(ref)),2) if mine[i:i+2]!=ref[i:i+2])
            print('DIFF',p[0],len(mine),nd)
print(tot,'textures',bad,'mismatch')
