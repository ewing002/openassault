# HD textúracsomag készítése
1. `dump.cpp` fordítása az `xacore`-ral: `g++ -O2 -std=c++17 -I../../src dump.cpp <build>/libxacore.a $(sdl2-config --cflags --libs) -o dump`
2. `./dump <játék mappa> in` – a pályák egyedi 256×256-os lapjai TGA-ként, a nevük a lap FNV-1a hash-e.
3. Súlyfájl ide: `realesr-general-x4v3.pth` (https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth)
4. `python3 batch.py [k n] [in] [out]` – 4x Real-ESRGAN (numpy), Lanczos 512-re, JPEG 92. Két szelet párhuzamosan:
   `OMP_NUM_THREADS=1 python3 batch.py 0 2 in out & OMP_NUM_THREADS=1 python3 batch.py 1 2 in out`
5. A kész `out\*.jpg` + egy `HDTEX.TXT` a játék gyökerében lévő `HDTEX\` mappába kerül.
