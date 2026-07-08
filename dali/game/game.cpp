#include <dali/game/game.h>

#include <dali/core/api.h>
#include <dali/core/color.h>
#include <dali/game/hex.h>
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

namespace game_private {

// Debug-draws a small hex neighbourhood straight onto ImGui's background draw list. Per
// milestone-01 step 3 there is no engine render layer yet — this is the temporary draw path that
// lets the grid/hover/path steps proceed before we know which primitives the real renderer needs.
// The hex under the mouse is highlighted, which also proves the world<->hex transform round-trips.
void DrawHexGrid(kdk::PlatformState* ps) {
    using namespace kdk;

    constexpr float kHexSize = 60.0f;  // Distance from a hex center to a corner, in pixels.
    constexpr int kGridRadius = 3;     // Radius-1 neighbourhood: a center tile ringed by 6.

    ImGuiIO& io = ImGui::GetIO();
    Vec2 origin(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    // Which hex is under the mouse? Invert the exact transform used to place the tiles, so the
    // highlight lines up regardless of the screen's Y-down convention.
    Vec2 mouse_relative(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
    Hex hovered = Hex::WorldToHex(kHexSize, mouse_relative);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    for (int q = -kGridRadius; q <= kGridRadius; ++q) {
        int r_lo = Max(-kGridRadius, -q - kGridRadius);
        int r_hi = Min(kGridRadius, -q + kGridRadius);
        for (int r = r_lo; r <= r_hi; ++r) {
            Hex hex{q, r};

            Vec2 world = Hex::HexToWorld(kHexSize, hex);
            Vec2 center(origin.x + world.x, origin.y + world.y);

            ImVec2 corners[6];
            for (int i = 0; i < 6; ++i) {
                Vec2 c = Hex::HexCorner(kHexSize, center, i);
                corners[i] = ImVec2(c.x, c.y);
            }

            bool is_hovered = hex.Q == hovered.Q && hex.R == hovered.R;
            Color32 fill = is_hovered ? Color32::Gold : Color32::DarkSlateGrey;

            draw_list->AddConvexPolyFilled(corners, 6, fill.Bits);
            draw_list->AddPolyline(corners, 6, Color32::White.Bits, ImDrawFlags_Closed, 2.0f);
        }
    }

    (void)ps;
}

}  // namespace game_private

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

    game_private::DrawHexGrid(ps);

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
