#pragma once

#include <dali/core/string.h>

#include <SDL3/SDL_filesystem.h>

namespace kdk {

// Paths -------------------------------------------------------------------------------------------

namespace paths {

bool IsAbsolute(const char* path);
inline bool IsAbsolute(const StringView& path) { return IsAbsolute(path.Str()); }

// The directory this program was run from.
StringView GetBaseDir(Arena* arena);

StringView GetDirname(Arena* arena, StringView path);
StringView GetBasename(Arena* arena, StringView path);
StringView GetExtension(Arena* arena, StringView path);
StringView RemoveExtension(Arena* arena, StringView path);
StringView ChangeExtension(Arena* arena, StringView original, StringView new_ext);

inline StringView PathJoin(Arena*, StringView a) { return a; }  // Base case for recursion.
StringView PathJoin(Arena* arena, StringView a, StringView b);

// Recursive variadic template to handle arbitrary number of paths
template <typename... Paths>
StringView PathJoin(Arena* arena, StringView first, StringView second, Paths... rest) {
    // Join the first two paths, then recursively join with the rest
    // TODO(cdc): This is very dumb, as it will allocate every sub-path, but this ok for now.
    return PathJoin(arena, PathJoin(arena, first, second), rest...);
}

struct DirEntry {
    StringView Path = {};
    SDL_PathInfo Info = {};

    bool IsFile() const { return Info.type == SDL_PATHTYPE_FILE; }
    bool IsDir() const { return Info.type == SDL_PATHTYPE_DIRECTORY; }
};

std::span<DirEntry> ListDir(Arena* arena, StringView path);

// Useful for printing line numbers without the bazel nonesense.
StringView CleanPathFromBazel(StringView path);

}  // namespace paths

// System ------------------------------------------------------------------------------------------
// TODO(cdc): Move to a more "system" like place.

StringView GetEnv(Arena* arena, const char* env);



} // namespace kdk
