#pragma once

#include <dali/core/api.h>
#include <dali/core/string.h>

#include <span>

namespace kdk {

struct Arena;

// Thin game-side wrappers over the host platform API (PlatformState::API), all resolved through the
// ambient GetGlobalPlatformState() below. Gameplay/asset/tooling code goes through these instead of
// touching the OS directly, so every host interaction stays on the platform (the port surface). One
// file per concern used to live here (file.h/log.h/process.h/platform_state.h); they are
// consolidated so the port surface is one place.

// --- Platform state ------------------------------------------------------------------------------

// Ambient access to the one PlatformState the game DLL is currently attached to. Rebound on every
// OnSOLoaded (and cleared on OnSOUnloaded, see game.cpp), so it survives hot-reload without being
// threaded through every call — infrastructure hanging off PlatformState (memory, file IO,
// logging) is the same for the whole process, so there's nothing to gain by passing it explicitly.
// Domain/gameplay state hanging off PlatformState::GameState should still be passed explicitly.
//
// Not thread-safe. Fine while the game DLL is single-threaded; revisit if that changes.
PlatformState* GetGlobalPlatformState();
void SetGlobalPlatformState(PlatformState* ps);

// --- Logging -------------------------------------------------------------------------------------

void Log(ELogSeverity severity, const char* fmt, ...);

void LogInfo(const char* fmt, ...);
void LogWarning(const char* fmt, ...);
void LogError(const char* fmt, ...);

// --- Files ---------------------------------------------------------------------------------------

// Reads a whole file into |arena|. Invalid FileContents on failure (or if the platform is unset).
FileContents ReadFile(Arena* arena, StringView path);
// Writes |data| to |path|, creating any missing parent directories. false on failure.
bool WriteFile(StringView path, std::span<const u8> data);
// Queries |path|'s last-modified time. |out_ns| (if non-null) gets nanoseconds since the Unix epoch;
// |out_datetime| (if non-null) gets the broken-down date, local when |datetime_local| (else UTC).
// false on failure (or if the platform is unset). The defaults live here, not on the contract
// pointer (default args are illegal on a pointer-to-function).
bool GetFileModTime(StringView path,
                    i64* out_ns,
                    DateTime* out_datetime = nullptr,
                    bool datetime_local = true);

// --- Processes -----------------------------------------------------------------------------------

// Runs |args| synchronously (argv[0] = executable on PATH), blocking until it exits, and captures
// stdout into |arena|. Result.Launched is false on failure (or if the platform is unset). No
// working directory - use a tool flag such as `git -C <dir>`. Editor/tooling only in practice - the
// shipping game has no reason to spawn a subprocess. Costly (a process spawn): run on-demand / cache
// the result, never per-frame.
ProcessResult RunProcess(Arena* arena, std::span<const StringView> args);

}  // namespace kdk
