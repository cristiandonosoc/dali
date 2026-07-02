#include <dali/core/filesystem.h>

#include <dali/core/memory.h>

#include <SDL3/SDL_Log.h>

#include <cwalk.h>

namespace kdk {


// Paths -------------------------------------------------------------------------------------------

namespace paths {

namespace paths_private {

StringView gInitialDirectory = {};

}  // namespace paths_private

bool IsAbsolute(const char* path) {
    if (!path) {
        return false;
    }

    return cwk_path_is_absolute(path);
}

StringView GetBaseDir(Arena* arena) {
    using namespace paths_private;

    if (gInitialDirectory.IsEmpty()) {
        if (StringView ws = GetEnv(arena, "BUILD_WORKSPACE_DIRECTORY"); !ws.IsEmpty()) {
            gInitialDirectory = ws;
        } else {
            gInitialDirectory = StringView(SDL_GetCurrentDirectory());
        }
    }

    return gInitialDirectory;
}

StringView GetDirname(Arena* arena, StringView path) {
    if (path.IsEmpty()) {
        return {};
    }

    u64 size = 0;
    if (cwk_path_get_dirname(path.Str(), &size); size == 0) {
        return {};
    }

    return InternStringToArena(arena, path.Str(), size);
}

StringView GetBasename(Arena* arena, StringView path) {
    if (path.IsEmpty()) {
        return {};
    }

    const char* out = nullptr;
    u64 size = 0;
    if (cwk_path_get_basename(path.Str(), &out, &size); size == 0) {
        return {};
    }

    return InternStringToArena(arena, out, size);
}

StringView GetExtension(Arena* arena, StringView path) {
    if (path.IsEmpty()) {
        return {};
    }

    const char* extension = nullptr;
    u64 size = 0;
    if (cwk_path_get_extension(path.Str(), &extension, &size); size == 0) {
        return {};
    }

    return InternStringToArena(arena, extension, size);
}

StringView RemoveExtension(Arena* arena, StringView path) {
    auto scratch = Arena::GetScratch();
    StringView extension = GetExtension(scratch, path);
    if (extension.IsEmpty()) {
        return path;
    }

    // Special case where extension is the same as the input (hidden files).
    if (extension.Size == path.Size) {
        return path;
    }

    StringView result = StringView(path.Str(), path.Size - extension.Size);
    return InternStringToArena(arena, result);
}

StringView ChangeExtension(Arena* arena, StringView original, StringView new_ext) {
    if (new_ext.IsEmpty()) {
        return original;
    }

    ASSERT(new_ext.Str()[0] == '.');
    auto scratch = Arena::GetScratch();

    StringView clean = RemoveExtension(scratch, original);
    return Concat(arena, clean, new_ext);
}

StringView PathJoin(Arena* arena, StringView a, StringView b) {
    if (a.IsEmpty()) {
        return b;
    }

    if (b.IsEmpty()) {
        return a;
    }

    // Worst case we string them together.
    u64 buffer_size = a.Size + b.Size + 2;
    char* buffer = (char*)arena->Push(buffer_size).data();
    u64 size = 0;
    if (size = cwk_path_join(a.Str(), b.Str(), buffer, buffer_size); size == 0) {
        return {};
    }

    return StringView(buffer, size);
}

namespace string_private {

constexpr u32 kMaxFilesInDirectory = 512;

struct EnumerateDirectoryCallbackData {
    Arena* ResultArena = nullptr;
    DirEntry* Entries = nullptr;
    u32 EntryCount = 0;
};

SDL_EnumerationResult EnumerateDirectoryCallback(void* userdata,
                                                 const char* dirname,
                                                 const char* fname) {
    auto* data = (EnumerateDirectoryCallbackData*)userdata;

    ASSERTF(data->EntryCount < kMaxFilesInDirectory, "Time to up this limit :)");

    StringView file = PathJoin(data->ResultArena, StringView(dirname), StringView(fname));

    SDL_PathInfo info = {};
    if (!SDL_GetPathInfo(file.Str(), &info)) {
        SDL_Log("ERROR: Getting path info for %s: %s\n", file.Str(), SDL_GetError());
    }

    data->Entries[data->EntryCount++] = DirEntry{
        .Path = file,
        .Info = info,
    };

    return SDL_ENUM_CONTINUE;
}

}  // namespace string_private

std::span<DirEntry> ListDir(Arena* arena, StringView path) {
    using namespace string_private;

    EnumerateDirectoryCallbackData data{
        .ResultArena = arena,
        .Entries = arena->PushArray<DirEntry>(kMaxFilesInDirectory).data(),
    };

    if (!SDL_EnumerateDirectory(path.Str(), EnumerateDirectoryCallback, &data)) {
        SDL_Log("ERROR: enumerating directory %s: %s\n", path.Str(), SDL_GetError());
        return {};
    }

    return {data.Entries, data.EntryCount};
}

StringView CleanPathFromBazel(StringView path) {
    // Remove bazel nonesense.
    const char* bazel_marker = "_main\\";
    if (const char* marker_pos = strstr(path.Str(), bazel_marker)) {
        path = StringView(marker_pos + strlen(bazel_marker));
    }
    return path;
}

}  // namespace paths

// System ------------------------------------------------------------------------------------------

StringView GetEnv(Arena* arena, const char* env) {
    char* buf = (char*)arena->PushZero(1024).data();
    size_t required_size;
    errno_t err = getenv_s(&required_size, buf, 1024, env);
    if (err) {
        return {};
    }

    return StringView(buf);
}





} // namespace kdk
