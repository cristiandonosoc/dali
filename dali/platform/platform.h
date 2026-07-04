#pragma once

#include <dali/core/api.h>

namespace kdk {

// Brings up memory and the main window: SDL init, window creation, and the OpenGL context (incl.
// loading GL functions via GLAD). Everything past that (ImGui, asset systems, scenes, etc.) is
// still TODO.
bool InitPlatform(PlatformState* ps);
void ShutdownPlatform(PlatformState* ps);

}  // namespace kdk
