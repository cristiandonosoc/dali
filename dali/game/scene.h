#pragma once

#include <dali/game/game.h>

namespace kdk {

struct AssetRegistry;

// Temporary YAML scene IO. Persists the editable grid state — which tiles are path, each tile's
// content, and the goal — so we can iterate on a layout across runs. The format is deliberately
// throwaway (see scene.cpp); do not build anything on top of it. Both return false on any IO or
// parse failure (e.g. LoadScene when the file does not exist yet).
//
// LoadScene needs |registry| to stamp scene towers from World::DefaultTower — the scene stores no
// per-tile blueprint, so every tower in it is the default one. Set World::DefaultTower and load the
// registry before calling.
bool SaveScene(const World& world, StringView path);
bool LoadScene(World* world, AssetRegistry* registry, StringView path);

}  // namespace kdk
