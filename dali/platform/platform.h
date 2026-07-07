#pragma once

#include <dali/core/api.h>

#include <SDL3/SDL_timer.h>

namespace kdk {

// Brings up memory and the main window: SDL init, window creation, and the OpenGL context (incl.
// loading GL functions via GLAD). Everything past that (ImGui, asset systems, scenes, etc.) is
// still TODO.
bool PlatformInit(PlatformState* ps);
void PlatformShutdown(PlatformState* ps);

enum class EPlatformFrameResponse : u8 {
	Continue,
	QuitRequested,
};

// Returns whether t
EPlatformFrameResponse PlatformBeginFrame(PlatformState* ps);
void PlatformEndFrame(PlatformState* ps);

u64 GetCPUTicks();

}  // namespace kdk
