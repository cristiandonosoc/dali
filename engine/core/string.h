#pragma once

#include <engine/core/container.h>
#include <engine/core/defines.h>

#include <SDL3/SDL_filesystem.h>

#include <cstring>
#include <span>
#include <string_view>

using namespace std::string_view_literals;

namespace std {
struct source_location;  // Forward declaration.
}  // namespace std

namespace kdk {

struct Arena;

struct String {
    static const char* kEmptyStrPtr;

    // You should not get this pointer directly if you want to use it for printing, since it might
    // be null. Use |Str()| instead.
    const char* _Str = nullptr;
    u64 Size = 0;

    // By default we create the empty value rather than null.
    // Easier for comparisons.
    String() : _Str(kEmptyStrPtr), Size(0) {}
    constexpr String(std::string_view sv) : _Str(sv.data()), Size(sv.size()) {}
    explicit String(const char* str) : _Str(str), Size(std::strlen(str)) {}
    constexpr explicit String(const char* str, u64 size) : _Str(str), Size(size) {}
    constexpr explicit String(std::span<u8> data)
        : _Str((const char*)data.data()), Size(data.size_bytes()) {}

    const char* Str() const { return _Str ? _Str : kEmptyStrPtr; }
    std::span<u8> ToSpan() const { return std::span<u8>((u8*)_Str, Size); }
    std::string_view ToSV() const { return std::string_view(_Str, Size); }

    bool IsEmpty() const { return Size == 0; }
    bool IsValid() const { return _Str != nullptr; }

    bool Equals(const char* str) const;
    bool Equals(const String& other) const;

    bool operator==(const String& other) const { return Equals(other); }

    // Subscript operator
    const char& operator[](u64 index) const {
        ASSERT(index < Size);
        return _Str[index];
    }

    // Iterator API
    const char* begin() const { return _Str; }
    const char* end() const { return _Str ? _Str + Size : nullptr; }
};

template <u64 CAPACITY>
struct FixedString {
    static constexpr u64 kCapacity = CAPACITY;

    Array<char, CAPACITY> _Chars;
    u32 Size = 0;

    FixedString() { Set(String()); }  // Default to empty string.
    FixedString(const char* str) { Set(str); }
    FixedString(std::string_view sv) { Set(String(sv)); }
    FixedString(String string) { Set(string); }

    void Set(const char* str, bool trap_truncation = false) { Set(String(str), trap_truncation); }
    void Set(String string, bool trap_truncation = false) {
        Size = (u32)string.Size;
        if (Size >= CAPACITY) {
            if (trap_truncation) {
                ASSERTF(false, "string exceeds limit");
            }
            Size = CAPACITY - 1;  // Leave space for null terminator.
        }
        std::memcpy(_Chars.DataPtr(), string.Str(), Size);
        _Chars[Size] = '\0';
    }

    String ToString() const { return String(_Chars.DataPtr(), Size); }
    const char* Str() const { return &_Chars[0]; }
    char* StrMutable() { return &_Chars[0]; }

    bool IsEmpty() const { return Size == 0; }

    bool operator==(const FixedString<CAPACITY>& other) const { return Equals(other.ToString()); }
    bool Equals(const String& other) const {
        String _this = ToString();
        return _this.Equals(other);
    }

    template <u64 OTHER_CAPACITY>
    bool operator<(const FixedString<OTHER_CAPACITY>& other) const {
        String this_str = ToString();
        String other_str = other.ToString();
        return std::strcmp(this_str.Str(), other_str.Str()) < 0;
    }
};

// Uses djb2 for now.
// http://www.cse.yorku.ca/~oz/hash.html
constexpr i32 CompileHash(const char* string) {
    u32 hash = 5381;

    while (true) {
        int c = *string++;
        if (c == 0) {
            break;
        }
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return (i32)hash;
}

inline i32 HashString(const char* string) { return CompileHash(string); }

// Literal operator for convenient usage with string literals
constexpr uint32_t operator"" _hash(const char* str, size_t) { return CompileHash(str); }

// Returns hash + 1 so we can use 0 as none;
inline i32 IDFromString(const char* string) { return HashString(string) + 1; }
inline i32 IDFromString(const String& string) { return IDFromString(string.Str()); }

// |length| MUST NOT include the zero terminator.
String InternStringToArena(Arena* arena, const char* string, u64 length = 0);
String InternStringToArena(Arena* arena, String string);

String Concat(Arena* arena, String a, String b);

String RemovePrefix(Arena* arena, String path, String prefix);

// Printf ------------------------------------------------------------------------------------------

String Printf(Arena* arena, const char* fmt, ...);
String ToString(Arena* arena, const std::source_location& location);

void PrintBacktrace(Arena* arena, u32 frames_to_skip = 0);

// Paths -------------------------------------------------------------------------------------------

namespace paths {

bool IsAbsolute(const char* path);
inline bool IsAbsolute(const String& path) { return IsAbsolute(path.Str()); }

// The directory this program was run from.
String GetBaseDir(Arena* arena);

String GetDirname(Arena* arena, String path);
String GetBasename(Arena* arena, String path);
String GetExtension(Arena* arena, String path);
String RemoveExtension(Arena* arena, String path);
String ChangeExtension(Arena* arena, String original, String new_ext);

inline String PathJoin(Arena*, String a) { return a; }  // Base case for recursion.
String PathJoin(Arena* arena, String a, String b);

// Recursive variadic template to handle arbitrary number of paths
template <typename... Paths>
String PathJoin(Arena* arena, String first, String second, Paths... rest) {
    // Join the first two paths, then recursively join with the rest
    // TODO(cdc): This is very dumb, as it will allocate every sub-path, but this ok for now.
    return PathJoin(arena, PathJoin(arena, first, second), rest...);
}

struct DirEntry {
    String Path = {};
    SDL_PathInfo Info = {};

    bool IsFile() const { return Info.type == SDL_PATHTYPE_FILE; }
    bool IsDir() const { return Info.type == SDL_PATHTYPE_DIRECTORY; }
};

std::span<DirEntry> ListDir(Arena* arena, String path);

// Useful for printing line numbers without the bazel nonesense.
String CleanPathFromBazel(String path);

}  // namespace paths

// System ------------------------------------------------------------------------------------------
// TODO(cdc): Move to a more "system" like place.

String GetEnv(Arena* arena, const char* env);

}  // namespace kdk
