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

struct TileChunk {
    static constexpr i32 kRadius = 3;
    static constexpr i32 kWidth = 2 * kRadius + 1;  // Axial bounding-box side (7); box coords [0,7).
    static constexpr i32 kTileCount = 3 * kRadius * (kRadius + 1) + 1;  // 37 at radius 3.

    // Tiles are stored in spiral (ring-major) order: slot 0 is the center, then ring k occupies 6k
    // contiguous slots starting at 3k(k-1)+1. HexToIndex is the inverse, so a lookup is O(1).
    FixedVector<Tile, kTileCount> Tiles;

    // Fills the grid with every hex within kRadius of the origin, in spiral order (see Tiles).
    void InitRing();
    // Spiral slot of |hex| (its index in Tiles), or NONE if |hex| lies outside the grid. Pure coord
    // math via a generated table — no scan.
    static i32 HexToIndex(const Hex& hex);
    Tile* FindTile(const Hex& hex);
};

struct Grid {
	Hex Offset = {};
	TileChunk TileChunk = {};

	void Init();
	Tile* FindTile(const Hex& hex);
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

// A tower shot in flight. Each frame it refreshes LastSeen to the live target's position and flies
// toward it; on a distance check it applies Damage and despawns. If the target dies mid-flight the
// shot keeps flying to the last position it saw and despawns there without effect, so it lands
// visually instead of popping out of existence.
struct Projectile {
    Vec2 Position = {};  // World space, same frame as Enemy::Position.
    Vec2 LastSeen = {};  // Target's position as of the last frame it was alive; the shot flies here.
    u32 TargetId = 0;      // The enemy this shot is chasing.
    float Speed = 320.0f;  // Pixels per second.
    float Damage = 5.0f;
};

// One wave of enemies: a finite burst emitted from the map's spawn sources during the Wave phase.
struct Wave {
    int Number = 0;    // 1-based once armed; 0 means no wave has run yet.
    int ToSpawn = 0;   // Enemies left to emit this wave; the wave ends when this hits 0 and none
                       // remain alive.
    float SpawnTimer = 0.0f;  // Counts up to Cadence, then emits one enemy and wraps.
    float Cadence = 0.6f;     // Seconds between emissions.
    int SpawnCursor = 0;      // Round-robins emissions across SpawnSources when there are several.
};

struct World {
    static constexpr i32 kMaxPathTiles = 16;
    static constexpr i32 kMaxEnemies = 128;
    static constexpr i32 kMaxProjectiles = 256;
    static constexpr i32 kMaxSpawnSources = 8;
    static constexpr float kMaxBaseHealth = 100.0f;

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

    Wave Wave = {};
    // The tiles enemies emit from: path "leaves" that nothing flows into, derived from the flow
    // field by CollectSpawnSources. These are the map's outskirts.
    FixedVector<Hex, kMaxSpawnSources> SpawnSources = {};

    // Sets up the M01 level: a radius-3 grid with a hardcoded straight diameter path (spawn at one
    // edge, through the center, base at the opposite edge).
    void InitLevel();
    void BuildStraightPath(Hex start, int dir, int steps);
    // Flood-fills PathDirection on every path tile so each steps toward the neighbour one hex
    // closer to Goal (BFS from Goal over IsPath tiles). Tiles with no route to Goal are left at
    // NONE.
    void CalculatePath();

    void SpawnEnemy(Hex at, float health_scale = 1.0f);
    // Returns the live enemy with |id|, or nullptr if it has despawned/died. Linear scan.
    Enemy* FindEnemy(u32 id);
    // Resets per-run state (gold, base health, wave, live entities) and strips placed towers, then
    // recomputes the path and spawn sources. Call when leaving PreGame for Build.
    void BeginRun();
    // Rebuilds SpawnSources from the current flow field: every path tile that no other path tile
    // flows into.
    void CollectSpawnSources();
    // Advances to the next wave: bumps the number and seeds how many enemies it will emit.
    void ArmNextWave();
    // Sets every tower's FireCooldown to 0 (ready). Called when entering Build so each wave starts
    // with towers ready rather than mid-cooldown from the last one.
    void ResetTowerCooldowns();
    // During the Wave phase, drips this wave's enemies from the spawn sources on the cadence.
    void UpdateWave(float dt);
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

// Top-level game flow: PreGame -> Build <-> Wave -> GameOver. PreGame is the setup/editor phase;
// Build and Wave alternate (place towers, then fight a wave); GameOver ends the run.
enum class EGamePhase : u8 {
    PreGame,
    Build,
    Wave,
    GameOver,
};
StringView ToString(EGamePhase phase);

struct GameState {
    EGamePhase Phase = EGamePhase::PreGame;
    EOperationMode CurrentOperation = EOperationMode::TogglePath;

    // Screen-space camera pan (WASD). Added to the world's draw origin, so it offsets both what is
    // rendered and where clicks land.
	bool InverseCameraMovement = false;
    Vec2 Camera = {};
    // World->screen scale (mouse wheel). The sim stays in world units; only rendering and picking
    // scale by this.
    float Zoom = 1.0f;

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
