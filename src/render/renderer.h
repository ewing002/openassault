#pragma once
#include "glapi.h"
#include "math.h"
#include "../assets/model.h"
#include "../assets/textures.h"
#include "../assets/world.h"
#include "../assets/typ.h"
#include "../game/pose.h"
#include "../assets/sprites.h"
#include <vector>
#include <string>
#include <set>

namespace xa {

class Renderer {
public:
    static constexpr int kMaxParts = 64;
    struct Instance {
        int model = 0; int objClass = -1; Vec3 pos; float radius = 1; Mat4 rot; std::string set;
        int worldIndex = -1; bool hidden = false;
        float rotorTicks = -1;          // per-instance rotor clock (player helicopter)
        float animTicks = -1;
        bool aim = false; float aimYaw = 0, aimPitch = 0;   // turret aiming (relative to the hull)
        int16_t vars[32] = {}; int nVars = 0;   // object angle words ([+0x1c], 3 per VARIABLE_OFFSET/2) set by behaviour code           // >= 0: frozen/own animation clock for all parts (wrecks, crashing units)
    };
    struct Extra { int model; int objClass; Vec3 pos; Mat4 rot; };        // dynamic objects (projectiles)
    struct Sprite { Vec3 pos; float size; int layer; float alpha; };        // additive billboard frames
    struct HudQuad {                                                      // pixels, origin top-left
        float x0, y0, x1, y1; uint8_t rgba[4];
        int font = -1; float u0 = 0, v0 = 0, u1 = 0, v1 = 0;                  // optional font texture
    };
    int uploadFont(const uint8_t* rgba, int w, int h);                     // returns font texture index
    bool init(std::string& err);
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    ~Renderer();                                                          // frees the GL objects (level change)
    void uploadSprites(const SpriteLib& sprites);
    // hd: HD texture pack / 2x upscale (512x512 layers, Options > Graphic Detail > HD textures)
    void uploadTextures(const TextureLib& tex, const GameFS* fs = nullptr, bool hdOn = false);   // HDTEX of fs when hdOn
    void uploadModels(const ModelLib& lib);
    void setWorld(const World& world, const ModelLib& lib, const TypeDB& types);
    int addInstance(int model, int objClass, Vec3 pos, float yawRad);
    void clearInstances() { instances_.clear(); }
    Instance* instanceForWorldObject(int worldIndex);
    std::vector<Instance>& instances() { return instances_; }
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;   // optional sub viewport for draw() (GL coordinates; vpW 0 = whole window)
    void draw(const Mat4& proj, const Mat4& view, Vec3 eye, int w, int h, float timeSec,
              const std::vector<Extra>* extras = nullptr, const std::vector<Sprite>* sprites = nullptr);
    void drawHud(const std::vector<HudQuad>& quads, int w, int h);

    std::set<std::string> hiddenSets;
    float unitScale = 1.0f / 1000.0f;   // game units -> GL units
    float ticksPerSecond = 60.0f;       // ROTATE_PART speeds are per game tick, 4096 = full turn
    float lodPixels[2] = {70.f, 20.f};  // projected radius thresholds for LOD 0 / 1
    float viewDistance = 20000.f;
    float skyDistance = 3000.f, skyDrop = 0.f;   // sky dome scale (must stay inside the far plane)
    float fogColor[3] = {0.55f, 0.65f, 0.78f}, fogNear = 300.f, fogFar = 4000.f;   // linear fog (GL units), set by scape switches
    int drawnLastFrame = 0;
    float currentTicks = 0;             // animation clock of the last draw (game ticks)
private:
    struct Range { int first = 0, opaque = 0, transparent = 0; };
    struct Item { int geo; Mat4 xf; int partOffset; float dist; };
    void submit(const Item& it, bool transparent);
    void addItem(int model, const ObjectType* t, Vec3 pos, float radius, const Mat4& rot, float rotorTicks, float animTicks, const Instance* inst,
                 Vec3 eye, float focal, const Mat4& camBasis, float timeSec);
    GLuint spriteProg_ = 0, spriteVao_ = 0, spriteVbo_ = 0, spriteTex_ = 0;
    GLint suVP_ = -1, suTex_ = -1, suEye_ = -1, suFog_ = -1;
    GLuint hudProg_ = 0, hudVao_ = 0, hudVbo_ = 0;
    GLint huTex_ = -1;
    std::vector<GLuint> fontTex_;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, texArray_ = 0;
    GLint uVP_ = -1, uModel_ = -1, uParts_ = -1, uEye_ = -1, uSun_ = -1, uFog_ = -1, uTex_ = -1, uFogColor_ = -1, uLit_ = -1;
    std::vector<Range> ranges_;
    std::vector<Instance> instances_;
    std::vector<Item> items_;
    std::vector<Mat4> partPool_;
    const ModelLib* lib_ = nullptr;
    Vec3 camFwd_;
    const TypeDB* types_ = nullptr;
    int texLayers_ = 0;
};

} // namespace xa
