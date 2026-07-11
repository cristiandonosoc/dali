#pragma once

namespace kdk {

struct PlatformState;

// Loads (or reloads) the game DLL's GLAD function table via ps->API.GLGetProcAddress. The DLL image
// gets a fresh, empty table on every (re)load, so this is called from OnSOLoaded each time — on the
// first load and on every hot-reload. Returns false if the table could not be resolved.
bool LoadGLBackend(PlatformState* ps);

}  // namespace kdk
