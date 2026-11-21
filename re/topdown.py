import sys,struct; sys.argv=['x']
exec(open('/root/xa/model.py').read().split("if __name__")[0])
from PIL import Image, ImageDraw
L='/mnt/user-data/uploads/t/DATA/LEVEL01'
lib=load_lib(L+'/OBJECTS')
pd=[l.split() for l in open(L+'/OBJECTS/PROTOTYP.DIR').read().split('\n') if len(l.split())==5]
w=open(L+'/SCAPES/WORLD.BIN','rb').read()
dl=[l.split() for l in open(L+'/SCAPES/WORLD.DIR').read().split('\n') if len(l.split())==3]
offs=[int(x[2]) for x in dl]+[len(w)]
def recs(name):
    k=[i for i,x in enumerate(dl) if x[0]==name][0]
    o=int(dl[k][2]); n=(offs[k+1]-o)//38
    return [struct.unpack_from('<IIIIiii5h',w,o+38*i) for i in range(n)]
tris=[]
for which in sys.argv_extra:
  for r in recs(which):
    name=pd[r[2]][0]
    try: m=parse(lib[name])
    except Exception as e: print('skip',name,e); continue
    V=m['verts']
    for p in m['polys']:
        pts=[(V[i][0]+r[4],V[i][1]+r[5],V[i][2]+r[6]) for i in p['idx']]
        tris.append(pts)
xs=[p[0] for t in tris for p in t]; ys=[p[1] for t in tris for p in t]; zs=[p[2] for t in tris for p in t]
print(len(tris),min(xs),max(xs),min(ys),max(ys),min(zs),max(zs))
S=1024; mn=min(min(xs),min(ys)); mx=max(max(xs),max(ys)); sc=(S-1)/(mx-mn)
img=Image.new('RGB',(S,S)); d=ImageDraw.Draw(img)
zmin,zmax=min(zs),max(zs)
for t in sorted(tris,key=lambda t:sum(p[2] for p in t)):
    z=sum(p[2] for p in t)/len(t); c=int(255*(z-zmin)/(zmax-zmin+1))
    d.polygon([((p[0]-mn)*sc,S-1-(p[1]-mn)*sc) for p in t],fill=(c,c,c),outline=(255,0,0))
img.save(sys.argv_out)
