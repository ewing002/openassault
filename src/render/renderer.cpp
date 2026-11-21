#include "renderer.h"
#include "../assets/hdtex.h"
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <algorithm>

namespace xa {
namespace {
const char* kVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aLayer;
layout(location=3) in vec4 aColor;
layout(location=4) in vec3 aNormal;
layout(location=5) in float aPart;
layout(location=6) in float aMode;
uniform mat4 uVP; uniform mat4 uModel; uniform mat4 uParts[64];
out vec2 vUV; flat out float vLayer; out vec4 vColor; out vec3 vN; out vec3 vWorld; flat out float vMode;
void main(){
  mat4 m = uModel * uParts[int(aPart + 0.5)];
  vec4 wp = m * vec4(aPos,1.0);
  vWorld = wp.xyz; vN = mat3(m) * aNormal;
  vUV = aUV; vLayer = aLayer; vColor = aColor; vMode = aMode;
  gl_Position = uVP * wp;
})";
// Modes: 0 opaque, 1 colour-keyed, 2 additive, 3 translucent, 4 shadow.
// Transparent modes output premultiplied alpha (blend ONE, ONE_MINUS_SRC_ALPHA).
const char* kFS = R"(#version 330 core
in vec2 vUV; flat in float vLayer; in vec4 vColor; in vec3 vN; in vec3 vWorld; flat in float vMode;
uniform sampler2DArray uTex; uniform vec3 uEye; uniform vec3 uSun; uniform vec2 uFog; uniform vec3 uFogColor; uniform float uLit;
out vec4 frag;
void main(){
  int mode = int(vMode + 0.5);
  vec4 c = vColor;
  if (vLayer >= 0.0) {
    vec4 t = texture(uTex, vec3(vUV, vLayer));
    if (mode == 1 && t.a < 0.5) discard;
    c = vec4(t.rgb * vColor.rgb, 1.0);
  }
  float d = length(vWorld - uEye);
  float f = clamp((d - uFog.x) / max(uFog.y - uFog.x, 1.0), 0.0, 1.0);
  if (mode == 2) { frag = vec4(c.rgb * (1.0 - f), 0.0); return; }
  if (mode == 4) { float lum = max(c.r, max(c.g, c.b)); frag = vec4(0.0, 0.0, 0.0, lum > 0.03 ? 0.45 * (1.0 - f) : 0.0); return; }
  vec3 n = normalize(vN);
  float l = mix(1.0, 0.55 + 0.45 * abs(dot(n, uSun)), uLit);
  vec3 col = mix(c.rgb * l, uFogColor, f);
  if (mode == 3) { frag = vec4(col * 0.5, 0.5); return; }
  frag = vec4(col, 1.0);
})";

const char* kSpriteVS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aUVL; layout(location=2) in float aAlpha;
uniform mat4 uVP; out vec3 vUVL; out float vAlpha; out vec3 vWorld;
void main(){ vUVL = aUVL; vAlpha = aAlpha; vWorld = aPos; gl_Position = uVP * vec4(aPos,1.0); })";
const char* kSpriteFS = R"(#version 330 core
in vec3 vUVL; in float vAlpha; in vec3 vWorld; uniform sampler2DArray uTex; uniform vec3 uEye; uniform vec2 uFog;
out vec4 frag;
void main(){ vec3 c = texture(uTex, vUVL).rgb; float f = clamp((length(vWorld-uEye) - uFog.x) / max(uFog.y - uFog.x, 1.0), 0.0, 1.0);
  frag = vec4(c * vAlpha * (1.0 - f), 0.0); })";
const char* kHudVS = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec4 aColor; layout(location=2) in vec3 aUV;
out vec4 vColor; out vec3 vUV;
void main(){ vColor = aColor; vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); })";
const char* kHudFS = R"(#version 330 core
in vec4 vColor; in vec3 vUV; uniform sampler2D uTex; out vec4 frag;
void main(){ vec4 c = vColor; if (vUV.z > 0.5) c *= texture(uTex, vUV.xy); if (c.a < 0.01) discard; frag = c; })";

GLuint compile(GLenum type, const char* src, std::string& err) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0; gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; gl::GetShaderInfoLog(s, sizeof log, nullptr, log); err = log; return 0; }
    return s;
}

struct Vtx { float pos[3]; float uv[2]; float layer; uint8_t color[4]; float n[3]; float part; float mode; };

void color565(uint16_t c, uint8_t* out) {
    out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
    out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
    out[2] = uint8_t((c & 31) * 255 / 31);
    out[3] = 255;
}
}

Renderer::~Renderer() {
    if (!prog_) return;
    for (GLuint t : fontTex_) gl::DeleteTextures(1, &t);
    GLuint tex[2] = {spriteTex_, texArray_}; gl::DeleteTextures(2, tex);
    GLuint bufs[3] = {vbo_, spriteVbo_, hudVbo_}; gl::DeleteBuffers(3, bufs);
    GLuint vaos[3] = {vao_, spriteVao_, hudVao_}; gl::DeleteVertexArrays(3, vaos);
    gl::DeleteProgram(prog_); gl::DeleteProgram(spriteProg_); gl::DeleteProgram(hudProg_);
}

bool Renderer::init(std::string& err) {
    GLuint vs = compile(gl::VERTEX_SHADER, kVS, err); if (!vs) return false;
    GLuint fs = compile(gl::FRAGMENT_SHADER, kFS, err); if (!fs) return false;
    prog_ = gl::CreateProgram();
    gl::AttachShader(prog_, vs); gl::AttachShader(prog_, fs); gl::LinkProgram(prog_);
    GLint ok = 0; gl::GetProgramiv(prog_, gl::LINK_STATUS, &ok);
    if (!ok) { char log[2048]; gl::GetProgramInfoLog(prog_, sizeof log, nullptr, log); err = log; return false; }
    uVP_ = gl::GetUniformLocation(prog_, "uVP"); uParts_ = gl::GetUniformLocation(prog_, "uParts");
    uModel_ = gl::GetUniformLocation(prog_, "uModel");
    uEye_ = gl::GetUniformLocation(prog_, "uEye"); uSun_ = gl::GetUniformLocation(prog_, "uSun");
    uLit_ = gl::GetUniformLocation(prog_, "uLit");
    uFog_ = gl::GetUniformLocation(prog_, "uFog"); uFogColor_ = gl::GetUniformLocation(prog_, "uFogColor"); uTex_ = gl::GetUniformLocation(prog_, "uTex");
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);

    auto link = [&](const char* vsrc, const char* fsrc, GLuint& out) {
        GLuint a = compile(gl::VERTEX_SHADER, vsrc, err); if (!a) return false;
        GLuint b = compile(gl::FRAGMENT_SHADER, fsrc, err); if (!b) return false;
        out = gl::CreateProgram(); gl::AttachShader(out, a); gl::AttachShader(out, b); gl::LinkProgram(out);
        GLint lk = 0; gl::GetProgramiv(out, gl::LINK_STATUS, &lk);
        if (!lk) { char log[2048]; gl::GetProgramInfoLog(out, sizeof log, nullptr, log); err = log; return false; }
        return true;
    };
    if (!link(kSpriteVS, kSpriteFS, spriteProg_) || !link(kHudVS, kHudFS, hudProg_)) return false;
    suVP_ = gl::GetUniformLocation(spriteProg_, "uVP"); suTex_ = gl::GetUniformLocation(spriteProg_, "uTex");
    suEye_ = gl::GetUniformLocation(spriteProg_, "uEye"); suFog_ = gl::GetUniformLocation(spriteProg_, "uFog");
    gl::GenVertexArrays(1, &spriteVao_); gl::GenBuffers(1, &spriteVbo_);
    gl::BindVertexArray(spriteVao_); gl::BindBuffer(gl::ARRAY_BUFFER, spriteVbo_);
    gl::EnableVertexAttribArray(0); gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 28, (void*)0);
    gl::EnableVertexAttribArray(1); gl::VertexAttribPointer(1, 3, gl::FLOAT, 0, 28, (void*)12);
    gl::EnableVertexAttribArray(2); gl::VertexAttribPointer(2, 1, gl::FLOAT, 0, 28, (void*)24);
    gl::GenVertexArrays(1, &hudVao_); gl::GenBuffers(1, &hudVbo_);
    gl::BindVertexArray(hudVao_); gl::BindBuffer(gl::ARRAY_BUFFER, hudVbo_);
    gl::EnableVertexAttribArray(0); gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, 24, (void*)0);
    gl::EnableVertexAttribArray(1); gl::VertexAttribPointer(1, 4, gl::UNSIGNED_BYTE, 1, 24, (void*)8);
    gl::EnableVertexAttribArray(2); gl::VertexAttribPointer(2, 3, gl::FLOAT, 0, 24, (void*)12);
    huTex_ = gl::GetUniformLocation(hudProg_, "uTex");
    gl::BindVertexArray(0);
    return true;
}

void Renderer::uploadSprites(const SpriteLib& sp) {
    gl::GenTextures(1, &spriteTex_);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, spriteTex_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    int n = std::max(1, sp.layers());
    gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, GLint(gl::RGBA8), SpriteLib::kSize, SpriteLib::kSize, n, 0, gl::RGBA, gl::UNSIGNED_BYTE,
                   sp.layers() ? sp.rgba().data() : nullptr);
    gl::GenerateMipmap(gl::TEXTURE_2D_ARRAY);
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, GLint(gl::LINEAR_MIPMAP_LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, GLint(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_S, GLint(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_T, GLint(gl::CLAMP_TO_EDGE));
}

int Renderer::uploadFont(const uint8_t* rgba, int w, int h) {
    GLuint t = 0;
    gl::GenTextures(1, &t);
    gl::BindTexture(gl::TEXTURE_2D, t);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, GLint(gl::RGBA8), w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, GLint(gl::NEAREST));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, GLint(gl::NEAREST));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, GLint(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, GLint(gl::CLAMP_TO_EDGE));
    fontTex_.push_back(t);
    return int(fontTex_.size()) - 1;
}

void Renderer::drawHud(const std::vector<HudQuad>& quads, int w, int h) {
    if (quads.empty()) return;
    gl::Viewport(0, 0, w, h);
    struct V { float x, y; uint8_t c[4]; float u, v, t; };
    std::vector<V> vb;
    vb.reserve(quads.size() * 6);
    for (const auto& q : quads) {
        float x0 = q.x0 / float(w) * 2 - 1, x1 = q.x1 / float(w) * 2 - 1;
        float y0 = 1 - q.y0 / float(h) * 2, y1 = 1 - q.y1 / float(h) * 2;
        float tt = q.font >= 0 ? 1.f : 0.f;
        V a{x0, y0, {}, q.u0, q.v0, tt}, b{x1, y0, {}, q.u1, q.v0, tt}, c{x1, y1, {}, q.u1, q.v1, tt}, d{x0, y1, {}, q.u0, q.v1, tt};
        for (V* v : {&a, &b, &c, &d}) std::copy(q.rgba, q.rgba + 4, v->c);
        vb.insert(vb.end(), {a, b, c, a, c, d});
    }
    gl::Disable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(hudProg_);
    gl::Uniform1i(huTex_, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindVertexArray(hudVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, hudVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(vb.size() * sizeof(V)), vb.data(), gl::STREAM_DRAW);
    // draw in runs of the same font texture, keeping the submission order
    size_t start = 0;
    while (start < quads.size()) {
        int f = quads[start].font;
        size_t end = start;
        while (end < quads.size() && (quads[end].font == f || (f < 0 && quads[end].font < 0))) end++;
        if (f >= 0 && size_t(f) < fontTex_.size()) gl::BindTexture(gl::TEXTURE_2D, fontTex_[size_t(f)]);
        gl::DrawArrays(gl::TRIANGLES, GLint(start * 6), GLsizei((end - start) * 6));
        start = end;
    }
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
}

void Renderer::uploadTextures(const TextureLib& tex, const GameFS* fs, bool hdOn) {
    const auto& pages = tex.pages();
    texLayers_ = int(pages.size());
    const GameFS* hd = fs && hdOn ? fs : nullptr;
    const int size = hd ? 512 : 256;
    gl::GenTextures(1, &texArray_);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, texArray_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, GLint(gl::RGBA8), size, size, std::max(texLayers_, 1), 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    std::vector<uint8_t> rgba;
    int packed = 0;
    for (size_t i = 0; i < pages.size(); i++) {
        if (pages[i].width != 256 || pages[i].height != 256) continue;
        bool fromPack = false;
        if (hd) { hdPage(*hd, pages[i], size, rgba, fromPack); packed += fromPack; }
        else rgb565ToRgba(pages[i].rgb565, rgba, true);
        gl::TexSubImage3D(gl::TEXTURE_2D_ARRAY, 0, 0, 0, GLint(i), size, size, 1, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
    }
    if (hd) std::printf("HD textures: %d of %zu pages from HDTEX\n", packed, pages.size());
    gl::GenerateMipmap(gl::TEXTURE_2D_ARRAY);
    {   // anisotropic filtering (EXT_texture_filter_anisotropic / GL 4.6): sharper ground at grazing angles
        float maxA = 0; gl::GetFloatv(0x84FF, &maxA);
        if (maxA > 1.f) gl::TexParameterf(gl::TEXTURE_2D_ARRAY, 0x84FE, std::min(16.f, maxA));
        while (gl::GetError() != 0) {}
    }
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, GLint(gl::LINEAR_MIPMAP_LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, GLint(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_S, GLint(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_T, GLint(gl::CLAMP_TO_EDGE));
}

void Renderer::uploadModels(const ModelLib& lib) {
    std::vector<Vtx> vb;
    ranges_.clear();
    std::vector<Vtx> trans;
    for (const Model& m : lib.models()) {
        Range r; r.first = int(vb.size());
        trans.clear();
        for (const Polygon& p : m.polys) {
            if (isInvisible(p)) continue;
            const Blend blend = polygonBlend(p);
            auto P = [&](size_t k) {
                const int32_t* v = &m.verts[size_t(p.idx[k]) * 3];
                return Vec3{float(v[0]) * unitScale, float(v[1]) * unitScale, float(v[2]) * unitScale};
            };
            Vec3 n = normalize(cross(P(1) - P(0), P(2) - P(0)));
            for (size_t k = 1; k + 1 < p.idx.size(); k++) {
                const size_t tri[3] = {0, k, k + 1};
                for (size_t t : tri) {
                    Vtx v{};
                    Vec3 pos = P(t);
                    v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
                    v.n[0] = n.x; v.n[1] = n.y; v.n[2] = n.z;
                    size_t vi = p.idx[t];
                    v.part = vi < m.vertexPart.size() ? float(std::min<int>(m.vertexPart[vi], kMaxParts - 1)) : 0.f;
                    v.mode = float(int(blend));
                    if (p.kind == Polygon::Textured && int(p.texture) < texLayers_) {
                        v.uv[0] = p.u[t] / 32768.0f; v.uv[1] = p.v[t] / 32768.0f; v.layer = float(p.texture);
                        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                    } else if (p.kind == Polygon::Gouraud) {
                        v.layer = -1;
                        uint8_t s = uint8_t(std::min(255, 64 + (p.shade[t] >> 7)));
                        v.color[0] = v.color[1] = v.color[2] = s; v.color[3] = 255;
                    } else {
                        v.layer = -1; color565(p.color, v.color);
                    }
                    (blend == Blend::Opaque || blend == Blend::ColorKey ? vb : trans).push_back(v);
                }
            }
        }
        r.opaque = int(vb.size()) - r.first;
        vb.insert(vb.end(), trans.begin(), trans.end());
        r.transparent = int(trans.size());
        ranges_.push_back(r);
    }
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(vb.size() * sizeof(Vtx)), vb.data(), gl::STATIC_DRAW);
    const GLsizei st = sizeof(Vtx);
    gl::EnableVertexAttribArray(0); gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, st, (void*)offsetof(Vtx, pos));
    gl::EnableVertexAttribArray(1); gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, st, (void*)offsetof(Vtx, uv));
    gl::EnableVertexAttribArray(2); gl::VertexAttribPointer(2, 1, gl::FLOAT, 0, st, (void*)offsetof(Vtx, layer));
    gl::EnableVertexAttribArray(3); gl::VertexAttribPointer(3, 4, gl::UNSIGNED_BYTE, 1, st, (void*)offsetof(Vtx, color));
    gl::EnableVertexAttribArray(4); gl::VertexAttribPointer(4, 3, gl::FLOAT, 0, st, (void*)offsetof(Vtx, n));
    gl::EnableVertexAttribArray(5); gl::VertexAttribPointer(5, 1, gl::FLOAT, 0, st, (void*)offsetof(Vtx, part));
    gl::EnableVertexAttribArray(6); gl::VertexAttribPointer(6, 1, gl::FLOAT, 0, st, (void*)offsetof(Vtx, mode));
    lib_ = &lib;
}

void Renderer::setWorld(const World& world, const ModelLib& lib, const TypeDB& types) {
    instances_.clear();
    types_ = &types;
    const auto& objs = world.objects();
    for (size_t i = 0; i < objs.size(); i++) {
        const WorldObject& o = objs[i];
        if (o.proto >= lib.models().size()) continue;
        Instance in;
        in.model = int(o.proto);
        in.objClass = int(o.objClass);
        in.pos = {o.pos[0] * unitScale, o.pos[1] * unitScale, o.pos[2] * unitScale};
        in.radius = float(o.radius) * unitScale;
        in.rot = worldObjectRotation(o.angle);
        in.set = o.set;
        in.worldIndex = int(i);
        instances_.push_back(in);
    }
}

int Renderer::addInstance(int model, int objClass, Vec3 pos, float yawRad) {
    Instance in;
    in.model = model; in.objClass = objClass; in.pos = pos;
    in.radius = lib_ ? float(lib_->models()[size_t(model)].radius) * unitScale : 1.f;
    in.rot = Mat4::rotZ(yawRad);
    instances_.push_back(in);
    return int(instances_.size()) - 1;
}

Renderer::Instance* Renderer::instanceForWorldObject(int worldIndex) {
    for (auto& in : instances_) if (in.worldIndex == worldIndex) return &in;
    return nullptr;
}

void Renderer::submit(const Item& it, bool transparent) {
    const Range& r = ranges_[size_t(it.geo)];
    int count = transparent ? r.transparent : r.opaque;
    if (!count) return;
    gl::UniformMatrix4fv(uModel_, 1, 0, it.xf.m);
    gl::UniformMatrix4fv(uParts_, kMaxParts, 0, partPool_[size_t(it.partOffset)].m);
    gl::DrawArrays(gl::TRIANGLES, transparent ? r.first + r.opaque : r.first, count);
}

void Renderer::addItem(int model, const ObjectType* t, Vec3 pos, float radius, const Mat4& rot0, float rotorTicks, float animTicks, const Instance* inst,
                       Vec3 eye, float focal, const Mat4& camBasis, float timeSec) {
    const auto& models = lib_->models();
    Vec3 d = pos - eye;
    float dist = std::sqrt(dot(d, d));
    if (dist - radius > viewDistance) return;
    if (dot(d, camFwd_) < -radius - 1.f) return;            // behind the camera
    int mi = model;
    const Model* m = &models[size_t(mi)];
    if (!m->lods.empty()) {                                 // LOD selection by projected radius
        float px = radius / std::max(dist, 0.001f) * focal;
        size_t n = m->lods.size();
        size_t pick = px > lodPixels[0] ? 0 : (px > lodPixels[1] ? std::min<size_t>(1, n - 1) : n - 1);
        int li = m->lods[pick];
        if (li < 0 || size_t(li) >= models.size()) return;
        mi = li; m = &models[size_t(mi)];
    }
    int geo = m->aliasOf >= 0 && size_t(m->aliasOf) < models.size() ? m->aliasOf : mi;
    const Range& r = ranges_[size_t(geo)];
    if (!r.opaque && !r.transparent) return;
    Mat4 rot = rot0;
    if (t && !t->parts.empty() && t->parts[0].method == ObjectType::RotateLikeCamera) rot = camBasis;
    Item it;
    it.geo = geo;
    it.xf = Mat4::translate(pos) * rot;
    it.dist = dist;
    it.partOffset = int(partPool_.size());
    partPool_.resize(partPool_.size() + kMaxParts);
    PoseInput pin;
    pin.unitScale = unitScale;
    pin.ticks = animTicks >= 0 ? animTicks : timeSec * ticksPerSecond;
    pin.rotorTicks = animTicks >= 0 && rotorTicks < 0 ? animTicks : rotorTicks;
    pin.billboardLocal = rot.rotationTransposed() * camBasis;
    if (inst && inst->aim) { pin.aim = true; pin.aimYaw = inst->aimYaw; pin.aimPitch = inst->aimPitch; }
    if (inst && inst->nVars > 0) { pin.vars = inst->vars; pin.nVars = inst->nVars; }
    computePose(models[size_t(geo)], t, pin, &partPool_[size_t(it.partOffset)], kMaxParts);
    items_.push_back(it);
}

void Renderer::draw(const Mat4& proj, const Mat4& view, Vec3 eye, int w, int h, float timeSec,
                    const std::vector<Extra>* extras, const std::vector<Sprite>* sprites) {
    gl::Viewport(0, 0, w, h);
    if (vpW > 0) {                                  // briefing monitor: black around the picture
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        gl::Viewport(vpX, vpY, vpW, vpH);
        gl::Enable(gl::SCISSOR_TEST);
        gl::Scissor(vpX, vpY, vpW, vpH);
    }
    gl::ClearColor(fogColor[0], fogColor[1], fogColor[2], 1);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    if (vpW > 0) gl::Disable(gl::SCISSOR_TEST);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);
    gl::Disable(gl::CULL_FACE);
    gl::UseProgram(prog_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, texArray_);
    gl::Uniform1i(uTex_, 0);
    Vec3 sun = normalize({0.4f, 0.3f, 0.85f});
    gl::Uniform3f(uSun_, sun.x, sun.y, sun.z);
    gl::Uniform3f(uEye_, eye.x, eye.y, eye.z);
    gl::Uniform2f(uFog_, fogNear, fogFar);
    gl::Uniform3f(uFogColor_, fogColor[0], fogColor[1], fogColor[2]);
    gl::Uniform1f(uLit_, 1.f);
    gl::BindVertexArray(vao_);
    Mat4 vp = proj * view;
    gl::UniformMatrix4fv(uVP_, 1, 0, vp.m);

    const float focal = float(h) * 0.5f / std::tan(30.f * 3.14159265f / 180.f);
    const auto& models = lib_->models();
    // Sprite orientation: local X = camera right, local Y = towards the viewer, local Z = camera up.
    const Mat4 camBasis = Mat4::basis({view.m[0], view.m[4], view.m[8]}, {view.m[2], view.m[6], view.m[10]}, {view.m[1], view.m[5], view.m[9]});
    camFwd_ = {-view.m[2], -view.m[6], -view.m[10]};
    currentTicks = timeSec * ticksPerSecond;
    items_.clear();
    partPool_.clear();
    // Sky domes (SKY*.3DW, switched on by the area): centred on the camera, unfogged, behind everything.
    auto isSky = [](const std::string& s) { return s.size() >= 3 && (s[0] | 32) == 's' && (s[1] | 32) == 'k' && (s[2] | 32) == 'y'; };
    for (const Instance& in : instances_) {
        if (in.hidden || hiddenSets.count(in.set) || !isSky(in.set)) continue;
        const Model& m = models[size_t(in.model)];
        int mi = m.lods.empty() ? in.model : m.lods[0];
        int geo = models[size_t(mi)].aliasOf >= 0 ? models[size_t(mi)].aliasOf : mi;
        Item it; it.geo = geo; it.dist = 0; it.partOffset = int(partPool_.size());
        partPool_.resize(partPool_.size() + kMaxParts);
        float rad = std::max(1.f, float(models[size_t(geo)].radius) * unitScale);
        float sc = std::min(1.f, skyDistance / rad);
        it.xf = Mat4::translate({eye.x, eye.y, eye.z - skyDrop * sc}) * Mat4::scale(sc) * in.rot;
        PoseInput pin; pin.unitScale = unitScale; pin.ticks = timeSec * ticksPerSecond;
        computePose(models[size_t(geo)], nullptr, pin, &partPool_[size_t(it.partOffset)], kMaxParts);
        gl::Uniform2f(uFog_, 1e8f, 2e8f);
        gl::Uniform1f(uLit_, 0.f);
        gl::DepthMask(0);
        submit(it, false); submit(it, true);
        gl::DepthMask(1);
        gl::Uniform2f(uFog_, fogNear, fogFar);
    }
    gl::Uniform1f(uLit_, 1.f);
    partPool_.clear();
    for (const Instance& in : instances_) {
        if (in.hidden || hiddenSets.count(in.set) || isSky(in.set)) continue;
        addItem(in.model, types_ ? types_->get(size_t(in.objClass)) : nullptr, in.pos, in.radius, in.rot, in.rotorTicks, in.animTicks, &in,
                eye, focal, camBasis, timeSec);
    }
    if (extras)
        for (const Extra& x : *extras) {
            if (x.model < 0 || size_t(x.model) >= models.size()) continue;
            addItem(x.model, types_ ? types_->get(size_t(x.objClass)) : nullptr, x.pos,
                    std::max(1.f, models[size_t(x.model)].radius * unitScale), x.rot, -1, -1, nullptr, eye, focal, camBasis, timeSec);
        }
    for (const Item& it : items_) submit(it, false);
    // Transparent pass: back to front, no depth writes.
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    gl::DepthMask(0);
    for (const Item& it : items_) submit(it, true);
    if (sprites && !sprites->empty() && spriteTex_) {
        Vec3 right{view.m[0], view.m[4], view.m[8]}, up{view.m[1], view.m[5], view.m[9]};
        std::vector<float> vb;
        vb.reserve(sprites->size() * 42);
        for (const Sprite& sp : *sprites) {
            Vec3 rr = right * sp.size, uu = up * sp.size;
            Vec3 c[4] = {sp.pos - rr - uu, sp.pos + rr - uu, sp.pos + rr + uu, sp.pos - rr + uu};
            const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            for (int k : {0, 1, 2, 0, 2, 3})
                vb.insert(vb.end(), {c[k].x, c[k].y, c[k].z, uv[k][0], uv[k][1], float(sp.layer), sp.alpha});
        }
        gl::UseProgram(spriteProg_);
        gl::UniformMatrix4fv(suVP_, 1, 0, vp.m);
        gl::Uniform3f(suEye_, eye.x, eye.y, eye.z);
        gl::Uniform2f(suFog_, fogNear, fogFar);
        gl::BindTexture(gl::TEXTURE_2D_ARRAY, spriteTex_);
        gl::Uniform1i(suTex_, 0);
        gl::BindVertexArray(spriteVao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, spriteVbo_);
        gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(vb.size() * sizeof(float)), vb.data(), gl::STREAM_DRAW);
        gl::DrawArrays(gl::TRIANGLES, 0, GLsizei(vb.size() / 7));
        gl::BindVertexArray(vao_);
        gl::UseProgram(prog_);
    }
    gl::DepthMask(1);
    gl::Disable(gl::BLEND);
    drawnLastFrame = int(items_.size());
}

} // namespace xa
