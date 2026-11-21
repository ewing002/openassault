import os, sys, numpy as np, esr
from PIL import Image
src, dst = sys.argv[3] if len(sys.argv) > 3 else 'in', sys.argv[4] if len(sys.argv) > 4 else 'out'; os.makedirs(dst, exist_ok=True)
files = sorted(f for f in os.listdir(src) if f.endswith('.tga'))
if len(sys.argv) > 2: files = files[int(sys.argv[1])::int(sys.argv[2])]
for n, f in enumerate(files):
    o = os.path.join(dst, f[:-4] + '.jpg')
    if os.path.exists(o): continue
    im = np.asarray(Image.open(os.path.join(src, f)).convert('RGB'), np.float32) / 255
    out = Image.fromarray((esr.upscale(im) * 255 + 0.5).astype(np.uint8)).resize((512, 512), Image.LANCZOS)
    out.save(o + '.tmp.jpg', quality=92, subsampling=0); os.replace(o + '.tmp.jpg', o)
    print(n, f, flush=True)
