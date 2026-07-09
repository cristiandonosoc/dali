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

// World unit == pixel: a hex center-to-corner spans this many pixels. Shared by the sim (enemy
// movement, spawn placement) and the debug renderer so both agree on where tiles sit.
constexpr float kHexSize = 60.0f;
constexpr float kSpawnInterval = 1.0f;  // Seconds between spawns, per spawner.

// Tower tuning. Range is in world units (pixels); at kHexSize=60 a range of 180 reaches ~3 tiles.
constexpr float kTowerRange = 180.0f;
constexpr float kTowerFireInterval = 0.6f;    // Seconds between shots.
constexpr float kTowerDamage = 5.0f;          // Per projectile; enemies start at 10 HP.
constexpr float kProjectileHitRadius = 8.0f;  // Distance at which a projectile connects.

namespace game_private {

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
std::optional<Hex> DrawHexGrid(PlatformState* ps, World* world) {
    std::optional<Hex> result = {};

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
            if (ps->Input.IsMousePressed(EMouseButton::Left)) {
                result = tile.Hex;
            }

            fill = Color32::Gold;
        }

        draw_list->AddConvexPolyFilled(corners, 6, fill.Bits);
        draw_list->AddPolyline(corners, 6, Color32::White.Bits, ImDrawFlags_Closed, 2.0f);
    }

    // Flow-field arrows: each path tile points toward the neighbour that steps one hex closer to
    // the goal (its PathDirection). Tiles with no route to the goal keep NONE and draw nothing.
    for (const Tile& tile : world->Grid.Tiles) {
        if (!tile.IsPath || tile.PathDirection == NONE) {
            continue;
        }
        ImVec2 center = tile_center(tile.Hex);
        ImVec2 toward = tile_center(tile.Hex.Neighbour(tile.PathDirection));
        DrawArrow(draw_list, center, toward, kHexSize, Color32::BrightGold);
    }

    // Buildings: a tile holds at most one. Spawners (enemy origins) are green discs; towers are
    // blue discs ringed by a faint circle showing their firing range.
    for (const Tile& tile : world->Grid.Tiles) {
        ImVec2 center = tile_center(tile.Hex);
        if (tile.Content == ETileContent::Spawner) {
            draw_list->AddCircleFilled(center, 10.0f, Color32::Green.Bits);
            draw_list->AddCircle(center, 10.0f, Color32::White.Bits, 0, 2.0f);
        } else if (tile.Content == ETileContent::Tower) {
            // The range ring only shows while hovering this tower, so a dense field stays readable.
            if (tile.Hex == hovered) {
                draw_list->AddCircle(center, kTowerRange, Color32::Cyan.Bits, 0, 2.0f);
            }
            draw_list->AddCircleFilled(center, 10.0f, Color32::SteelBlue.Bits);
            draw_list->AddCircle(center, 10.0f, Color32::White.Bits, 0, 2.0f);
        }
    }

    // The goal: the tile every path drains into. Its base-health bar is always shown so you can
    // watch it drop as enemies break through.
    if (world->Goal.has_value()) {
        ImVec2 center = tile_center(*world->Goal);
        draw_list->AddCircleFilled(center, 9.0f, Color32::Red.Bits);
        DrawHealthBar(draw_list,
                      center.x,
                      center.y - 18.0f,
                      28.0f,
                      world->BaseHealth / World::kMaxBaseHealth,
                      Color32::Green);
    }

    // Enemies walking the flow field from spawner toward the goal.
    for (const Enemy& enemy : world->Enemies) {
        ImVec2 p(origin.x + enemy.Position.x, origin.y + enemy.Position.y);
        draw_list->AddCircleFilled(p, 6.0f, Color32::OrangeRed.Bits);
        draw_list->AddCircle(p, 6.0f, Color32::Black.Bits, 0, 1.5f);

        // Health bar, only once the enemy has taken damage (a full bar would just be clutter).
        if (enemy.MaxHealth > 0.0f && enemy.Health < enemy.MaxHealth) {
            DrawHealthBar(draw_list,
                          p.x,
                          p.y - 12.0f,
                          18.0f,
                          enemy.Health / enemy.MaxHealth,
                          Color32::Green);
        }
    }

    // Tower projectiles in flight toward their targets.
    for (const Projectile& projectile : world->Projectiles) {
        ImVec2 p(origin.x + projectile.Position.x, origin.y + projectile.Position.y);
        draw_list->AddCircleFilled(p, 3.0f, Color32::Yellow.Bits);
    }

    return result;
}

}  // namespace game_private

void Grid::InitRing(int radius) {
    Tiles.Size = 0;
    for (int q = -radius; q <= radius; ++q) {
        int r_lo = Max(-radius, -q - radius);
        int r_hi = Min(radius, -q + radius);
        for (int r = r_lo; r <= r_hi; ++r) {
            Tiles.Push(Tile{
                .Hex = Hex{q, r}
            });
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

void World::InitLevel() {
    Grid.InitRing(3);
    BuildStraightPath(Hex{-3, 0}, 0, 6);
    for (const Hex& hex : Path) {
        if (Tile* tile = Grid.FindTile(hex)) {
            tile->IsPath = true;
        }
    }
    Goal = Path.Last();
    CalculatePath();
}

void World::BuildStraightPath(Hex start, int dir, int steps) {
    Path.Clear();
    Hex hex = start;
    Path.Push(hex);
    for (int i = 0; i < steps; ++i) {
        hex = hex.Neighbour(dir);
        Path.Push(hex);
    }
}

void World::CalculatePath() {
    for (Tile& tile : Grid.Tiles) {
        tile.PathDirection = NONE;
    }

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
    FixedVector<Hex, decltype(Grid.Tiles)::kMaxSize> frontier;
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

void World::SpawnEnemy(Hex at) {
    if (Enemies.IsFull()) {
        return;
    }
    Enemy enemy = {};
    enemy.Id = NextEnemyId++;
    enemy.Position = Hex::HexToWorld(kHexSize, at);
    enemy.Target = at;  // retargets to the next flow-field tile on the first update
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

void World::UpdateSpawners(float dt) {
    for (Tile& tile : Grid.Tiles) {
        if (tile.Content != ETileContent::Spawner) {
            continue;
        }
        tile.SpawnTimer += dt;
        if (tile.SpawnTimer >= kSpawnInterval) {
            tile.SpawnTimer -= kSpawnInterval;
            SpawnEnemy(tile.Hex);
        }
    }
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

    for (Tile& tile : Grid.Tiles) {
        if (tile.Content != ETileContent::Tower) {
            continue;
        }

        tile.FireCooldown -= dt;
        if (tile.FireCooldown > 0.0f) {
            continue;
        }

        // Among enemies in range, pick the one closest to the base. Compare squared distances to
        // skip the sqrt.
        Vec2 tower_pos = Hex::HexToWorld(kHexSize, tile.Hex);
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
            continue;  // nothing in range (or no room) — hold fire, retry next frame
        }

        Projectile projectile = {};
        projectile.Position = tower_pos;
        projectile.TargetId = target->Id;
        projectile.Damage = kTowerDamage;
        Projectiles.Push(projectile);
        tile.FireCooldown = kTowerFireInterval;
    }
}

void World::UpdateProjectiles(float dt) {
    for (i32 i = 0; i < Projectiles.Size;) {
        Projectile& projectile = Projectiles[i];

        Enemy* target = FindEnemy(projectile.TargetId);
        if (!target) {
            Projectiles.RemoveUnorderedAt(i);  // target died/escaped before we reached it
            continue;
        }

        Vec2 delta = target->Position - projectile.Position;
        if (LengthSq(delta) <= kProjectileHitRadius * kProjectileHitRadius) {
            target->Health -= projectile.Damage;
            if (target->Health <= 0.0f) {
                Gold += target->Reward;  // bounty for the kill
                Enemies.RemoveUnorderedAt((i32)(target - Enemies.begin()));
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

    // Reload the last saved scene; fall back to the hardcoded default the first time (no file yet).
    if (!LoadScene(&gs->World, kScenePath)) {
        gs->World.InitLevel();
    }

    printf("[game] GameInit\n");
}

void GameUpdate(PlatformState* ps, GameState* gs) {
    float dt = (float)ps->TimeTracking.DeltaSeconds;

    World& world = gs->World;
    world.Count++;
    world.UpdateSpawners(dt);
    world.UpdateEnemies(dt);
    world.UpdateTowers(dt);
    world.UpdateProjectiles(dt);
}

StringView ToString(EOperationMode mode) {
    // clang-format off
    switch (mode) {
        case EOperationMode::TogglePath: return "Toggle Path"sv;
        case EOperationMode::SetPathGoal: return "Set Path Goal"sv;
        case EOperationMode::ToggleSpawner: return "Toggle Spawner"sv;
        case EOperationMode::ToggleTower: return "Toggle Tower"sv;
    }
    // clang-format on
    ASSERT(false);
    return "<unknown>"sv;
}

void GameRender(PlatformState* ps, GameState* gs) {
    using namespace game_private;
    (void)ps;

    auto clicked_hex = DrawHexGrid(ps, &gs->World);
    if (clicked_hex.has_value()) {
        if (Tile* tile = gs->World.Grid.FindTile(*clicked_hex)) {
            switch (gs->CurrentOperation) {
                case EOperationMode::TogglePath: {
                    FLIP_BOOL(tile->IsPath);
                    // A spawner can only sit on a path tile, so it goes when the path does.
                    if (!tile->IsPath && tile->Content == ETileContent::Spawner) {
                        tile->Content = ETileContent::None;
                    }
                    gs->World.CalculatePath();
                    break;
                }
                case EOperationMode::SetPathGoal: {
                    // The goal is a path tile every route drains into; make it path and re-flood.
                    tile->IsPath = true;
                    gs->World.Goal = *clicked_hex;
                    gs->World.CalculatePath();
                    break;
                }
                case EOperationMode::ToggleSpawner: {
                    if (tile->Content == ETileContent::Spawner) {
                        tile->Content = ETileContent::None;
                    } else if (tile->IsPath) {
                        // Only path tiles can host a spawner. Placing it replaces any other
                        // content.
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
            }
        }
    }

    ImGui::Begin("Dali");
    ImGui::Text("Hello from the game DLL");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

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
        SaveScene(gs->World, kScenePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) {
        LoadScene(&gs->World, kScenePath);
    }

    // Debug spawn: drop one crawler onto a spawner (or, failing that, any path tile that still has a
    // route to the goal) so you can farm towers without waiting on the spawn timers.
    if (ImGui::Button("Spawn Enemy")) {
        Tile* origin = nullptr;
        for (Tile& tile : gs->World.Grid.Tiles) {
            if (tile.Content == ETileContent::Spawner) {
                origin = &tile;
                break;
            }
            if (!origin && tile.IsPath && tile.PathDirection != NONE) {
                origin = &tile;  // fallback; keep scanning in case a real spawner shows up
            }
        }
        if (origin) {
            gs->World.SpawnEnemy(origin->Hex);
        }
    }

    ImGui::Text("Base Health: %.0f / %.0f", gs->World.BaseHealth, World::kMaxBaseHealth);
    ImGui::Text("Gold: %d", gs->World.Gold);
    ImGui::Text("Enemies: %d", gs->World.Enemies.Size);
    ImGui::Text("World Count: %d", gs->World.Count);
    ImGui::Text("DLL Reload Count: %d", gs->InternalDetectedReload);
    ImGui::End();
}

}  // namespace kdk
