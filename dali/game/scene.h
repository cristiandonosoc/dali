#pragma once

#include <dali/game/game.h>

namespace kdk {

// Temporary YAML scene IO. Persists the editable grid state — which tiles are path, each tile's
// content, and the goal — so we can iterate on a layout across runs. The format is deliberately
// throwaway (see scene.cpp); do not build anything on top of it. Both return false on any IO or
// parse failure (e.g. LoadScene when the file does not exist yet).
bool SaveScene(const World& world, StringView path);
bool LoadScene(World* world, StringView path);

}  // namespace kdk
