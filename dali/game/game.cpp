#include <dali/game/game.h>

#include <dali/core/api.h>
#include <dali/game/platform_state.h>

#include <imgui.h>

#include <cstdio>

namespace kdk {

GameState* GetGameState(PlatformState* ps) {
    ASSERT(ps->GameState);
    return (GameState*)ps->GameState;
}

World* GetWorld(PlatformState* ps) { return &GetGameState(ps)->World; }

}  // namespace kdk

// The hot-reloadable game DLL. For now every entry point just prints, so we can verify the
// platform's load-and-call flow end to end before there is any real gameplay. The five functions
// are exported with C linkage so their symbol names match the KDK_*_NAME strings the platform
// resolves (see dali/core/api.h).

extern "C" {

KDK_API bool OnGameInit(kdk::PlatformState* ps) {
    using namespace kdk;

    auto* game_state = ps->Memory.PermanentArena.PushZero<GameState>();
    ps->GameState = game_state;

    printf("[game] OnGameInit\n");
    return true;
}

KDK_API bool OnGameUpdate(kdk::PlatformState* ps) {
    using namespace kdk;

    World* world = GetWorld(ps);
    world->Count++;

    return true;
}

KDK_API bool OnGameRender(kdk::PlatformState* ps) {
    using namespace kdk;

    GameState* game_state = GetGameState(ps);
    World* world = &game_state->World;

    // ImGui UI is submitted here (between the platform's NewFrame and Render). Draw data is
    // finalized and rendered by the platform in PlatformEndFrame.
    ImGui::ShowDemoWindow();

    ImGui::Begin("Dali");
    ImGui::Text("Hello from the game DLL");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("World Count: %d", world->Count);
    ImGui::Text("DLL Reload Count: %d", game_state->InternalDetectedReload);

    ImGui::End();

    return true;
}

KDK_API bool OnSOLoaded(kdk::PlatformState* ps) {
    using namespace kdk;

    SetGlobalPlatformState(ps);

    // NOTE: GameState is initialized on |OnGameInit|, which is called after the first load.
    if (auto* game_state = (GameState*)ps->GameState) {
        ASSERT(ps->GameLibraryState.ReloadCount == game_state->InternalDetectedReload);
        game_state->InternalDetectedReload++;
    }

    ImGui::SetCurrentContext((ImGuiContext*)ps->ImGuiState.Context);
    ImGui::SetAllocatorFunctions(ps->ImGuiState.AllocFunc, ps->ImGuiState.FreeFunc);

    printf("[game] OnSOLoaded\n");
    return true;
}

KDK_API bool OnSOUnloaded(kdk::PlatformState* ps) {
    (void)ps;
    kdk::SetGlobalPlatformState(nullptr);

    printf("[game] OnSOUnloaded\n");
    return true;
}

}  // extern "C"
