#include <engine/core/string.h>

#include <engine/core/memory.h>

#include <SDL3/SDL.h>

#include <cwalk.h>

#include <stb/stb_sprintf.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>

#include <source_location>

namespace kdk {

const char* StringView::kEmptyStrPtr = "";

namespace string_private {

inline bool StrCmpWithLength(const char* s1, const char* s2, u64 size) {
    for (u64 i = 0; i < size; i++) {
        if (*s1++ != *s2++) {
            return false;
        }
    }

    return true;
}

}  // namespace string_private

bool StringView::Equals(const char* str) const {
    if (!IsValid()) {
        return false;
    }

    if (!str) {
        return false;
    }

    u64 size = std::strlen(str);
    if (Size != size) {
        return false;
    }

    if (_Str == str) {
        return true;
    }

    return string_private::StrCmpWithLength(_Str, str, Size);
}

bool StringView::Equals(const StringView& other) const {
    if (Size != other.Size) {
        return false;
    }

    // Since they are the same size, pointer comparison would mean they're equal.
    if (_Str == other._Str) {
        return true;
    }

    return string_private::StrCmpWithLength(_Str, other._Str, Size);
}

StringView InternStringToArena(Arena* arena, const char* string, u64 length) {
    if (length == 0) {
        length = std::strlen(string);
    }

    auto dst = ArenaPush(arena, length + 1);
    std::memcpy(dst.data(), string, length);
    dst[length] = 0;  // The null terminator.
    return StringView((char*)dst.data(), length);
}

StringView InternStringToArena(Arena* arena, StringView string) {
    return InternStringToArena(arena, string.Str(), string.Size);
}

StringView Concat(Arena* arena, StringView a, StringView b) {
    if (a.IsEmpty()) {
        return b;
    }

    if (b.IsEmpty()) {
        return a;
    }

    i64 buffer_size = a.Size + b.Size + 1;
    auto data = ArenaPush(arena, buffer_size);
    char* buffer = (char*)data.data();
    char* ptr = buffer;
    std::memcpy(ptr, a.Str(), a.Size);
    ptr += a.Size;
    std::memcpy(ptr, b.Str(), b.Size);
    ptr += b.Size;
    *ptr = 0;

    i64 strlen = buffer_size - 1;
    ASSERT(ptr - buffer == strlen);
    return StringView(buffer, strlen);
}

StringView RemovePrefix(Arena* arena, StringView path, StringView prefix) {
    if (path.IsEmpty() || prefix.IsEmpty()) {
        return path;
    }

    // Check if the path starts with the prefix. If prefix is longer than path, can't be a prefix
    if (path.Size < prefix.Size) {
        return path;
    }

    // Compare the prefix portion (case-sensitive comparison)
    if (std::strncmp(path.Str(), prefix.Str(), prefix.Size) != 0) {
        return path;  // Prefix doesn't match
    }

    // If the prefix matches exactly the whole path, return empty
    if (path.Size == prefix.Size) {
        return {};
    }

    // Skip past the prefix
    const char* remaining = path.Str() + prefix.Size;
    u64 remaining_size = path.Size - prefix.Size;

    // Skip leading separator if present
    if (remaining_size > 0 && (remaining[0] == '/' || remaining[0] == '\\')) {
        remaining++;
        remaining_size--;
    }

    if (remaining_size == 0) {
        return {};
    }

    return InternStringToArena(arena, remaining, remaining_size);
}

// Printf ------------------------------------------------------------------------------------------

StringView Printf(Arena* arena, const char* fmt, ...) {
    int size = 4 * STB_SPRINTF_MIN;
    auto data = ArenaPush(arena, size);
    char* buf = (char*)data.data();
    va_list va;
    va_start(va, fmt);
    int len = stbsp_vsnprintf(buf, size, fmt, va);
    va_end(va);

    return StringView(buf, len);
}

StringView ToString(Arena* arena, const std::source_location& location) {
    return Printf(arena,
                  "%s:%d (%s)",
                  location.file_name(),
                  location.line(),
                  location.function_name());
}

void PrintBacktrace(Arena* arena, u32 frames_to_skip) {
    // We don't want to get this function in the way.
    frames_to_skip++;

    constexpr u32 kFramesToCapture = 16;
    Array<void*, kFramesToCapture> frames;
    u32 frame_count =
        CaptureStackBackTrace(frames_to_skip, kFramesToCapture, frames.DataPtr(), NULL);

    HANDLE handle = GetCurrentProcess();
    if (!SymInitialize(handle, nullptr, true)) {
        char buffer[256];

        // Format the error message
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       GetLastError(),
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buffer,
                       256,
                       NULL);
        std::printf("ERROR: PrintBacktrace: SymInitialize: %s\n", buffer);
        return;
    }
    DEFER { SymCleanup(handle); };

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    // Symbol info buffer.
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)ArenaPushZero(arena, sizeof(SYMBOL_INFO) + 256).data();
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    std::printf("--- BACKTRACE ----------------------------------------------------------------\n");

    bool has_seen_valid_frame = false;
    u32 frames_skipped = 0;
    for (u32 i = 0; i < frame_count; i++) {
        u64 addr = (u64)frames[i];

        const char* function_name = nullptr;
        if (SymFromAddr(handle, addr, nullptr, symbol)) {
            function_name = symbol->Name;
        }

        if (!function_name) {
            // We don't want to show a bunch of "<unknown>" at the top if we can avoid it.
            if (has_seen_valid_frame) {
                std::printf("Frame %02d: <unknown>\n", i);
            } else {
                frames_skipped++;
            }
            continue;
        }

        if (!has_seen_valid_frame) {
            if (frames_skipped > 0) {
                std::printf("Skipped %d frames (were \"<unknown>\")\n", frames_skipped);
            }
            has_seen_valid_frame = true;
        }

        // Get the file and line info.
        StringView file("<unknown>"sv);
        u32 line_number = 0;
        DWORD displacement = 0;
        if (SymGetLineFromAddr64(handle, addr, &displacement, &line)) {
            file = StringView(line.FileName);
            line_number = (u32)line.LineNumber;

            file = paths::CleanPathFromBazel(file);
        }

        std::printf("Frame %02d: %s (%s:%d)\n",
                    i - frames_skipped,
                    function_name,
                    file.Str(),
                    line_number);
    }
}

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
    auto scratch = GetScratchArena(arena);
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
    auto scratch = GetScratchArena(arena);

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
    char* buffer = (char*)ArenaPush(arena, buffer_size).data();
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
        .Entries = ArenaPushArray<DirEntry>(arena, kMaxFilesInDirectory).data(),
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
    char* buf = (char*)ArenaPushZero(arena, 1024).data();
    size_t required_size;
    errno_t err = getenv_s(&required_size, buf, 1024, env);
    if (err) {
        return {};
    }

    return StringView(buf);
}

}  // namespace kdk
