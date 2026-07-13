#pragma once

#include <dali/core/api.h>
#include <dali/core/string.h>

#include <span>

namespace kdk {

struct Arena;

// Thin game-side wrapper over the platform's process API (PlatformState::API), resolved through the
// ambient GetGlobalPlatformState() like file.h. Editor/tooling only in practice - the shipping game
// has no reason to spawn a subprocess.

// Runs |args| synchronously (argv[0] = executable on PATH), blocking until it exits, and captures
// stdout into |arena|. Result.Launched is false on failure (or if the platform is unset). No
// working directory - use a tool flag such as `git -C <dir>`. Costly (a process spawn): run
// on-demand / cache the result, never per-frame.
ProcessResult RunProcess(Arena* arena, std::span<const StringView> args);

}  // namespace kdk
