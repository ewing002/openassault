// Minimal OpenGL 3.3 core loader (no external generator needed).
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define XA_APIENTRY __stdcall
#else
#define XA_APIENTRY
#endif

typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei;
typedef float GLfloat; typedef unsigned char GLboolean; typedef unsigned int GLbitfield;
typedef char GLchar; typedef ptrdiff_t GLsizeiptr; typedef ptrdiff_t GLintptr; typedef void GLvoid;

#define XA_GL_FUNCS(X) \
 X(void, Viewport, GLint, GLint, GLsizei, GLsizei) \
 X(void, Scissor, GLint, GLint, GLsizei, GLsizei) \
 X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat) \
 X(void, Clear, GLbitfield) \
 X(void, Enable, GLenum) X(void, Disable, GLenum) \
 X(void, DepthFunc, GLenum) X(void, CullFace, GLenum) X(void, BlendFunc, GLenum, GLenum) \
 X(void, DepthMask, GLboolean) \
 X(GLenum, GetError) \
 X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) \
 X(void, PixelStorei, GLenum, GLint) \
 X(void, GenTextures, GLsizei, GLuint*) X(void, DeleteTextures, GLsizei, const GLuint*) X(void, DeleteBuffers, GLsizei, const GLuint*) X(void, DeleteVertexArrays, GLsizei, const GLuint*) X(void, DeleteProgram, GLuint) X(void, BindTexture, GLenum, GLuint) \
 X(void, TexParameteri, GLenum, GLenum, GLint) X(void, TexParameterf, GLenum, GLenum, GLfloat) X(void, GetFloatv, GLenum, GLfloat*) \
 X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
 X(void, TexImage3D, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
 X(void, TexSubImage3D, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*) \
 X(void, GenerateMipmap, GLenum) X(void, ActiveTexture, GLenum) \
 X(void, DrawArrays, GLenum, GLint, GLsizei) \
 X(void, GenVertexArrays, GLsizei, GLuint*) X(void, BindVertexArray, GLuint) \
 X(void, GenBuffers, GLsizei, GLuint*) X(void, BindBuffer, GLenum, GLuint) \
 X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum) \
 X(void, EnableVertexAttribArray, GLuint) \
 X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
 X(GLuint, CreateShader, GLenum) X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) \
 X(void, CompileShader, GLuint) X(void, GetShaderiv, GLuint, GLenum, GLint*) \
 X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
 X(GLuint, CreateProgram) X(void, AttachShader, GLuint, GLuint) X(void, LinkProgram, GLuint) \
 X(void, GetProgramiv, GLuint, GLenum, GLint*) X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
 X(void, UseProgram, GLuint) X(GLint, GetUniformLocation, GLuint, const GLchar*) \
 X(void, Uniform1i, GLint, GLint) X(void, Uniform1f, GLint, GLfloat) X(void, Uniform2f, GLint, GLfloat, GLfloat) X(void, Uniform3f, GLint, GLfloat, GLfloat, GLfloat) \
 X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)

namespace gl {
#define XA_DECL(ret, name, ...) typedef ret (XA_APIENTRY *PFN_##name)(__VA_ARGS__); extern PFN_##name name;
XA_GL_FUNCS(XA_DECL)
#undef XA_DECL
bool load(void* (*getProc)(const char*));

enum : GLenum {
    COLOR_BUFFER_BIT = 0x4000, DEPTH_BUFFER_BIT = 0x100, DEPTH_TEST = 0x0B71, SCISSOR_TEST = 0x0C11, CULL_FACE = 0x0B44, BLEND = 0x0BE2,
    ONE = 1, LEQUAL = 0x0203, LESS = 0x0201, BACK = 0x0405, SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303,
    TEXTURE_2D_ARRAY = 0x8C1A, TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0, TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800,
    TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803, LINEAR = 0x2601, NEAREST = 0x2600, LINEAR_MIPMAP_LINEAR = 0x2703,
    REPEAT = 0x2901, CLAMP_TO_EDGE = 0x812F, RGBA = 0x1908, RGBA8 = 0x8058, UNSIGNED_BYTE = 0x1401, FLOAT = 0x1406,
    TRIANGLES = 0x0004, ARRAY_BUFFER = 0x8892, STATIC_DRAW = 0x88E4, STREAM_DRAW = 0x88E0, VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30,
    COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, PACK_ALIGNMENT = 0x0D05, UNPACK_ALIGNMENT = 0x0CF5,
    UNSIGNED_SHORT = 0x1403, SHORT = 0x1402, INT = 0x1404
};
}
