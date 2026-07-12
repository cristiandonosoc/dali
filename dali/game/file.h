#pragma once

#include <dali/core/api.h>
#include <dali/core/string.h>

#include <span>

namespace kdk {

struct Arena;

// Thin game-side wrappers over the platform file API (PlatformState::API), resolved through the
// ambient GetGlobalPlatformState() like log.h. Gameplay/asset code goes through these instead of
// std::fstream so all file IO stays on the platform (the port surface).

// Reads a whole file into |arena|. Invalid FileContents on failure (or if the platform is unset).
FileContents ReadFile(Arena* arena, StringView path);
// Writes |data| to |path|, creating any missing parent directories. false on failure.
bool WriteFile(StringView path, std::span<const u8> data);

}  // namespace kdk
