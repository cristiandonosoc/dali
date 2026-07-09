#pragma once

#include <dali/game/hex.h>
#include <dali/core/container.h>

namespace kdk {

struct PlatformState;

struct Tile {
    Hex Hex = {};
    bool IsPath = false;
};

struct Grid {
    // Cap holds a radius-3 neighbourhood (3*r*(r+1)+1 = 37 tiles) with headroom.
    FixedVector<Tile, 64> Tiles;

    // Fills the grid with every hex within |radius| of the origin. radius 0 = 1 tile, radius 1 = a
    // center ringed by 6 (7 total), etc.
    void InitRing(int radius);
    Tile* FindTile(Hex hex);
};

// Ordered route enemies follow: spawn at [0], base at the last element. Kept separate from the
// grid because Tile::IsPath alone loses the ordering enemies need.
constexpr i32 kMaxPathTiles = 16;
using Path = FixedVector<Hex, kMaxPathTiles>;

// Builds a straight run of |steps| + 1 contiguous tiles from |start| walking in |dir| (0..5).
// Contiguity is true by construction (each tile is the previous one's Neighbour).
void BuildStraightPath(Path* out, Hex start, int dir, int steps);

struct World {
	int Count = 0;
    Grid Grid = {};
    Path Path = {};

    // Sets up the M01 level: a radius-3 grid with a hardcoded straight diameter path (spawn at one
    // edge, through the center, base at the opposite edge).
    void InitLevel();
};

struct GameState {
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


} // namespace kdk
