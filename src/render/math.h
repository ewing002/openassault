#pragma once
#include <cmath>
#include <algorithm>

namespace xa {
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalize(Vec3 a) { float l = std::sqrt(dot(a, a)); return l > 0 ? a * (1.0f / l) : a; }

struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};   // column-major
    static Mat4 perspective(float fovy, float aspect, float zn, float zf) {
        Mat4 r; float f = 1.0f / std::tan(fovy / 2);
        for (float& v : r.m) v = 0;
        r.m[0] = f / aspect; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1; r.m[14] = 2 * zf * zn / (zn - zf);
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 at, Vec3 up) {
        Vec3 f = normalize(at - eye), s = normalize(cross(f, up)), u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
    static Mat4 translate(Vec3 t) { Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static Mat4 rotZ(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
    static Mat4 rotX(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
    static Mat4 rotY(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
    // rotation-only part transposed (inverse of a pure rotation)
    Mat4 rotationTransposed() const {
        Mat4 r;
        for (int c = 0; c < 3; c++) for (int k = 0; k < 3; k++) r.m[c * 4 + k] = m[k * 4 + c];
        return r;
    }
    static Mat4 basis(Vec3 x, Vec3 y, Vec3 z) {
        Mat4 r; r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z; r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
        r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z; return r;
    }
    Vec3 transformPoint(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    static Mat4 scale(float s) { Mat4 r; r.m[0] = r.m[5] = r.m[10] = s; return r; }
};
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
        r.m[c * 4 + rr] = s;
    }
    return r;
}
}
