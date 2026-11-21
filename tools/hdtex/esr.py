# Real-ESRGAN realesr-general-x4v3 (SRVGGNetCompact) inference in plain numpy
import zipfile, pickle, numpy as np, sys
def load_pth(path):
    z = zipfile.ZipFile(path); root = z.namelist()[0].split('/')[0]
    class Storage:
        def __init__(s, key, dtype): s.key = key; s.dtype = dtype
    def rebuild(storage, offset, size, stride, *a):
        buf = np.frombuffer(z.read(f'{root}/data/{storage.key}'), dtype=storage.dtype)
        n = int(np.prod(size)) if len(size) else 1
        arr = np.lib.stride_tricks.as_strided(buf[offset:], shape=size, strides=[x * buf.itemsize for x in stride]) if len(size) else buf[offset:offset+1]
        return np.array(arr, dtype=np.float32)
    class U(pickle.Unpickler):
        def find_class(s, mod, name):
            if name == '_rebuild_tensor_v2': return rebuild
            if name == 'FloatStorage': return np.float32
            if name == 'HalfStorage': return np.float16
            if mod == 'collections' and name == 'OrderedDict':
                import collections; return collections.OrderedDict
            if name == '_rebuild_parameter': return lambda d, rg, h: d
            return super().find_class(mod, name)
        def persistent_load(s, pid):
            _, st, key, loc, n = pid
            dt = st if isinstance(st, type) else np.float32
            return Storage(key, dt)
    return U(z.open(f'{root}/data.pkl')).load()

sd = load_pth(__import__('os').path.join(__import__('os').path.dirname(__file__),'realesr-general-x4v3.pth'))
if 'params' in sd: sd = sd['params']
keys = sorted({k.rsplit('.',1)[0] for k in sd}, key=lambda k: int(k.split('.')[1]))
layers = [(k, sd[k+'.weight'], sd.get(k+'.bias')) for k in keys]

def conv(x, w, b):            # x HxWxC, w O,I,3,3
    H, W, C = x.shape
    p = np.pad(x, ((1,1),(1,1),(0,0)), mode='edge' if False else 'constant')
    cols = np.empty((H, W, 9, C), np.float32)
    for i in range(3):
        for j in range(3): cols[:, :, i*3+j, :] = p[i:i+H, j:j+W, :]
    wm = w.transpose(2, 3, 1, 0).reshape(9*C, -1)
    return (cols.reshape(H*W, 9*C) @ wm).reshape(H, W, -1) + (b if b is not None else 0)

def upscale(img):              # img HxWx3 float 0..1 -> 4x
    x = img.astype(np.float32)
    i = 0
    while i < len(layers):
        k, w, b = layers[i]
        if w.ndim == 4:
            x = conv(x, w, b)
            if i + 1 < len(layers) and layers[i+1][1].ndim == 1:   # PReLU
                a = layers[i+1][1]; x = np.where(x >= 0, x, x * a); i += 1
        i += 1
    H, W, C = x.shape          # pixel shuffle 4 (C = 3*16, torch order c,r1,r2)
    x = x.reshape(H, W, 3, 4, 4).transpose(0, 3, 1, 4, 2).reshape(H*4, W*4, 3)
    base = np.repeat(np.repeat(img, 4, 0), 4, 1)
    return np.clip(x + base, 0, 1)

if __name__ == '__main__':
    from PIL import Image
    im = np.asarray(Image.open(sys.argv[1]).convert('RGB'), np.float32) / 255
    import time; t = time.time(); out = upscale(im); print('time', time.time() - t)
    Image.fromarray((out * 255 + 0.5).astype(np.uint8)).save(sys.argv[2])
