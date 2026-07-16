#include <dali/game/game.h>

#include <dali/core/api.h>
#include <dali/game/graphics/gl.h>
#include <dali/game/platform.h>

#include <imgui.h>

#include <cstdio>

// The hot-reloadable game DLL's entry surface: the exact set of extern "C" symbols the platform
// resolves after LoadLibrary (their names must match the KDK_*_NAME strings in dali/core/api.h).
// This is the only translation unit that knows about the export contract and the reload-time
// rebind — it casts PlatformState::GameState once at the boundary and forwards to the plain game
// functions in game.h, which is where the actual gameplay lives (and stays testable). OnSOLoaded /
// OnSOUnloaded are process-global rebind concerns, so they have no game.h counterpart.

extern "C" {

KDK_API bool OnGameInit(kdk::PlatformState* ps) {
    auto* game_state = ps->Memory.PermanentArena.PushInit<kdk::GameState>();
    ps->GameState = game_state;
    kdk::GameInit(ps, game_state);
    return true;
}

KDK_API bool OnGameUpdate(kdk::PlatformState* ps) {
    kdk::GameUpdate(ps, (kdk::GameState*)ps->GameState);
    return true;
}

KDK_API bool OnGameRender(kdk::PlatformState* ps) {
    kdk::GameRender(ps, (kdk::GameState*)ps->GameState);
    return true;
}

KDK_API bool OnSOLoaded(kdk::PlatformState* ps, bool is_reload) {
    using namespace kdk;

    SetGlobalPlatformState(ps);

    // NOTE: GameState is initialized on |OnGameInit|, which is called after the first load.
    if (auto* game_state = (GameState*)ps->GameState) {
        ASSERT(ps->GameLibraryState.ReloadCount == game_state->InternalDetectedReload);
        if (is_reload) {
            game_state->InternalDetectedReload++;
        }
    }

    ImGui::SetCurrentContext((ImGuiContext*)ps->ImGuiState.Context);
    ImGui::SetAllocatorFunctions(ps->ImGuiState.AllocFunc, ps->ImGuiState.FreeFunc);

    // The DLL image gets a fresh, empty GLAD table on every load; repopulate it against the
    // platform's already-current context. Must happen before any GL call in the game (texture
    // uploads, etc.).
    if (!LoadGLBackend(ps)) {
        printf("[entrypoint] ERROR: failed to load GL backend\n");
        return false;
    }

    printf("[entrypoint] OnSOLoaded\n");
    return true;
}

KDK_API bool OnSOUnloaded(kdk::PlatformState* ps) {
    (void)ps;
    kdk::SetGlobalPlatformState(nullptr);

    printf("[entrypoint] OnSOUnloaded\n");
    return true;
}

}  // extern "C"
