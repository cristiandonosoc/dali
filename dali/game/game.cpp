#include <dali/game/game.h>

#include <dali/core/api.h>
#include <dali/core/color.h>
#include <dali/game/hex.h>
#include <dali/game/log.h>
#include <dali/game/scene.h>

#include <imgui.h>

#include <cstdio>
#include <optional>

namespace kdk {

// Hex center-to-corner distance in WORLD units. The sim runs entirely in world units (enemy
// movement, ranges, spawn placement); the renderer converts to pixels as world * zoom. At zoom 1 a
// world unit is one pixel.
constexpr float kHexSize = 60.0f;

// Tower tuning. Range is in world units (pixels); at kHexSize=60 a range of 180 reaches ~3 tiles.
constexpr float kTowerRange = 180.0f;
constexpr float kTowerFireInterval = 0.6f;    // Seconds between shots.
constexpr float kTowerDamage = 5.0f;          // Per projectile; enemies start at 10 HP.
constexpr float kProjectileHitRadius = 8.0f;  // Distance at which a projectile connects.

// Economy + wave tuning.
constexpr int kStartingGold = 100;                // Gold the player begins a run with.
constexpr int kTowerCost = 40;                    // Gold to place one tower.
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

// Draws a short arrow from |from_center| toward |toward_center| (a neighbouring tile center),
// scaled to |size|. Used to visualize each path tile's PathDirection.
void DrawArrow(ImDrawList* draw_list,
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
    draw_list->AddLine(tail, tip, color.Bits, 3.0f);

    // Two barbs: |dir| reversed, then rotated +/- 28 degrees and scaled down.
    constexpr float kHead = 10.0f;
    float a = ToRadians(28.0f);
    Vec2 back(-dir.x, -dir.y);
    Vec2 left(back.x * Cos(a) - back.y * Sin(a), back.x * Sin(a) + back.y * Cos(a));
    Vec2 right(back.x * Cos(-a) - back.y * Sin(-a), back.x * Sin(-a) + back.y * Cos(-a));
    draw_list->AddLine(tip,
                       ImVec2(tip.x + left.x * kHead, tip.y + left.y * kHead),
                       color.Bits,
                       3.0f);
    draw_list->AddLine(tip,
                       ImVec2(tip.x + right.x * kHead, tip.y + right.y * kHead),
                       color.Bits,
                       3.0f);
}

// Draws a horizontal health bar (black backdrop + colored fill) centered at |center_x| with its top
// at |top_y|. |fraction| is clamped to [0,1]; an empty bar still shows the backdrop.
void DrawHealthBar(ImDrawList* draw_list,
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
    draw_list->AddRectFilled(bar_min, bar_max, Color32::Black.Bits);
    draw_list->AddRectFilled(bar_min, fill_max, fill.Bits);
    draw_list->AddRect(bar_min, bar_max, Color32::Black.Bits);
}

// Debug-draws the grid straight onto ImGui's background draw list. Per milestone-01 step 3 there is
// no engine render layer yet — this is the temporary draw path that lets the grid/hover/path steps
// proceed before we know which primitives the real renderer needs. The hex under the mouse is
// highlighted, which also proves the world<->hex transform round-trips.
//
// Returns clicked hex.
std::optional<Hex> DrawHexGrid(PlatformState* ps, World* world, Vec2 camera, float zoom) {
    std::optional<Hex> result = {};

    ImGuiIO& io = ImGui::GetIO();
    // Center the world in the area right of the docked panel, then apply the camera pan. Clicks use
    // the same origin + zoom below, so rendering and picking stay in lockstep.
    Vec2 origin(kSidePanelWidth + (io.DisplaySize.x - kSidePanelWidth) * 0.5f + camera.x,
                io.DisplaySize.y * 0.5f + camera.y);

    // World->screen: the sim is in zoom-independent world units; pixels are origin + world * zoom.
    auto world_to_screen = [&](Vec2 w) -> ImVec2 {
        return ImVec2(origin.x + w.x * zoom, origin.y + w.y * zoom);
    };

    // Which hex is under the mouse? Invert the exact transform (undo zoom, then origin) so the
    // highlight lines up regardless of the screen's Y-down convention.
    Vec2 mouse_world((io.MousePos.x - origin.x) / zoom, (io.MousePos.y - origin.y) / zoom);
    Hex hovered = Hex::WorldToHex(kHexSize, mouse_world);

    // A left click selects the hovered hex — even empty space with no tile, so ops like AddChunk
    // can target a void. Clicks ImGui is consuming (over the docked panel) are ignored.
    if (!io.WantCaptureMouse) {
        if (ps->Input.IsMousePressed(EMouseButton::Left)) {
            result = hovered;
        }
    }

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // A tile-center in screen space. Shared by the grid pass and the path spine so they line up.
    auto tile_center = [&](Hex hex) -> ImVec2 {
        return world_to_screen(Hex::HexToWorld(kHexSize, hex));
    };

    for (i32 chunk_index = 0; chunk_index < world->Grid.Chunks.Size; ++chunk_index) {
        const TileChunk& chunk = world->Grid.Chunks[chunk_index];
        for (i32 slot = 0; slot < chunk.Tiles.Size; ++slot) {
            const Tile& tile = chunk.Tiles[slot];
            ImVec2 center = tile_center(tile.Hex);

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

            draw_list->AddConvexPolyFilled(corners, 6, fill.Bits);
            draw_list->AddPolyline(corners, 6, Color32::White.Bits, ImDrawFlags_Closed, 2.0f);

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
        ImVec2 center = tile_center(tile->Hex);
        ImVec2 toward = tile_center(tile->Hex.Neighbour(tile->PathDirection));
        DrawArrow(draw_list, center, toward, kHexSize * zoom, Color32::BrightGold);
    });

    // Buildings: a tile holds at most one. Spawners (enemy origins) are green discs; towers are
    // blue discs ringed by a faint circle showing their firing range.
    world->Grid.ForEachTile([&](Tile* tile) {
        ImVec2 center = tile_center(tile->Hex);
        if (tile->Content == ETileContent::Spawner) {
            draw_list->AddCircleFilled(center, 10.0f * zoom, Color32::Green.Bits);
            draw_list->AddCircle(center, 10.0f * zoom, Color32::White.Bits, 0, 2.0f);
        } else if (tile->Content == ETileContent::Tower) {
            // The range ring only shows while hovering this tower, so a dense field stays readable.
            if (tile->Hex == hovered) {
                draw_list->AddCircle(center, kTowerRange * zoom, Color32::Cyan.Bits, 0, 2.0f);
            }
            draw_list->AddCircleFilled(center, 10.0f * zoom, Color32::SteelBlue.Bits);
            draw_list->AddCircle(center, 10.0f * zoom, Color32::White.Bits, 0, 2.0f);

            // Seconds until this tower can fire again (0.00 = ready), so cooldown behavior is
            // visible at a glance.
            char cooldown_label[16];
            snprintf(cooldown_label, sizeof(cooldown_label), "%.2f", tile->FireCooldown);
            draw_list->AddText(ImVec2(center.x + 12.0f * zoom, center.y - 8.0f * zoom),
                               Color32::White.Bits,
                               cooldown_label);
        }
    });

    // Spawn sources: the outskirt tiles waves emit from (derived path leaves). A green ring marks
    // each so you can see where enemies will enter.
    for (const Hex& source : world->SpawnSources) {
        ImVec2 center = tile_center(source);
        draw_list->AddCircle(center, kHexSize * 0.55f * zoom, Color32::Green.Bits, 6, 3.0f);
    }

    // The goal: the tile every path drains into. Its base-health bar is always shown so you can
    // watch it drop as enemies break through.
    if (world->Goal.has_value()) {
        ImVec2 center = tile_center(*world->Goal);
        draw_list->AddCircleFilled(center, 9.0f * zoom, Color32::Red.Bits);
        DrawHealthBar(draw_list,
                      center.x,
                      center.y - 18.0f * zoom,
                      28.0f * zoom,
                      world->BaseHealth / World::kMaxBaseHealth,
                      Color32::Green);
    }

    // Enemies walking the flow field from spawner toward the goal.
    for (const Enemy& enemy : world->Enemies) {
        ImVec2 p = world_to_screen(enemy.Position);
        draw_list->AddCircleFilled(p, 6.0f * zoom, Color32::OrangeRed.Bits);
        draw_list->AddCircle(p, 6.0f * zoom, Color32::Black.Bits, 0, 1.5f);

        // Health bar, only once the enemy has taken damage (a full bar would just be clutter).
        if (enemy.MaxHealth > 0.0f && enemy.Health < enemy.MaxHealth) {
            DrawHealthBar(draw_list,
                          p.x,
                          p.y - 12.0f * zoom,
                          18.0f * zoom,
                          enemy.Health / enemy.MaxHealth,
                          Color32::Green);
        }
    }

    // Tower projectiles in flight toward their targets.
    for (const Projectile& projectile : world->Projectiles) {
        ImVec2 p = world_to_screen(projectile.Position);
        draw_list->AddCircleFilled(p, 3.0f * zoom, Color32::Yellow.Bits);
    }

    return result;
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

void World::SpawnEnemy(Hex at, float health_scale) {
    if (Enemies.IsFull()) {
        return;
    }
    Enemy enemy = {};
    enemy.Id = NextEnemyId;
    NextEnemyId++;
    enemy.Position = Hex::HexToWorld(kHexSize, at);
    enemy.Target = at;  // retargets to the next flow-field tile on the first update
    enemy.Health = enemy.Health * health_scale;
    enemy.MaxHealth = enemy.Health;
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

void MakeTower(Tile* tile) {
    tile->Content = ETileContent::Tower;
    tile->FireCooldown = 0.0f;  // ready to fire the moment an enemy walks into range
}

void World::BeginRun() {
    Gold = kStartingGold;
    BaseHealth = kMaxBaseHealth;
    Wave = {};
    Enemies.Clear();
    Projectiles.Clear();

    // Start with no towers; PreGame only lays out terrain (path + goal).
    Grid.ForEachTile([](Tile* tile) { tile->Content = ETileContent::None; });

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
        if (tile->Content != ETileContent::Tower) {
            return;
        }
        tile->FireCooldown = 0.0f;
    });
}

void World::UpdateWave(float dt) {
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
    SpawnEnemy(source, health_scale);
    Wave.ToSpawn--;
}

void World::UpdateEnemies(float dt) {
    constexpr float kArriveThreshold = 2.0f;  // Pixels; within this we hop to the next tile.

    for (i32 i = 0; i < Enemies.Size;) {
        Enemy& enemy = Enemies[i];

        // Spend this frame's travel budget, walking through as many tiles as it reaches.
        float remaining = enemy.Speed * dt;
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
            float move = Min(remaining, dist);
            enemy.Position.x += dir.x * move;
            enemy.Position.y += dir.y * move;
            remaining -= move;
        }

        if (arrived) {
            BaseHealth = Max(0.0f, BaseHealth - enemy.Damage);  // enemy breached the goal
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
        if (tile->Content != ETileContent::Tower) {
            return;
        }

        tile->FireCooldown -= dt;
        if (tile->FireCooldown > 0.0f) {
            return;  // still cooling down
        }
        // Ready to fire. Clamp to 0 so a tower with no target in range holds at "ready" instead of
        // drifting ever more negative while it waits.
        tile->FireCooldown = 0.0f;

        // Among enemies in range, pick the one closest to the base. Compare squared distances to
        // skip the sqrt.
        Vec2 tower_pos = Hex::HexToWorld(kHexSize, tile->Hex);
        constexpr float kRangeSq = kTowerRange * kTowerRange;
        Enemy* target = nullptr;
        float best_priority = 0.0f;  // lower wins; meaning depends on goal_pos
        for (Enemy& enemy : Enemies) {
            float d_sq = LengthSq(enemy.Position - tower_pos);
            if (d_sq > kRangeSq) {
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
        projectile.Damage = kTowerDamage;
        Projectiles.Push(projectile);
        tile->FireCooldown = kTowerFireInterval;
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
        if (LengthSq(delta) <= kProjectileHitRadius * kProjectileHitRadius) {
            // Reached the aim point. Only deal damage if the target is still there to take it.
            if (target) {
                target->Health -= projectile.Damage;
                if (target->Health <= 0.0f) {
                    Gold += target->Reward;  // bounty for the kill
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

    // Reload the last saved scene; fall back to an empty grid the first time (no file yet). The
    // player lays out the path/goal in the PreGame editor.
    if (!LoadScene(&gs->World, kScenePath)) {
        gs->World.Grid.Init();
    }
    gs->World.CollectSpawnSources();  // so the outskirts are visible before the first path edit

    // Load all baked assets once. GL is already live (OnSOLoaded ran first), and the registry lives
    // in the PermanentArena, so this does not repeat on reload.
    gs->Registry.CrawlAndLoad();

    printf("[game] GameInit\n");
}

void GameUpdate(PlatformState* ps, GameState* gs) {
    float dt = (float)ps->TimeTracking.DeltaSeconds;

    World& world = gs->World;

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

    world.UpdateWave(dt);
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
        gs->Phase = EGamePhase::Build;  // wave cleared; back to building
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
        case EGamePhase::Build:    return "Build"sv;
        case EGamePhase::Wave:     return "Wave"sv;
        case EGamePhase::GameOver: return "GameOver"sv;
    }
    // clang-format on
    ASSERT(false);
    return "<unknown>"sv;
}

namespace game_private {

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
                tile->Content = ETileContent::None;
            } else if (tile->IsPath) {
                // Only path tiles can host a spawner. Placing it replaces any other content.
                MakeSpawner(tile);
            }
            break;
        }
        case EOperationMode::ToggleTower: {
            if (tile->Content == ETileContent::Tower) {
                tile->Content = ETileContent::None;
            } else if (!tile->IsPath) {
                // Towers guard the path from beside it, so they only go on non-path tiles.
                MakeTower(tile);
            }
            break;
        }
        case EOperationMode::AddChunk: {
            break;  // handled before the tile lookup above
        }
    }
}

// Build-phase interaction: buy a tower on an empty, non-path tile if the player can afford it.
void TryBuyTower(World* world, Hex hex) {
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
    if (world->Gold < kTowerCost) {
        return;
    }
    world->Gold -= kTowerCost;
    MakeTower(tile);
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
        for (i32 t = (i32)EAssetType::Texture; t < (i32)EAssetType::COUNT; ++t) {
            EAssetType type = (EAssetType)t;
            // Menu label = the type token with a capitalized first letter ("texture" -> "Texture").
            char label[64];
            snprintf(label, sizeof(label), "%s", ToString(type).Str());
            if (label[0] >= 'a' && label[0] <= 'z') {
                label[0] = (char)(label[0] - 'a' + 'A');
            }
            bool is_selected = gs->AssetEditor.CurrentType == type;
            if (ImGui::MenuItem(label, nullptr, is_selected)) {
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

}  // namespace game_private

void GameRender(PlatformState* ps, GameState* gs) {
    using namespace game_private;
    (void)ps;

    World& world = gs->World;

    float menu_bar_height = DrawMainMenuBar(gs);
    if (gs->AppMode == EAppMode::Assets) {
        DrawAssetsMode(gs, menu_bar_height);
        return;
    }

    std::optional<Hex> clicked_hex = DrawHexGrid(ps, &world, gs->Camera, gs->Zoom);
    if (clicked_hex.has_value()) {
        switch (gs->Phase) {
            case EGamePhase::PreGame: {
                ApplyEditorOp(gs, *clicked_hex);
                break;
            }
            case EGamePhase::Build: {
                TryBuyTower(&world, *clicked_hex);
                break;
            }
            case EGamePhase::Wave:
            case EGamePhase::GameOver: {
                break;  // no placement while a wave runs or after the run ends
            }
        }
    }

    // Dock the control panel to the full-height left edge as a fixed side window.
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, menu_bar_height));
    ImGui::SetNextWindowSize(ImVec2(kSidePanelWidth, io.DisplaySize.y - menu_bar_height));
    ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Dali", nullptr, panel_flags);
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

            if (ImGui::Button("Save Scene")) {
                SaveScene(world, kScenePath);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Scene")) {
                LoadScene(&world, kScenePath);
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
                    gs->Phase = EGamePhase::Build;
                }
            }
            break;
        }
        case EGamePhase::Build: {
            ImGui::Text("Click an empty tile to build a tower (%d gold).", kTowerCost);
            if (ImGui::Button("Start Wave")) {
                world.ArmNextWave();
                gs->Phase = EGamePhase::Wave;
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart Game")) {
                gs->Phase = EGamePhase::PreGame;
            }
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
                world.SpawnEnemy(world.SpawnSources[0]);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+100 Gold")) {
            world.Gold += 100;
        }
    }

    ImGui::End();
}

}  // namespace kdk
