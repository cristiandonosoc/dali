#include <dali/core/string.h>

#include <dali/core/memory.h>
#include <dali/core/container.h>
#include <dali/core/filesystem.h>

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

    auto dst = arena->Push(length + 1);
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
    auto data = arena->Push(buffer_size);
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
    va_list va;
    va_start(va, fmt);
    StringView result = PrintfV(arena, fmt, va);
    va_end(va);

    return result;
}

StringView PrintfV(Arena* arena, const char* fmt, va_list args) {
    int size = 4 * STB_SPRINTF_MIN;
    auto data = arena->Push(size);
    char* buf = (char*)data.data();
    int len = stbsp_vsnprintf(buf, size, fmt, args);

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
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)arena->PushZero(sizeof(SYMBOL_INFO) + 256).data();
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


}  // namespace kdk
