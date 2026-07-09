#pragma once

#include <dali/core/container.h>
#include <dali/core/string.h>
#include <dali/game/hex.h>

#include <optional>

namespace kdk {

struct PlatformState;

// What a tile holds. Mutually exclusive — a tile has at most one building.
enum class ETileContent : u8 {
    None,
    Spawner,
    Tower,
};

struct Tile {
    Hex Hex = {};
    bool IsPath = false;

    int PathDirection = NONE;
    ETileContent Content = ETileContent::None;
    // Seconds accumulated toward this spawner's next spawn. Seeded with a random phase offset (see
    // MakeSpawner) so spawners fire out of sync. Only meaningful when Content == Spawner.
    float SpawnTimer = 0.0f;
    // Seconds until this tower can fire again. Counts down; a shot resets it to the fire interval.
    // Only meaningful when Content == Tower.
    float FireCooldown = 0.0f;
};

struct Grid {
    // Cap holds a radius-3 neighbourhood (3*r*(r+1)+1 = 37 tiles) with headroom.
    FixedVector<Tile, 64> Tiles;

    // Fills the grid with every hex within |radius| of the origin. radius 0 = 1 tile, radius 1 = a
    // center ringed by 6 (7 total), etc.
    void InitRing(int radius);
    Tile* FindTile(Hex hex);
};

struct Enemy {
    u32 Id = 0;          // Stable across the enemy's life; projectiles home on this, not an index.
    Vec2 Position = {};  // World space (pre-view-origin). World unit == pixel.
    Hex Target = {};     // The tile it is walking toward; the flow field picks the next one.
    float Speed = 100.0f;     // Pixels per second.
    float Health = 10.0f;     // Single HP pool; a tower projectile subtracts its damage on impact.
    float MaxHealth = 10.0f;  // Health at spawn; the health bar draws Health/MaxHealth.
    float Damage = 5.0f;      // HP drained from the base when this enemy reaches the goal.
    int Reward = 5;           // Gold granted to the player when a tower kills this enemy.
};

// A tower shot in flight. Homes on the enemy with Id == TargetId every frame; on a distance check
// against that enemy it applies Damage and despawns. If the target dies first (no such Id), the
// projectile despawns without effect.
struct Projectile {
    Vec2 Position = {};    // World space, same frame as Enemy::Position.
    u32 TargetId = 0;      // The enemy this shot is chasing.
    float Speed = 320.0f;  // Pixels per second.
    float Damage = 5.0f;
};

struct World {
    static constexpr i32 kMaxPathTiles = 16;
    static constexpr i32 kMaxEnemies = 128;
    static constexpr i32 kMaxProjectiles = 256;
    static constexpr float kMaxBaseHealth = 100.0f;

    int Count = 0;
    // The base's HP. Each enemy that reaches the goal subtracts its damage; it never goes below 0.
    float BaseHealth = kMaxBaseHealth;
    // Player currency. Grows by an enemy's Reward each time a tower kills one.
    int Gold = 0;
    Grid Grid = {};
    FixedVector<Hex, kMaxPathTiles> Path = {};
    // The tile every path drains into. All PathDirections point one hex closer to it.
    std::optional<Hex> Goal = {};

    u32 NextEnemyId = 1;  // 0 is reserved as "no target"; ids are handed out monotonically.
    FixedVector<Enemy, kMaxEnemies> Enemies = {};
    FixedVector<Projectile, kMaxProjectiles> Projectiles = {};

    // Sets up the M01 level: a radius-3 grid with a hardcoded straight diameter path (spawn at one
    // edge, through the center, base at the opposite edge).
    void InitLevel();
    void BuildStraightPath(Hex start, int dir, int steps);
    // Flood-fills PathDirection on every path tile so each steps toward the neighbour one hex
    // closer to Goal (BFS from Goal over IsPath tiles). Tiles with no route to Goal are left at
    // NONE.
    void CalculatePath();

    void SpawnEnemy(Hex at);
    // Returns the live enemy with |id|, or nullptr if it has despawned/died. Linear scan.
    Enemy* FindEnemy(u32 id);
    // Advances each spawner's own timer by dt; spawns one enemy from a spawner when its timer
    // wraps.
    void UpdateSpawners(float dt);
    // Advances each enemy along the flow field; despawns those that reach the goal.
    void UpdateEnemies(float dt);
    // Ticks each tower's cooldown; a ready tower fires a projectile at the nearest enemy in range.
    void UpdateTowers(float dt);
    // Advances each projectile toward its target; on a distance check it deals damage and despawns
    // (killing the enemy if its health hits zero). Projectiles whose target is gone despawn too.
    void UpdateProjectiles(float dt);
};

// Marks |tile| as a spawner and seeds its SpawnTimer with a random [0,1)s phase offset, so multiple
// spawners fire out of sync instead of stacking enemies. Use this instead of setting Content by
// hand.
void MakeSpawner(Tile* tile);
// Marks |tile| as a tower, ready to fire immediately. Use this instead of setting Content by hand.
void MakeTower(Tile* tile);

enum class EOperationMode : u8 {
    TogglePath,
    SetPathGoal,
    ToggleSpawner,
    ToggleTower,
};
StringView ToString(EOperationMode mode);

// Every EOperationMode value, in menu order. Keep in sync with the enum (and ToString) when adding
// operations — this is what the ImGui operation selector iterates.
constexpr EOperationMode kOperationModes[] = {
    EOperationMode::TogglePath,
    EOperationMode::SetPathGoal,
    EOperationMode::ToggleSpawner,
    EOperationMode::ToggleTower,
};

struct GameState {
    EOperationMode CurrentOperation = EOperationMode::TogglePath;

    World World = {};
    int InternalDetectedReload = 0;
};

// Game entry points, called from the DLL's entrypoint.cpp. The entrypoint owns the platform<->DLL
// export machinery (the extern "C" OnGame*/OnSO* symbols, the GameState allocation + reload-time
// rebind); these stay plain functions that receive the state already allocated and cast, so they
// remain testable.
void GameInit(PlatformState* ps, GameState* gs);
void GameUpdate(PlatformState* ps, GameState* gs);
void GameRender(PlatformState* ps, GameState* gs);

}  // namespace kdk
