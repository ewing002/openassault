#include "glapi.h"
namespace gl {
#define XA_DEF(ret, name, ...) PFN_##name name = nullptr;
XA_GL_FUNCS(XA_DEF)
#undef XA_DEF
bool load(void* (*getProc)(const char*)) {
    bool ok = true;
#define XA_LOAD(ret, name, ...) name = reinterpret_cast<PFN_##name>(getProc("gl" #name)); if (!name) ok = false;
    XA_GL_FUNCS(XA_LOAD)
#undef XA_LOAD
    return ok;
}
}
