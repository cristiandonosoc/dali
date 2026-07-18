#include <dali/game/game.h>

#include <dali/core/api.h>
#include <dali/core/color.h>
#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/hex.h>
#include <dali/game/imgui_widgets.h>
#include <dali/game/platform.h>
#include <dali/game/scene.h>
#include <dali/game/ui/ui.h>

#include <imgui.h>

#include <cstdio>
#include <optional>

namespace kdk {

// Tower tuning (range, fire interval, damage, projectile hit radius, cost) is authored per
// blueprint on TowerAsset::InstanceData and snapshotted onto each Tower at placement — there are no
// tower constants here.

// Economy + wave tuning.
constexpr int kStartingGold = 100;                // Gold the player begins a run with.
constexpr int kWaveBaseCount = 5;                 // Enemies in wave 1; grows by 1 each wave.
constexpr float kWaveHealthScalePerWave = 0.15f;  // +15% enemy HP for each wave past the first.

// UI / camera.
constexpr float kSidePanelWidth = 320.0f;  // Width of the docked left control panel, in pixels.
constexpr float kCameraPanSpeed = 500.0f;  // WASD camera pan speed, pixels per second.
constexpr float kZoomStep = 0.1f;          // Fractional zoom change per mouse-wheel notch.
constexpr float kZoomMin = 0.3f;           // Clamp so the world can't collapse or invert.
constexpr float kZoomMax = 3.0f;

namespace game_private {

// The six axial neighbour directions, flat-top, matching Hex::Direction. Kept local and constexpr
// so the spiral tables below can be generated at compile time (Hex's own methods are not
// constexpr).
constexpr Hex kHexDirections[6] = {
    {+1,  0},
    {+1, -1},
    { 0, -1},
    {-1,  0},
    {-1, +1},
    { 0, +1},
};

// The spiral (ring-major) slot->hex layout, generated from TileChunk::kRadius: slot 0 is the
// center, then ring k (1..kRadius) contributes 6k hexes, walked from the (-k,+k) corner around the
// 6 directions. Change kRadius and the tables follow — no hand-maintained data.
consteval Array<Hex, TileChunk::kTileCount> MakeSlotToHex() {
    Array<Hex, TileChunk::kTileCount> table = {};
    i32 slot = 0;
    table[slot] = Hex{0, 0};
    slot++;
    for (i32 k = 1; k <= TileChunk::kRadius; ++k) {
        Hex hex = {-k, k};
        for (i32 i = 0; i < 6; ++i) {
            for (i32 j = 0; j < k; ++j) {
                table[slot] = hex;
                slot++;
                hex = Hex{hex.Q + kHexDirections[i].Q, hex.R + kHexDirections[i].R};
            }
        }
    }
    return table;
}

// Inverse of MakeSlotToHex, indexed by the axial bounding box b = (q + kRadius) * kWidth + (r +
// kRadius). Box cells outside the ring stay NONE. Together the pair gives O(1) hex<->slot both
// ways.
consteval Array<i32, TileChunk::kWidth * TileChunk::kWidth> MakeHexToSlot() {
    Array<Hex, TileChunk::kTileCount> slot_to_hex = MakeSlotToHex();
    Array<i32, TileChunk::kWidth * TileChunk::kWidth> table = {};
    for (i32 box = 0; box < TileChunk::kWidth * TileChunk::kWidth; ++box) {
        table[box] = NONE;
    }
    for (i32 slot = 0; slot < TileChunk::kTileCount; ++slot) {
        Hex hex = slot_to_hex[slot];
        i32 box = (hex.Q + TileChunk::kRadius) * TileChunk::kWidth + (hex.R + TileChunk::kRadius);
        table[box] = slot;
    }
    return table;
}

constexpr Array<Hex, TileChunk::kTileCount> kSlotToHex = MakeSlotToHex();
constexpr Array<i32, TileChunk::kWidth * TileChunk::kWidth> kHexToSlot = MakeHexToSlot();

DrawContext NewDrawContext(PlatformState* ps, GameState* gs) {
    ImGuiIO& io = ImGui::GetIO();
    Vec2 origin = {io.DisplaySize.x * 0.5f + gs->Camera.x, io.DisplaySize.y * 0.5f + gs->Camera.y};

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    DrawContext dc = {
        .Registry = &gs->Registry,
        .DrawList = draw_list,
        .UI = UILayer::New(ps),
        .Origin = origin,
        .Zoom = gs->Zoom,
        .Time = (float)ps->TimeTracking.TotalSeconds,
        .ShowBounds = io.KeyCtrl,
    };

    return dc;
}

// Draws a short arrow from |from_center| toward |toward_center| (a neighbouring tile center),
// scaled to |size|. Used to visualize each path tile's PathDirection.
void DrawArrow(DrawContext* dc,
               ImVec2 from_center,
               ImVec2 toward_center,
               float size,
               Color32 color) {
    Vec2 delta(toward_center.x - from_center.x, toward_center.y - from_center.y);
    if (IsZero(delta)) {
        return;
    }
    Vec2 dir = Normalize(delta);

    float shaft = size * 0.42f;
    ImVec2 tail(from_center.x - dir.x * shaft * 0.35f, from_center.y - dir.y * shaft * 0.35f);
    ImVec2 tip(from_center.x + dir.x * shaft, from_center.y + dir.y * shaft);
    dc->DrawList->AddLine(tail, tip, color.Bits, 3.0f);

    // Two barbs: |dir| reversed, then rotated +/- 28 degrees and scaled down.
    constexpr float kHead = 10.0f;
    float a = ToRadians(28.0f);
    Vec2 back(-dir.x, -dir.y);
    Vec2 left(back.x * Cos(a) - back.y * Sin(a), back.x * Sin(a) + back.y * Cos(a));
    Vec2 right(back.x * Cos(-a) - back.y * Sin(-a), back.x * Sin(-a) + back.y * Cos(-a));
    dc->DrawList->AddLine(tip,
                          ImVec2(tip.x + left.x * kHead, tip.y + left.y * kHead),
                          color.Bits,
                          3.0f);
    dc->DrawList->AddLine(tip,
                          ImVec2(tip.x + right.x * kHead, tip.y + right.y * kHead),
                          color.Bits,
                          3.0f);
}

ImVec2 ToImVec2(const Vec2& v) { return {v.x, v.y}; }
Vec2 ToVec2(const ImVec2& v) { return {v.x, v.y}; }

// Draws a horizontal health bar (black backdrop + colored fill) centered at |center_x| with its top
// at |top_y|. |fraction| is clamped to [0,1]; an empty bar still shows the backdrop.
void DrawHealthBar(DrawContext* dc,
                   float center_x,
                   float top_y,
                   float width,
                   float fraction,
                   Color32 fill) {
    constexpr float kHeight = 3.0f;
    float f = Clamp(fraction, 0.0f, 1.0f);
    ImVec2 bar_min(center_x - width * 0.5f, top_y);
    ImVec2 bar_max(bar_min.x + width, bar_min.y + kHeight);
    ImVec2 fill_max(bar_min.x + width * f, bar_max.y);
    dc->DrawList->AddRectFilled(bar_min, bar_max, Color32::Black.Bits);
    dc->DrawList->AddRectFilled(bar_min, fill_max, fill.Bits);
    dc->DrawList->AddRect(bar_min, bar_max, Color32::Black.Bits);
}

// A small magenta crosshair + dot at a sprite's pivot (its anchor point), so the pivot is
// distinguishable from the green bounds box. Debug overlay, gated by DrawContext::ShowBounds.
void DrawPivotMarker(DrawContext* dc, const ImVec2& pivot) {
    constexpr float kArm = 5.0f;
    dc->DrawList->AddLine(ImVec2(pivot.x - kArm, pivot.y),
                          ImVec2(pivot.x + kArm, pivot.y),
                          Color32::Magenta.Bits,
                          1.5f);
    dc->DrawList->AddLine(ImVec2(pivot.x, pivot.y - kArm),
                          ImVec2(pivot.x, pivot.y + kArm),
                          Color32::Magenta.Bits,
                          1.5f);
    dc->DrawList->AddCircleFilled(pivot, 1.5f, Color32::Magenta.Bits);
}

void DrawSpriteSheetClip(DrawContext* dc,
                         const SpriteSheetClipReference& ref,
                         const SpriteSheetClip& clip,
                         const ImVec2& pos) {
    i32 frame = clip.At(dc->Time, clip.FPS, true);
    // The clip's pivot lands on |pos|: take the centered quad and shift it by the pivot offset.
    Vec2 draw_size = clip._CellSize * dc->Zoom;
    ImVec2 hsize = ToImVec2(draw_size / 2.0f);
    ImVec2 off = ToImVec2(clip.PivotOffset(draw_size));
    ImVec2 pmin(pos.x - hsize.x + off.x, pos.y - hsize.y + off.y);
    ImVec2 pmax(pos.x + hsize.x + off.x, pos.y + hsize.y + off.y);

    FrameUv frameuv = clip.CellRect(frame);
    ImVec2 uv0 = ToImVec2(frameuv.Uv0);
    ImVec2 uv1 = ToImVec2(frameuv.Uv1);
    if (ref.FlipX) {
        // ImGui samples min.u -> max.u across the quad, so swapping the U bounds mirrors
        // horizontally.
        float u = uv0.x;
        uv0.x = uv1.x;
        uv1.x = u;
    }

    dc->DrawList->AddImage(clip._Resolved->Resource.ImGuiId(), pmin, pmax, uv0, uv1);

    if (dc->ShowBounds) {
        dc->DrawList->AddRect(pmin, pmax, Color32::Green.Bits, 0.0f, 0, 1.5f);
        DrawPivotMarker(dc, pos);
    }
}

void DrawEnemy(DrawContext* dc, const Enemy& enemy) {
    ImVec2 p = dc->WorldToScreen(enemy.Position);
    // dc->DrawList->AddCircleFilled(p, 6.0f * dc->Zoom, enemy.Data.Color.Bits);
    // dc->DrawList->AddCircle(p, 6.0f * dc->Zoom, Color32::Black.Bits, 0, 1.5f);

    // The slot for this facing, resolved live. A drawn enemy is expected to carry a valid, baked
    // walk clip for whichever way it faces; a missing one is a data error, not a
    // runtime-recoverable state.
    const SpriteSheetClipReference& ref = enemy.Data.Walk.Resolve(enemy.Facing);
    const SpriteSheetClip* clip = ref.Resolve(*dc->Registry);
    ASSERT(clip);
    ASSERT(clip->_Resolved);

    DrawSpriteSheetClip(dc, ref, *clip, p);

    // Health bar, only once the enemy has taken damage (a full bar would just be clutter).
    if (enemy.Data.MaxHealth > 0.0f && enemy.Health < enemy.Data.MaxHealth) {
        DrawHealthBar(dc,
                      p.x,
                      p.y - 12.0f * dc->Zoom,
                      18.0f * dc->Zoom,
                      enemy.Health / enemy.Data.MaxHealth,
                      Color32::Green);
    }
}

void DrawTower(DrawContext* dc, const Tile& tile, const Tower& tower) {
    ImVec2 center = dc->TileCenter(tile.Hex);
    // The tower's idle animation, from the blueprint it snapshotted at placement (resolved live, so
    // editing the sheet shows up immediately). Falls back to a placeholder disc when no clip
    // resolves, so an art-less tower is still visible.
    const SpriteSheetClip* clip = tower.Data.IdleClip.Resolve(*dc->Registry);

    i32 pos = NONE;

    bool drawable = clip != nullptr;
    if (drawable) {
        drawable &= clip->_Resolved != nullptr;
    }
    if (drawable) {
        pos = clip->At(dc->Time, clip->FPS, true);
        drawable &= pos != NONE;
        drawable &= pos < clip->_Frames.Size;
    }
    if (drawable) {
        FrameUv frameuv = clip->_Frames[pos];
        ImVec2 uv0 = ToImVec2(frameuv.Uv0);
        ImVec2 uv1 = ToImVec2(frameuv.Uv1);
        if (tower.Data.IdleClip.FlipX) {
            // Swapping the U bounds mirrors horizontally (ImGui samples min.u -> max.u).
            float u = uv0.x;
            uv0.x = uv1.x;
            uv1.x = u;
        }
        // The clip's pivot lands on the tile center: shift the centered quad by the pivot offset.
        Vec2 draw_size = clip->_CellSize * dc->Zoom;
        ImVec2 hsize = ToImVec2(draw_size / 2.0f);
        ImVec2 off = ToImVec2(clip->PivotOffset(draw_size));
        ImVec2 pmin(center.x - hsize.x + off.x, center.y - hsize.y + off.y);
        ImVec2 pmax(center.x + hsize.x + off.x, center.y + hsize.y + off.y);
        dc->DrawList->AddImage((ImTextureID)clip->_Handle, pmin, pmax, uv0, uv1);
        if (dc->ShowBounds) {
            dc->DrawList->AddRect(pmin, pmax, Color32::Green.Bits, 0.0f, 0, 1.5f);
            DrawPivotMarker(dc, center);
        }
    } else {
        dc->DrawList->AddCircleFilled(center, 10.0f * dc->Zoom, Color32::SteelBlue.Bits);
        dc->DrawList->AddCircle(center, 10.0f * dc->Zoom, Color32::White.Bits, 0, 2.0f);
        if (dc->ShowBounds) {
            ImVec2 hsize(10.0f * dc->Zoom, 10.0f * dc->Zoom);
            dc->DrawList
                ->AddRect(center - hsize, center + hsize, Color32::Green.Bits, 0.0f, 0, 1.5f);
            DrawPivotMarker(dc, center);
        }
    }

    // Seconds until this tower can fire again (0.00 = ready), so cooldown behavior is
    // visible at a glance.
    char cooldown_label[16];
    snprintf(cooldown_label, sizeof(cooldown_label), "%.2f", tower.FireCooldown);
    dc->DrawList->AddText(ImVec2(center.x + 12.0f * dc->Zoom, center.y - 8.0f * dc->Zoom),
                          Color32::White.Bits,
                          cooldown_label);
}

void DrawTileContent(DrawContext* dc, const Tile& tile, const std::optional<Hex>& hovered) {
    ImVec2 center = dc->TileCenter(tile.Hex);
    if (tile.Content == ETileContent::Spawner) {
        dc->DrawList->AddCircleFilled(center, 10.0f * dc->Zoom, Color32::Green.Bits);
        dc->DrawList->AddCircle(center, 10.0f * dc->Zoom, Color32::White.Bits, 0, 2.0f);
    } else if (tile.Content == ETileContent::Tower) {
        ASSERT(tile.Tower.has_value());  // MakeTower/ClearTileContent keep these in step
        const Tower& tower = *tile.Tower;
        DrawTower(dc, tile, tower);

        // The range ring only shows while hovering this tower, so a dense field stays readable.
        if (tile.Hex == hovered) {
            dc->DrawList->AddCircle(center,
                                    tower.Data.Range * dc->Zoom,
                                    Color32::Cyan.Bits,
                                    0,
                                    2.0f);
        }
    }
}

// Debug-draws the grid straight onto ImGui's background draw list. Per milestone-01 step 3 there is
// no engine render layer yet — this is the temporary draw path that lets the grid/hover/path steps
// proceed before we know which primitives the real renderer needs. The hex under the mouse is
// highlighted, which also proves the world<->hex transform round-trips.
//
// Returns the hovered hex, empty when the mouse belongs to a higher tier (|ui| is consulted, not
// ImGui directly — it already folds ImGui's claim in). Acting on that hex is the caller's call:
// the game is the lowest tier, so it can only decide once every tier above it has been emitted.
std::optional<Hex> DrawHexGrid(DrawContext* dc, World* world) {
    float zoom = dc->Zoom;

    // Which hex is under the mouse? Invert the exact transform the context paints with (undo zoom,
    // then origin), so the highlight lines up regardless of the screen's Y-down convention and
    // picking stays in lockstep with rendering. Stays empty when a higher tier owns the mouse, so
    // no hex lights up under the debug panel or behind a UI button. Empty space with no tile still
    // hovers, so ops like AddChunk can target a void.
    std::optional<Hex> hovered = {};
    if (!dc->UI.MouseCaptured) {
        Vec2 mouse_world((dc->UI.MousePos.x - dc->Origin.x) / zoom,
                         (dc->UI.MousePos.y - dc->Origin.y) / zoom);
        hovered = Hex::WorldToHex(kHexSize, mouse_world);
    }

    for (i32 chunk_index = 0; chunk_index < world->Grid.Chunks.Size; ++chunk_index) {
        const TileChunk& chunk = world->Grid.Chunks[chunk_index];
        for (i32 slot = 0; slot < chunk.Tiles.Size; ++slot) {
            const Tile& tile = chunk.Tiles[slot];
            ImVec2 center = dc->TileCenter(tile.Hex);

            ImVec2 corners[6];
            for (int i = 0; i < 6; ++i) {
                Vec2 c = Hex::HexCorner(kHexSize * zoom, Vec2(center.x, center.y), i);
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

            dc->DrawList->AddConvexPolyFilled(corners, 6, fill.Bits);
            dc->DrawList->AddPolyline(corners, 6, Color32::White.Bits, ImDrawFlags_Closed, 2.0f);

            // // Debug label: absolute hex, then chunk:slot (slots repeat per chunk, so both are
            // // shown).
            // char coord[64];
            // snprintf(coord,
            //          sizeof(coord),
            //          "(%d,%d)\n%d:%d",
            //          tile.Hex.Q,
            //          tile.Hex.R,
            //          chunk_index,
            //          slot);
            // draw_list->AddText(ImVec2(center.x + -12.0f * zoom, center.y - 8.0f * zoom),
            //                    Color32::White.Bits,
            //                    coord);
        }
    }

    // Flow-field arrows: each path tile points toward the neighbour that steps one hex closer to
    // the goal (its PathDirection). Tiles with no route to the goal keep NONE and draw nothing.
    world->Grid.ForEachTile([&](Tile* tile) {
        if (!tile->IsPath || tile->PathDirection == NONE) {
            return;
        }
        ImVec2 center = dc->TileCenter(tile->Hex);
        ImVec2 toward = dc->TileCenter(tile->Hex.Neighbour(tile->PathDirection));
        DrawArrow(dc, center, toward, kHexSize * zoom, Color32::BrightGold);
    });

    // Buildings: a tile holds at most one. Spawners (enemy origins) are green discs; towers are
    // blue discs ringed by a faint circle showing their firing range.
    world->Grid.ForEachTile([&](Tile* tile) { DrawTileContent(dc, *tile, hovered); });

    // Spawn sources: the outskirt tiles waves emit from (derived path leaves). A green ring marks
    // each so you can see where enemies will enter.
    for (const Hex& source : world->SpawnSources) {
        ImVec2 center = dc->TileCenter(source);
        dc->DrawList->AddCircle(center, kHexSize * 0.55f * zoom, Color32::Green.Bits, 6, 3.0f);
    }

    // The goal: the tile every path drains into. Its base-health bar is always shown so you can
    // watch it drop as enemies break through.
    if (world->Goal.has_value()) {
        ImVec2 center = dc->TileCenter(*world->Goal);
        dc->DrawList->AddCircleFilled(center, 9.0f * zoom, Color32::Red.Bits);
        DrawHealthBar(dc,
                      center.x,
                      center.y - 18.0f * zoom,
                      28.0f * zoom,
                      world->BaseHealth / World::kMaxBaseHealth,
                      Color32::Green);
    }

    // Enemies walking the flow field from spawner toward the goal.
    for (const Enemy& enemy : world->Enemies) {
        DrawEnemy(dc, enemy);
    }

    // Tower projectiles in flight toward their targets.
    for (const Projectile& projectile : world->Projectiles) {
        ImVec2 p = dc->WorldToScreen(projectile.Position);
        dc->DrawList->AddCircleFilled(p, 3.0f * zoom, Color32::Yellow.Bits);
    }

    return hovered;
}

}  // namespace game_private

void TileChunk::InitRing(Hex offset) {
    Offset = offset;
    Tiles.Clear();
    for (i32 slot = 0; slot < kTileCount; ++slot) {
        Hex relative = game_private::kSlotToHex[slot];
        Tiles.Push(Tile{.Hex = offset.Add(relative)});
    }
}

i32 TileChunk::HexToIndex(const Hex& hex) {
    if (AbsI(hex.Q) > kRadius) {
        return NONE;
    }
    if (AbsI(hex.R) > kRadius) {
        return NONE;
    }
    i32 box = (hex.Q + kRadius) * kWidth + (hex.R + kRadius);
    return game_private::kHexToSlot[box];
}

Tile* TileChunk::FindTile(const Hex& hex) {
    Hex relative = hex.Substract(Offset);
    i32 index = HexToIndex(relative);
    if (index == NONE) {
        return nullptr;
    }
    if (index >= Tiles.Size) {
        return nullptr;
    }
    return &Tiles[index];
}

Hex TileChunk::NeighbourChunkOffset(int dir) {
    Hex a = Hex::Direction(dir);
    Hex b = Hex::Direction((dir + 1) % 6);
    return Hex{(kRadius + 1) * a.Q + kRadius * b.Q, (kRadius + 1) * a.R + kRadius * b.R};
}

void Grid::Init() {
    Chunks.Clear();
    AddChunk(Hex{0, 0});
}

TileChunk* Grid::AddChunk(Hex center) {
    if (Chunks.IsFull()) {
        return nullptr;
    }
    TileChunk& chunk = Chunks.Push(TileChunk{});
    chunk.InitRing(center);
    return &chunk;
}

Tile* Grid::FindTile(const Hex& hex) {
    // TODO(perf): linear scan over chunks. Replace with an absolute-hex -> chunk index map once the
    // chunk count grows. The distance guard keeps each miss to a cheap subtract, no table probe.
    for (TileChunk& chunk : Chunks) {
        if (Hex::Distance(hex, chunk.Offset) > TileChunk::kRadius) {
            continue;
        }
        if (Tile* tile = chunk.FindTile(hex)) {
            return tile;
        }
    }
    return nullptr;
}

void Grid::ForEachTile(const Function<void(Tile*)>& fn) {
    for (TileChunk& chunk : Chunks) {
        for (Tile& tile : chunk.Tiles) {
            fn(&tile);
        }
    }
}

void World::CalculatePath() {
    Grid.ForEachTile([](Tile* tile) { tile->PathDirection = NONE; });

    if (!Goal.has_value()) {
        return;
    }
    Tile* goal_tile = Grid.FindTile(*Goal);
    if (!goal_tile || !goal_tile->IsPath) {
        return;
    }

    // BFS outward from the goal over connected path tiles. Each tile records the direction that
    // steps toward the tile it was reached from — i.e. one hex closer to the goal. A real direction
    // (0..5) doubles as the "visited" mark; the goal stays NONE (you are already home).
    FixedVector<Hex, TileChunk::kTileCount * Grid::kMaxChunks> frontier;
    frontier.Push(*Goal);
    for (i32 head = 0; head < frontier.Size; ++head) {
        Hex current = frontier[head];
        for (int dir = 0; dir < 6; ++dir) {
            Hex neighbour = current.Neighbour(dir);
            Tile* tile = Grid.FindTile(neighbour);
            if (!tile || !tile->IsPath) {
                continue;
            }
            if (neighbour == *Goal || tile->PathDirection != NONE) {
                continue;  // the root, or already visited
            }
            tile->PathDirection = (dir + 3) % 6;  // opposite of |dir|: steps back toward |current|
            frontier.Push(neighbour);
        }
    }
}

void World::SpawnEnemy(const EnemyAsset& blueprint, Hex at, float health_scale) {
    if (Enemies.IsFull()) {
        return;
    }
    Enemy enemy = {};
    enemy.Id = NextEnemyId;
    NextEnemyId++;
    enemy.Position = Hex::HexToWorld(kHexSize, at);
    enemy.Target = at;  // retargets to the next flow-field tile on the first update
    enemy.Data = blueprint.PerInstanceData;  // snapshot the stats; the blueprint is not retained
    enemy.Data.MaxHealth *= health_scale;
    enemy.Health = enemy.Data.MaxHealth;
    enemy.Blueprint = blueprint.Manifest.Id;
    Enemies.Push(enemy);
}

Enemy* World::FindEnemy(u32 id) {
    if (id == 0) {
        return nullptr;
    }
    for (Enemy& enemy : Enemies) {
        if (enemy.Id == id) {
            return &enemy;
        }
    }
    return nullptr;
}

void MakeSpawner(Tile* tile) {
    tile->Content = ETileContent::Spawner;
    tile->SpawnTimer = random::FloatUNI();  // phase offset in [0,1)s so spawners don't sync up
}

void MakeTower(Tile* tile, const TowerAsset& blueprint) {
    Tower tower = {};
    tower.Data = blueprint.PerInstanceData;  // snapshot the stats; the blueprint is not retained
    tower.FireCooldown = 0.0f;               // ready to fire the moment an enemy walks into range
    tower.Blueprint = blueprint.Manifest.Id;

    tile->Content = ETileContent::Tower;
    tile->Tower = tower;
}

void ClearTileContent(Tile* tile) {
    tile->Content = ETileContent::None;
    tile->Tower.reset();
    tile->SpawnTimer = 0.0f;
}

void World::BeginRun() {
    Gold = kStartingGold;
    BaseHealth = kMaxBaseHealth;
    Wave = {};
    Enemies.Clear();
    Projectiles.Clear();

    // Start with no towers; PreGame only lays out terrain (path + goal).
    Grid.ForEachTile([](Tile* tile) { ClearTileContent(tile); });

    CalculatePath();
    CollectSpawnSources();
}

void World::CollectSpawnSources() {
    SpawnSources.Clear();
    Grid.ForEachTile([&](Tile* tile) {
        if (!tile->IsPath) {
            return;
        }
        if (tile->PathDirection == NONE) {
            return;  // no route to the goal, so nothing spawns here
        }

        // A source is a path tile that no neighbouring path tile flows into.
        bool has_incoming = false;
        for (int dir = 0; dir < 6; ++dir) {
            Tile* neighbour = Grid.FindTile(tile->Hex.Neighbour(dir));
            if (!neighbour) {
                continue;
            }
            if (!neighbour->IsPath) {
                continue;
            }
            if (neighbour->PathDirection == NONE) {
                continue;
            }
            Hex flows_to = neighbour->Hex.Neighbour(neighbour->PathDirection);
            if (flows_to == tile->Hex) {
                has_incoming = true;
                break;
            }
        }

        if (has_incoming) {
            return;
        }
        if (SpawnSources.IsFull()) {
            return;
        }
        SpawnSources.Push(tile->Hex);
    });
}

void World::ArmNextWave() {
    Wave.Number++;
    Wave.ToSpawn = kWaveBaseCount + Wave.Number;
    Wave.SpawnTimer = 0.0f;
}

void World::ResetTowerCooldowns() {
    Grid.ForEachTile([](Tile* tile) {
        if (!tile->Tower.has_value()) {
            return;
        }
        tile->Tower->FireCooldown = 0.0f;
    });
}

void World::UpdateWave(float dt, const EnemyAsset& blueprint) {
    if (Wave.ToSpawn <= 0) {
        return;
    }
    if (SpawnSources.IsEmpty()) {
        return;
    }

    Wave.SpawnTimer += dt;
    if (Wave.SpawnTimer < Wave.Cadence) {
        return;
    }
    Wave.SpawnTimer -= Wave.Cadence;

    int index = Wave.SpawnCursor % SpawnSources.Size;
    Wave.SpawnCursor++;
    Hex source = SpawnSources[index];

    float health_scale = 1.0f + kWaveHealthScalePerWave * (float)(Wave.Number - 1);
    SpawnEnemy(blueprint, source, health_scale);
    Wave.ToSpawn--;
}

void World::UpdateEnemies(float dt) {
    constexpr float kArriveThreshold = 2.0f;  // Pixels; within this we hop to the next tile.

    for (i32 i = 0; i < Enemies.Size;) {
        Enemy& enemy = Enemies[i];

        // Spend this frame's travel budget, walking through as many tiles as it reaches.
        float remaining = enemy.Data.Speed * dt;
        bool arrived = false;
        while (remaining > 0.0f) {
            Vec2 target_pos = Hex::HexToWorld(kHexSize, enemy.Target);
            Vec2 delta(target_pos.x - enemy.Position.x, target_pos.y - enemy.Position.y);
            float dist = Length(delta);

            if (dist <= kArriveThreshold) {
                enemy.Position = target_pos;  // snap to the center, then pick the next tile
                Tile* tile = Grid.FindTile(enemy.Target);
                if (!tile || tile->PathDirection == NONE) {
                    arrived = true;  // reached the goal (or the path was removed under it)
                    break;
                }
                enemy.Target = enemy.Target.Neighbour(tile->PathDirection);
                continue;
            }

            Vec2 dir = Normalize(delta);
            enemy.Facing =
                FacingFromDir(dir);  // only while actually moving; a stopped enemy holds it
            float move = Min(remaining, dist);
            enemy.Position.x += dir.x * move;
            enemy.Position.y += dir.y * move;
            remaining -= move;
        }

        if (arrived) {
            BaseHealth = Max(0.0f, BaseHealth - enemy.Data.Damage);  // enemy breached the goal
            Enemies.RemoveUnorderedAt(i);  // swaps the last enemy into i; reprocess it
        } else {
            ++i;
        }
    }
}

void World::UpdateTowers(float dt) {
    // Precompute the base position once; targeting prioritizes the enemy nearest it (the Rogue
    // Tower default — punish the enemy about to break through). Without a goal, fall back to
    // nearest-to-tower.
    std::optional<Vec2> goal_pos;
    if (Goal.has_value()) {
        goal_pos = Hex::HexToWorld(kHexSize, *Goal);
    }

    Grid.ForEachTile([&](Tile* tile) {
        if (!tile->Tower.has_value()) {
            return;
        }
        Tower& tower = *tile->Tower;

        tower.FireCooldown -= dt;
        if (tower.FireCooldown > 0.0f) {
            return;  // still cooling down
        }
        // Ready to fire. Clamp to 0 so a tower with no target in range holds at "ready" instead of
        // drifting ever more negative while it waits.
        tower.FireCooldown = 0.0f;

        // Among enemies in range, pick the one closest to the base. Compare squared distances to
        // skip the sqrt.
        Vec2 tower_pos = Hex::HexToWorld(kHexSize, tile->Hex);
        float range_sq = tower.Data.Range * tower.Data.Range;
        Enemy* target = nullptr;
        float best_priority = 0.0f;  // lower wins; meaning depends on goal_pos
        for (Enemy& enemy : Enemies) {
            float d_sq = LengthSq(enemy.Position - tower_pos);
            if (d_sq > range_sq) {
                continue;  // out of range
            }
            float priority = goal_pos.has_value() ? LengthSq(enemy.Position - *goal_pos) : d_sq;
            if (!target || priority < best_priority) {
                best_priority = priority;
                target = &enemy;
            }
        }

        if (!target || Projectiles.IsFull()) {
            return;  // nothing in range (or no room) — hold fire, retry next frame
        }

        Projectile projectile = {};
        projectile.Position = tower_pos;
        projectile.LastSeen =
            target->Position;  // seed; refreshed each frame while the target lives
        projectile.TargetId = target->Id;
        projectile.Damage = tower.Data.Damage;
        projectile.HitRadius = tower.Data.ProjectileHitRadius;
        Projectiles.Push(projectile);
        tower.FireCooldown = tower.Data.FireInterval;
    });
}

void World::UpdateProjectiles(float dt) {
    for (i32 i = 0; i < Projectiles.Size;) {
        Projectile& projectile = Projectiles[i];

        // Refresh the aim point while the target is alive; once it dies we keep the last one so the
        // shot still flies out and lands instead of vanishing.
        Enemy* target = FindEnemy(projectile.TargetId);
        if (target) {
            projectile.LastSeen = target->Position;
        }

        Vec2 delta = projectile.LastSeen - projectile.Position;
        if (LengthSq(delta) <= projectile.HitRadius * projectile.HitRadius) {
            // Reached the aim point. Only deal damage if the target is still there to take it.
            if (target) {
                target->Health -= projectile.Damage;
                if (target->Health <= 0.0f) {
                    Gold += target->Data.Reward;  // bounty for the kill
                    Enemies.RemoveUnorderedAt((i32)(target - Enemies.begin()));
                }
            }
            Projectiles.RemoveUnorderedAt(i);
            continue;
        }

        Vec2 dir = Normalize(delta);
        projectile.Position += dir * (projectile.Speed * dt);
        ++i;
    }
}

constexpr StringView kScenePath = "extras/scene.yaml"sv;

void GameInit(PlatformState* ps, GameState* gs) {
    (void)ps;

    // Load all baked assets once. GL is already live (OnSOLoaded ran first), and the registry lives
    // in the PermanentArena, so this does not repeat on reload. This runs before LoadScene, which
    // stamps scene towers from the registry.
    gs->Registry.CrawlAndLoad();

    // The blueprint waves spawn until wave-composition exists. If the asset is absent, UpdateWave
    // falls back to a default-stat enemy, so this id being unresolved is not fatal.
    gs->World.DefaultEnemy = AssetId::Normalize("enemies/wolf"sv);
    // The build menu's initial selection; the player switches it in the side panel. Unresolved is
    // not fatal — the tower falls back to default stats and a placeholder disc.
    gs->World.SelectedTower = AssetId::Normalize("towers/arrow"sv);

    // Reload the last saved scene; fall back to an empty grid the first time (no file yet). The
    // player lays out the path/goal in the PreGame editor.
    if (!LoadScene(&gs->World, &gs->Registry, kScenePath)) {
        gs->World.Grid.Init();
    }
    gs->World.CollectSpawnSources();  // so the outskirts are visible before the first path edit

    printf("[game] GameInit\n");
}

void GameUpdate(PlatformState* ps, GameState* gs) {
    float dt = (float)ps->TimeTracking.DeltaSeconds;

    World& world = gs->World;

    // IsKeyPressed is gated on ImGui's WantCaptureKeyboard, so typing "/" into a filter box edits
    // the text instead of toggling the panel.
    if (ps->Input.IsKeyPressed(EKey::Slash)) {
        gs->ShowDebugPanel = !gs->ShowDebugPanel;
    }

    // Camera pan (WASD) works in every phase: it just offsets the world's draw origin. W/A pan the
    // view up/left (content slides down/right), S/D the opposite.
    Vec2 pan = {};
    float multiple = gs->InverseCameraMovement ? -1.0f : 1.0f;
    if (ps->Input.IsKeyDown(EKey::W)) {
        pan.y += 1.0f * multiple;
    }
    if (ps->Input.IsKeyDown(EKey::S)) {
        pan.y -= 1.0f * multiple;
    }
    if (ps->Input.IsKeyDown(EKey::A)) {
        pan.x += 1.0f * multiple;
    }
    if (ps->Input.IsKeyDown(EKey::D)) {
        pan.x -= 1.0f * multiple;
    }
    gs->Camera.x += pan.x * kCameraPanSpeed * dt;
    gs->Camera.y += pan.y * kCameraPanSpeed * dt;

    // Mouse-wheel zoom, unless ImGui is using the wheel (e.g. hovering the docked panel). Zoom only
    // scales the world->screen mapping; the sim stays in world units.
    if (!ps->Input.MouseOverride) {
        float scroll = ps->Input.MouseScroll.y;
        gs->Zoom *= (1.0f + kZoomStep * scroll);
        gs->Zoom = Clamp(gs->Zoom, kZoomMin, kZoomMax);
    }

    if (gs->Phase != EGamePhase::Wave) {
        return;  // PreGame / Build / GameOver freeze the sim
    }

    // Resolve the wave's enemy blueprint here (the sim is registry-agnostic). Fall back to a
    // default blueprint if the id is missing, so the game still runs before any enemy asset is
    // authored.
    const EnemyAsset* blueprint = gs->Registry.FindEnemyAsset(world.DefaultEnemy);
    EnemyAsset fallback = {};
    world.UpdateWave(dt, blueprint ? *blueprint : fallback);
    world.UpdateEnemies(dt);
    world.UpdateTowers(dt);
    world.UpdateProjectiles(dt);

    if (world.BaseHealth <= 0.0f) {
        gs->Phase = EGamePhase::GameOver;
        return;
    }
    if (world.Wave.ToSpawn == 0 && world.Enemies.Size == 0) {
        world.Projectiles.Clear();
        world.ResetTowerCooldowns();
        gs->Phase = EGamePhase::Prepare;  // wave cleared; back to building
    }
}

StringView ToString(EOperationMode mode) {
    // clang-format off
    switch (mode) {
        case EOperationMode::TogglePath: return "Toggle Path"sv;
        case EOperationMode::SetPathGoal: return "Set Path Goal"sv;
        case EOperationMode::ToggleSpawner: return "Toggle Spawner"sv;
        case EOperationMode::ToggleTower: return "Toggle Tower"sv;
        case EOperationMode::AddChunk: return "Add Chunk"sv;
    }
    // clang-format on
    ASSERT(false);
    return "<unknown>"sv;
}

StringView ToString(EAppMode mode) {
    // clang-format off
    switch (mode) {
        case EAppMode::Game:   return "Game"sv;
        case EAppMode::Assets: return "Assets"sv;
    }
    // clang-format on
    ASSERT(false);
    return "<unknown>"sv;
}

StringView ToString(EGamePhase phase) {
    // clang-format off
    switch (phase) {
        case EGamePhase::PreGame:  return "PreGame"sv;
        case EGamePhase::Prepare:    return "Prepare"sv;
        case EGamePhase::Wave:     return "Wave"sv;
        case EGamePhase::GameOver: return "GameOver"sv;
    }
    // clang-format on
    ASSERT(false);
    return "<unknown>"sv;
}

namespace game_private {

// The blueprint the build cursor places (World::SelectedTower, chosen in the build menu). Resolving
// happens here rather than in the sim, which stays registry-agnostic; a missing asset resolves to
// default stats so the game still runs before any tower asset is authored. By value: TowerAsset is
// a small value blob and placement happens at click rate, which buys a caller-side fallback local
// at every site.
TowerAsset ResolveSelectedTower(const World& world, AssetRegistry* registry) {
    const TowerAsset* blueprint = registry->FindTowerAsset(world.SelectedTower);
    if (!blueprint) {
        return {};
    }
    return *blueprint;
}

// One drawable animation frame: everything AddImage needs to paint a clip's current frame anywhere
// (thumbnails, palettes), with FlipX already applied to the UVs.
struct ClipFrameDraw {
    ImTextureID Handle = 0;
    ImVec2 Uv0 = {};
    ImVec2 Uv1 = {};
    Vec2 CellSize = {};
};

// Resolves |ref| live and samples its clip at |time| (looping). nullopt when the clip is missing or
// not baked, so callers fall back to a placeholder.
std::optional<ClipFrameDraw> ResolveClipFrame(const SpriteSheetClipReference& ref,
                                              AssetRegistry* registry,
                                              float time) {
    const SpriteSheetClip* clip = ref.Resolve(*registry);
    bool drawable = clip != nullptr;
    if (drawable) {
        drawable &= clip->_Resolved != nullptr;
    }
    i32 pos = NONE;
    if (drawable) {
        pos = clip->At(time, clip->FPS, true);
        drawable &= pos != NONE;
        drawable &= pos < clip->_Frames.Size;
    }
    if (!drawable) {
        return std::nullopt;
    }

    FrameUv uv = clip->_Frames[pos];
    ClipFrameDraw frame = {};
    frame.Handle = (ImTextureID)clip->_Handle;
    frame.Uv0 = ImVec2(uv.Uv0.x, uv.Uv0.y);
    frame.Uv1 = ImVec2(uv.Uv1.x, uv.Uv1.y);
    if (ref.FlipX) {
        float u = frame.Uv0.x;
        frame.Uv0.x = frame.Uv1.x;
        frame.Uv1.x = u;
    }
    frame.CellSize = clip->_CellSize;
    return frame;
}

// The tower build menu: one row per loaded TowerAsset (animated idle-clip thumbnail, name, cost);
// clicking a row makes it the blueprint the build cursor places (World::SelectedTower). Each row is
// a full-width Selectable for hover/selection feedback, with the content painted over it through
// the window draw list. The cost turns red while unaffordable in Build (where placing charges
// gold); PreGame placement is free, so it stays neutral there.
void DrawTowerBuildMenu(GameState* gs, float time) {
    constexpr float kThumb = 40.0f;
    constexpr float kPad = 4.0f;

    ImGui::SeparatorText("Towers");

    auto scratch = Arena::GetScratch();
    std::span<const AssetId*> ids =
        SortedFilteredIds(scratch, gs->Registry.TowerAssets, EAssetType::Tower, {});
    if (ids.empty()) {
        ImGui::TextWrapped("No tower assets loaded. Author one under Assets > Tower.");
        return;
    }

    for (const AssetId* id : ids) {
        const TowerAsset* tower = gs->Registry.FindTowerAsset(*id);
        if (!tower) {
            continue;
        }
        ImGui::PushID(id->Value.Str());

        ImVec2 row_min = ImGui::GetCursorScreenPos();
        bool selected = (*id == gs->World.SelectedTower);
        float row_height = kThumb + kPad * 2.0f;
        if (ImGui::Selectable("##tower_row", selected, 0, ImVec2(0.0f, row_height))) {
            gs->World.SelectedTower = *id;
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Thumbnail: the idle clip's current frame, aspect-fit into a kThumb box. Falls back to the
        // same placeholder disc the world draws for a clip-less tower, so the two always match.
        ImVec2 box_min(row_min.x + kPad, row_min.y + kPad);
        std::optional<ClipFrameDraw> frame =
            ResolveClipFrame(tower->PerInstanceData.IdleClip, &gs->Registry, time);
        if (frame.has_value()) {
            bool sizable = frame->CellSize.x > 0.0f;
            sizable &= frame->CellSize.y > 0.0f;
            float scale = 1.0f;
            if (sizable) {
                scale = Min(kThumb / frame->CellSize.x, kThumb / frame->CellSize.y);
            }
            Vec2 size = frame->CellSize * scale;
            ImVec2 img_min(box_min.x + (kThumb - size.x) * 0.5f,
                           box_min.y + (kThumb - size.y) * 0.5f);
            ImVec2 img_max(img_min.x + size.x, img_min.y + size.y);
            draw->AddImage(frame->Handle, img_min, img_max, frame->Uv0, frame->Uv1);
        } else {
            ImVec2 center(box_min.x + kThumb * 0.5f, box_min.y + kThumb * 0.5f);
            draw->AddCircleFilled(center, kThumb * 0.35f, Color32::SteelBlue.Bits);
            draw->AddCircle(center, kThumb * 0.35f, Color32::White.Bits, 0, 2.0f);
        }

        // Name + cost, right of the thumbnail.
        float text_x = box_min.x + kThumb + 8.0f;
        draw->AddText(ImVec2(text_x, row_min.y + kPad),
                      ImGui::GetColorU32(ImGuiCol_Text),
                      ShortId(EAssetType::Tower, *id).Str());
        char cost_label[32];
        snprintf(cost_label, sizeof(cost_label), "%d gold", tower->PerInstanceData.Cost);
        bool unaffordable = (gs->Phase == EGamePhase::Prepare);
        unaffordable &= (gs->World.Gold < tower->PerInstanceData.Cost);
        ImU32 cost_color =
            unaffordable ? IM_COL32(230, 90, 90, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddText(ImVec2(text_x, row_min.y + kPad + ImGui::GetTextLineHeightWithSpacing()),
                      cost_color,
                      cost_label);

        ImGui::PopID();
    }
}

// Adds a new chunk adjacent to the existing grid, in the direction of |clicked|. Chunk centers sit
// on the super-hex lattice (see TileChunk::NeighbourChunkOffset) so the clusters tile gaplessly and
// never overlap: it snaps to the neighbour slot of the nearest chunk that best faces the click.
// No-op if that slot is already filled (or the grid is empty/full).
void TryAddChunkToward(World* world, Hex clicked) {
    Grid& grid = world->Grid;
    if (grid.Chunks.IsEmpty()) {
        return;
    }
    if (grid.Chunks.IsFull()) {
        return;
    }

    // The chunk nearest the click is the one we expand from.
    TileChunk* nearest = &grid.Chunks[0];
    i32 best = Hex::Distance(clicked, nearest->Offset);
    for (TileChunk& chunk : grid.Chunks) {
        i32 d = Hex::Distance(clicked, chunk.Offset);
        if (d < best) {
            best = d;
            nearest = &chunk;
        }
    }

    // Of the 6 neighbouring chunk slots, pick the one facing the click.
    Hex target = nearest->Offset.Add(TileChunk::NeighbourChunkOffset(0));
    i32 target_dist = Hex::Distance(target, clicked);
    for (int dir = 1; dir < 6; ++dir) {
        Hex candidate = nearest->Offset.Add(TileChunk::NeighbourChunkOffset(dir));
        i32 d = Hex::Distance(candidate, clicked);
        if (d < target_dist) {
            target_dist = d;
            target = candidate;
        }
    }

    // Don't stack a chunk onto one that already exists there.
    for (TileChunk& chunk : grid.Chunks) {
        if (chunk.Offset == target) {
            return;
        }
    }

    grid.AddChunk(target);
}

// PreGame terrain editor: applies the current operation to the clicked tile. This is the M01
// editor, now scoped to the setup phase.
void ApplyEditorOp(GameState* gs, Hex hex) {
    // AddChunk targets empty space (a hex with no tile yet), so it runs before the tile lookup.
    if (gs->CurrentOperation == EOperationMode::AddChunk) {
        TryAddChunkToward(&gs->World, hex);
        return;
    }

    Tile* tile = gs->World.Grid.FindTile(hex);
    if (!tile) {
        return;
    }

    switch (gs->CurrentOperation) {
        case EOperationMode::TogglePath: {
            FLIP_BOOL(tile->IsPath);
            // A spawner can only sit on a path tile, so it goes when the path does.
            if (!tile->IsPath) {
                if (tile->Content == ETileContent::Spawner) {
                    tile->Content = ETileContent::None;
                }
            }
            gs->World.CalculatePath();
            gs->World.CollectSpawnSources();
            break;
        }
        case EOperationMode::SetPathGoal: {
            // The goal is a path tile every route drains into; make it path and re-flood.
            tile->IsPath = true;
            gs->World.Goal = hex;
            gs->World.CalculatePath();
            gs->World.CollectSpawnSources();
            break;
        }
        case EOperationMode::ToggleSpawner: {
            if (tile->Content == ETileContent::Spawner) {
                ClearTileContent(tile);
            } else if (tile->IsPath) {
                // Only path tiles can host a spawner. Placing it replaces any other content.
                MakeSpawner(tile);
            }
            break;
        }
        case EOperationMode::ToggleTower: {
            if (tile->Content == ETileContent::Tower) {
                ClearTileContent(tile);
            } else if (!tile->IsPath) {
                // Towers guard the path from beside it, so they only go on non-path tiles.
                MakeTower(tile, ResolveSelectedTower(gs->World, &gs->Registry));
            }
            break;
        }
        case EOperationMode::AddChunk: {
            break;  // handled before the tile lookup above
        }
    }
}

// Build-phase interaction: buy a tower on an empty, non-path tile if the player can afford it. The
// price is the blueprint's, so it is whatever the tower being placed costs.
void TryBuyTower(World* world, AssetRegistry* registry, Hex hex) {
    Tile* tile = world->Grid.FindTile(hex);
    if (!tile) {
        return;
    }
    if (tile->IsPath) {
        return;
    }
    if (tile->Content != ETileContent::None) {
        return;
    }
    TowerAsset blueprint = ResolveSelectedTower(*world, registry);
    if (world->Gold < blueprint.PerInstanceData.Cost) {
        return;
    }
    world->Gold -= blueprint.PerInstanceData.Cost;
    MakeTower(tile, blueprint);
}

// Rebuilds + restages the game DLL by running reload.bat (which the running platform notices and
// hot-reloads). Blocks until the build finishes. Returns 0 on success, 1 on failure - stored so the
// menu can show the outcome, and it survives the reload it triggers.
i32 RunHotReload() {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    StringView bat = paths::PathJoin(arena, paths::GetBaseDir(arena), "reload.bat"sv);
    // A .bat is not directly executable; cmd /c runs it. reload.bat cd's to its own dir (%~dp0), so
    // no working directory is needed.
    StringView args[] = {
        "cmd"sv,
        "/c"sv,
        bat,
    };
    ProcessResult result = RunProcess(arena, std::span<const StringView>(args));

    bool ok = result.Launched;
    ok &= (result.ExitCode == 0);
    return ok ? 0 : 1;
}

// Draws a tick (ok) or a cross (fail) into a small reserved box on the current line. The default
// ImGui font has no check/cross glyph, so they are stroked with the draw list.
void DrawStatusGlyph(bool ok) {
    float sz = ImGui::GetFontSize();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(sz, sz));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float pad = sz * 0.2f;
    float thickness = 2.0f;
    if (ok) {
        ImU32 col = IM_COL32(60, 200, 90, 255);
        ImVec2 low(p.x + pad, p.y + sz * 0.55f);
        ImVec2 mid(p.x + sz * 0.42f, p.y + sz - pad);
        ImVec2 high(p.x + sz - pad, p.y + pad);
        draw->AddLine(low, mid, col, thickness);
        draw->AddLine(mid, high, col, thickness);
    } else {
        ImU32 col = IM_COL32(220, 70, 70, 255);
        ImVec2 tl(p.x + pad, p.y + pad);
        ImVec2 br(p.x + sz - pad, p.y + sz - pad);
        draw->AddLine(tl, br, col, thickness);
        draw->AddLine(ImVec2(br.x, tl.y), ImVec2(tl.x, br.y), col, thickness);
    }
}

// Draws the top menu bar and lets the user switch app modes. Returns the bar's height so the views
// below can dock beneath it instead of under it.
float DrawMainMenuBar(GameState* gs) {
    float height = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        height = ImGui::GetWindowSize().y;
        for (EAppMode mode : kAppModes) {
            bool is_selected = gs->AppMode == mode;
            if (ImGui::MenuItem(ToString(mode).Str(), nullptr, is_selected)) {
                gs->AppMode = mode;
            }
        }

        // Right-aligned "Hot Reload" button + last-result glyph.
        ImGuiStyle& style = ImGui::GetStyle();
        const char* label = "Hot Reload";
        float glyph_w = 0.0f;
        if (gs->HotReloadResult != NONE) {
            glyph_w = ImGui::GetFontSize() + style.ItemSpacing.x;
        }
        float block_w = ImGui::CalcTextSize(label).x + style.ItemSpacing.x * 2.0f + glyph_w +
                        style.WindowPadding.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - block_w);
        if (ImGui::MenuItem(label)) {
            gs->HotReloadResult = RunHotReload();
        }
        if (gs->HotReloadResult != NONE) {
            ImGui::SameLine();
            DrawStatusGlyph(gs->HotReloadResult == 0);
        }

        ImGui::EndMainMenuBar();
    }
    return height;
}

// A secondary, full-width menu bar just below the main one, with one entry per asset type. Switches
// which asset type the editor is showing.
void DrawAssetTypeBar(GameState* gs, float menu_bar_height) {
    ImGuiIO& io = ImGui::GetIO();
    float bar_height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(0.0f, menu_bar_height));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, bar_height));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##AssetTypeBar", nullptr, flags);
    if (ImGui::BeginMenuBar()) {
        // The type-agnostic overview comes first and is the landing pane.
        if (ImGui::MenuItem("Database", nullptr, gs->AssetEditor.ShowDatabase)) {
            gs->AssetEditor.ShowDatabase = true;
        }
        for (i32 t = (i32)EAssetType::Texture; t < (i32)EAssetType::COUNT; ++t) {
            EAssetType type = (EAssetType)t;
            // Menu label = the type token with a capitalized first letter ("texture" -> "Texture").
            char label[64];
            snprintf(label, sizeof(label), "%s", ToString(type).Str());
            if (label[0] >= 'a' && label[0] <= 'z') {
                label[0] = (char)(label[0] - 'a' + 'A');
            }
            bool is_selected = !gs->AssetEditor.ShowDatabase;
            is_selected &= (gs->AssetEditor.CurrentType == type);
            if (ImGui::MenuItem(label, nullptr, is_selected)) {
                gs->AssetEditor.ShowDatabase = false;
                gs->AssetEditor.CurrentType = type;
            }
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

// The Assets view: a secondary type bar, then the type's create/list/inspect UI. The window shell
// lives here; the contents are the AssetEditor.
void DrawAssetsMode(GameState* gs, float menu_bar_height) {
    DrawAssetTypeBar(gs, menu_bar_height);

    ImGuiIO& io = ImGui::GetIO();
    float top = menu_bar_height + ImGui::GetFrameHeight();  // main menu bar + type bar
    ImGui::SetNextWindowPos(ImVec2(0.0f, top));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - top));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Assets", nullptr, flags);
    gs->AssetEditor.Draw(&gs->Registry);
    ImGui::End();
}

// Placeholder game UI: a centered row of squares along the bottom edge. Here to exercise the input
// tiering — hovering one suppresses the grid highlight underneath and swallows the click — before
// the real tower palette (icons, cost, selection) lands on top of it.
void DrawGameUI(DrawContext* dc) {
    constexpr i32 kButtonCount = 4;
    constexpr float kButtonSize = 64.0f;
    constexpr float kSpacing = 12.0f;
    constexpr float kBottomMargin = 24.0f;

    ImGuiIO& io = ImGui::GetIO();
    float row_width = kButtonCount * kButtonSize + (kButtonCount - 1) * kSpacing;
    float x = (io.DisplaySize.x - row_width) * 0.5f;
    float y = io.DisplaySize.y - kBottomMargin - kButtonSize;

    for (i32 i = 0; i < kButtonCount; ++i) {
        if (UIButton(dc, Vec2(x, y), Vec2(kButtonSize, kButtonSize))) {
            printf("[game] UI button %d clicked\n", i);
        }
        x += kButtonSize + kSpacing;
    }
}

// The debug panel: dev-facing tooling (phase controls, scene IO, counters) docked to the left
// edge. Toggled with "/". It floats over the world rather than reserving space from it, so the
// view never shifts when it comes and goes.
void DrawDebugPanel(PlatformState* ps, GameState* gs, float menu_bar_height) {
    World& world = gs->World;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, menu_bar_height));
    ImGui::SetNextWindowSize(ImVec2(kSidePanelWidth, io.DisplaySize.y - menu_bar_height));
    ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Debug", nullptr, panel_flags);
    ImGui::Text("%.1f FPS", io.Framerate);
    ImGui::Text("Phase: %s", ToString(gs->Phase).Str());
    ImGui::Text("WASD pan / wheel zoom (%.2fx)", gs->Zoom);
    ImGui::Text("Base Health: %.0f / %.0f", world.BaseHealth, World::kMaxBaseHealth);
    ImGui::Text("Gold: %d", world.Gold);
    ImGui::Separator();

    switch (gs->Phase) {
        case EGamePhase::PreGame: {
            if (ImGui::BeginCombo("Operation", ToString(gs->CurrentOperation).Str())) {
                for (EOperationMode mode : kOperationModes) {
                    bool is_selected = gs->CurrentOperation == mode;
                    if (ImGui::Selectable(ToString(mode).Str(), is_selected)) {
                        gs->CurrentOperation = mode;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // Placing towers in the editor uses the same build-menu selection as the Build phase
            // (free here — PreGame lays out the map, it doesn't charge gold).
            if (gs->CurrentOperation == EOperationMode::ToggleTower) {
                DrawTowerBuildMenu(gs, ps->TimeTracking.TotalSeconds);
                ImGui::Separator();
            }

            if (ImGui::Button("Save Scene")) {
                SaveScene(world, kScenePath);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Scene")) {
                LoadScene(&world, &gs->Registry, kScenePath);
            }

            // Lock in the terrain and start the run, but only if there's a goal and at least one
            // spawn source for waves to come from.
            if (ImGui::Button("Start Game")) {
                world.CalculatePath();
                world.CollectSpawnSources();
                bool can_start = world.Goal.has_value();
                can_start &= !world.SpawnSources.IsEmpty();
                if (can_start) {
                    world.BeginRun();
                    gs->Phase = EGamePhase::Prepare;
                }
            }
            break;
        }
        case EGamePhase::Prepare: {
            if (ImGui::Button("Start Wave")) {
                world.ArmNextWave();
                gs->Phase = EGamePhase::Wave;
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart Game")) {
                gs->Phase = EGamePhase::PreGame;
            }

            DrawTowerBuildMenu(gs, ps->TimeTracking.TotalSeconds);
            ImGui::TextWrapped("Click an empty tile to build the selected tower.");
            break;
        }
        case EGamePhase::Wave: {
            ImGui::Text("Wave %d", world.Wave.Number);
            ImGui::Text("To spawn: %d   Alive: %d", world.Wave.ToSpawn, world.Enemies.Size);
            break;
        }
        case EGamePhase::GameOver: {
            ImGui::Text("Game Over - survived %d waves.", world.Wave.Number);
            if (ImGui::Button("Restart")) {
                gs->Phase = EGamePhase::PreGame;
            }
            break;
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Enemies:     %d/%d", world.Enemies.Size, world.Enemies.kMaxSize);
        ImGui::Text("Projectiles: %d/%d", world.Projectiles.Size, world.Projectiles.kMaxSize);
        ImGui::Text("DLL Reload Count: %d", gs->InternalDetectedReload);

        ImGui::Checkbox("Inverse Camera Movement", &gs->InverseCameraMovement);

        // Drop one enemy on the first spawn source, to test firing without waiting on a wave.
        if (ImGui::Button("Spawn Enemy")) {
            if (!world.SpawnSources.IsEmpty()) {
                const EnemyAsset* blueprint = gs->Registry.FindEnemyAsset(world.DefaultEnemy);
                EnemyAsset fallback = {};
                world.SpawnEnemy(blueprint ? *blueprint : fallback, world.SpawnSources[0]);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+100 Gold")) {
            world.Gold += 100;
        }
    }

    ImGui::End();
}

}  // namespace game_private

void GameRender(PlatformState* ps, GameState* gs) {
    using namespace game_private;

    World& world = gs->World;

    float menu_bar_height = DrawMainMenuBar(gs);
    if (gs->AppMode == EAppMode::Assets) {
        DrawAssetsMode(gs, menu_bar_height);
        return;
    }

    // The frame's shared draw state, built once here and passed down: everything that paints the
    // world agrees on one draw list, origin and zoom, and picking inverts the same transform.
    //
    // Center the world in the window, then apply the camera pan. The debug panel is an overlay and
    // deliberately does NOT bias this: it can be toggled away ("/"), and biasing would make the
    // world lurch sideways each time. Pan if it covers something.

    DrawContext dc = NewDrawContext(ps, gs);
    dc.BeginFrame();

    // Input priority is ImGui -> UI -> game, arbitrated purely by emission order: the UI goes first
    // and may claim the mouse, the world goes after and sees the verdict. That is the reverse of
    // the z-order we want, so the two paint into separate channels of one draw list — channel 0
    // (world) merges under channel 1 (UI), and both stay under ImGui's windows, which is where the
    // debug panel belongs.

    dc.EnableUIChannel();
    DrawGameUI(&dc);

    dc.EnableGameplayChannel();
    std::optional<Hex> hovered = DrawHexGrid(&dc, &world);

    dc.EndFrame();

    // The game is the lowest tier, so it decides last: |hovered| is already empty if ImGui or the
    // UI claimed the mouse this frame.
    //
    // TODO(arch): this is gameplay running inside the render pass — ApplyEditorOp and TryBuyTower
    // mutate the world from GameRender, so a click lands at draw time instead of in GameUpdate, and
    // rendering is no longer a pure read of the world. The intended shape is for input to queue a
    // command that GameUpdate drains next tick. Deferred: the command shape wants designing on its
    // own, and it is orthogonal to the input tiering above.
    bool clicked = hovered.has_value();
    clicked &= dc.UI.MousePressed;
    if (clicked) {
        switch (gs->Phase) {
            case EGamePhase::PreGame: {
                ApplyEditorOp(gs, *hovered);
                break;
            }
            case EGamePhase::Prepare: {
                TryBuyTower(&world, &gs->Registry, *hovered);
                break;
            }
            case EGamePhase::Wave:
            case EGamePhase::GameOver: {
                break;  // no placement while a wave runs or after the run ends
            }
        }
    }

    if (gs->ShowDebugPanel) {
        DrawDebugPanel(ps, gs, menu_bar_height);
    }
}

}  // namespace kdk
