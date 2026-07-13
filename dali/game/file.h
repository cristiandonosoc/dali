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
// Queries |path|'s last-modified time. |out_ns| (if non-null) gets nanoseconds since the Unix epoch;
// |out_datetime| (if non-null) gets the broken-down date, local when |datetime_local| (else UTC).
// false on failure (or if the platform is unset). The defaults live here, not on the contract
// pointer (default args are illegal on a pointer-to-function).
bool GetFileModTime(StringView path,
                    i64* out_ns,
                    DateTime* out_datetime = nullptr,
                    bool datetime_local = true);

}  // namespace kdk
