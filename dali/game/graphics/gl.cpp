#include <dali/game/graphics/gl.h>

#include <dali/core/api.h>

#include <glad/gl.h>

namespace kdk {

bool LoadGLBackend(PlatformState* ps) {
    if (!ps->API.GLGetProcAddress) {
        return false;
    }
    // The platform already made the GL context current and loaded its own table; this populates the
    // DLL's separate copy against the same context. Both modules resolve the same entry points.
    return gladLoadGL((GLADloadfunc)ps->API.GLGetProcAddress) != 0;
}

}  // namespace kdk
