#pragma once

#include <dali/core/api.h>

namespace kdk {

// Ambient access to the one PlatformState the game DLL is currently attached to. Rebound on every
// OnSOLoaded (and cleared on OnSOUnloaded, see game.cpp), so it survives hot-reload without being
// threaded through every call — infrastructure hanging off PlatformState (memory, file IO,
// logging) is the same for the whole process, so there's nothing to gain by passing it explicitly.
// Domain/gameplay state hanging off PlatformState::GameState should still be passed explicitly.
//
// Not thread-safe. Fine while the game DLL is single-threaded; revisit if that changes.
PlatformState* GetGlobalPlatformState();
void SetGlobalPlatformState(PlatformState* ps);

}  // namespace kdk
