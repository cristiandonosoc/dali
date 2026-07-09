#include <dali/game/game.h>

#include <dali/core/api.h>
#include <dali/core/color.h>
#include <dali/game/hex.h>

#include <imgui.h>

#include <cstdio>

namespace game_private {

// Debug-draws the grid straight onto ImGui's background draw list. Per milestone-01 step 3 there is
// no engine render layer yet — this is the temporary draw path that lets the grid/hover/path steps
// proceed before we know which primitives the real renderer needs. The hex under the mouse is
// highlighted, which also proves the world<->hex transform round-trips.
void DrawHexGrid(kdk::World* world) {
    using namespace kdk;

    constexpr float kHexSize = 60.0f;  // Distance from a hex center to a corner, in pixels.

    ImGuiIO& io = ImGui::GetIO();
    Vec2 origin(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    // Which hex is under the mouse? Invert the exact transform used to place the tiles, so the
    // highlight lines up regardless of the screen's Y-down convention.
    Vec2 mouse_relative(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
    Hex hovered = Hex::WorldToHex(kHexSize, mouse_relative);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // A tile-center in screen space. Shared by the grid pass and the path spine so they line up.
    auto tile_center = [&](Hex hex) -> ImVec2 {
        Vec2 w = Hex::HexToWorld(kHexSize, hex);
        return ImVec2(origin.x + w.x, origin.y + w.y);
    };

    for (const Tile& tile : world->Grid.Tiles) {
        ImVec2 center = tile_center(tile.Hex);

        ImVec2 corners[6];
        for (int i = 0; i < 6; ++i) {
            Vec2 c = Hex::HexCorner(kHexSize, Vec2(center.x, center.y), i);
            corners[i] = ImVec2(c.x, c.y);
        }

        // Hover wins over the path color, which wins over the plain grid color.
        Color32 fill = Color32::DarkSlateGrey;
        if (tile.IsPath) {
            fill = Color32::Sienna;
        }
        if (tile.Hex == hovered) {
            fill = Color32::Gold;
        }

        draw_list->AddConvexPolyFilled(corners, 6, fill.Bits);
        draw_list->AddPolyline(corners, 6, Color32::White.Bits, ImDrawFlags_Closed, 2.0f);
    }

    // The path spine: an ordered polyline through the path-tile centers. A visible gap here would
    // mean two consecutive path tiles aren't neighbours. Spawn is green, base is red.
    const Path& path = world->Path;
    if (path.Size >= 2) {
        ImVec2 spine[kMaxPathTiles];
        for (int i = 0; i < path.Size; ++i) {
            spine[i] = tile_center(path[i]);
        }
        draw_list->AddPolyline(spine, path.Size, Color32::BrightGold.Bits, ImDrawFlags_None, 4.0f);
        draw_list->AddCircleFilled(spine[0], 8.0f, Color32::Green.Bits);
        draw_list->AddCircleFilled(spine[path.Size - 1], 8.0f, Color32::Red.Bits);
    }
}

}  // namespace game_private

namespace kdk {

void Grid::InitRing(int radius) {
    Tiles.Size = 0;
    for (int q = -radius; q <= radius; ++q) {
        int r_lo = Max(-radius, -q - radius);
        int r_hi = Min(radius, -q + radius);
        for (int r = r_lo; r <= r_hi; ++r) {
            Tiles.Push(Tile{.Hex = Hex{q, r}});
        }
    }
}

Tile* Grid::FindTile(Hex hex) {
    for (Tile& tile : Tiles) {
        if (tile.Hex == hex) {
            return &tile;
        }
    }
    return nullptr;
}

void BuildStraightPath(Path* out, Hex start, int dir, int steps) {
    out->Size = 0;
    Hex hex = start;
    out->Push(hex);
    for (int i = 0; i < steps; ++i) {
        hex = hex.Neighbour(dir);
        out->Push(hex);
    }
}

void World::InitLevel() {
    Grid.InitRing(3);
    BuildStraightPath(&Path, Hex{-3, 0}, 0, 6);
    for (const Hex& hex : Path) {
        if (Tile* tile = Grid.FindTile(hex)) {
            tile->IsPath = true;
        }
    }
}

void GameInit(PlatformState* ps, GameState* gs) {
    (void)ps;

    gs->World.InitLevel();

    printf("[game] GameInit\n");
}

void GameUpdate(PlatformState* ps, GameState* gs) {
    (void)ps;

    gs->World.Count++;
}

void GameRender(PlatformState* ps, GameState* gs) {
    (void)ps;

    game_private::DrawHexGrid(&gs->World);

    // ImGui UI is submitted here (between the platform's NewFrame and Render). Draw data is
    // finalized and rendered by the platform in PlatformEndFrame.
    ImGui::ShowDemoWindow();

    ImGui::Begin("Dali");
    ImGui::Text("Hello from the game DLL");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("World Count: %d", gs->World.Count);
    ImGui::Text("DLL Reload Count: %d", gs->InternalDetectedReload);
    ImGui::End();
}

}  // namespace kdk
