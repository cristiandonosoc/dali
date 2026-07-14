#pragma once

#include <dali/core/array.h>
#include <dali/core/defines.h>

#include <cstdarg>
#include <cstring>
#include <span>
#include <string_view>

using namespace std::string_view_literals;

namespace std {
struct source_location;  // Forward declaration.
}  // namespace std

namespace kdk {

struct Arena;

struct StringView {
    static const char* kEmptyStrPtr;

    // You should not get this pointer directly if you want to use it for printing, since it might
    // be null. Use |Str()| instead.
    const char* _Str = nullptr;
    u64 Size = 0;

    // By default we create the empty value rather than null.
    // Easier for comparisons.
    StringView() : _Str(kEmptyStrPtr), Size(0) {}
    constexpr StringView(std::string_view sv) : _Str(sv.data()), Size(sv.size()) {}
    explicit StringView(const char* str) : _Str(str), Size(std::strlen(str)) {}
    constexpr explicit StringView(const char* str, u64 size) : _Str(str), Size(size) {}
    constexpr explicit StringView(std::span<u8> data)
        : _Str((const char*)data.data()), Size(data.size_bytes()) {}

    const char* Str() const { return _Str ? _Str : kEmptyStrPtr; }
    std::span<u8> ToSpan() const { return std::span<u8>((u8*)_Str, Size); }
    std::string_view ToSV() const { return std::string_view(_Str, Size); }

    bool IsEmpty() const { return Size == 0; }
    bool IsValid() const { return _Str != nullptr; }

    bool Equals(const char* str) const;
    bool Equals(const StringView& other) const;

    bool operator==(const StringView& other) const { return Equals(other); }

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

    FixedString() { Set(StringView()); }  // Default to empty string.
    FixedString(const char* str) { Set(str); }
    FixedString(std::string_view sv) { Set(StringView(sv)); }
    FixedString(StringView string) { Set(string); }

    void Set(const char* str, bool trap_truncation = false) {
        Set(StringView(str), trap_truncation);
    }
    void Set(StringView string, bool trap_truncation = false) {
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

    StringView ToString() const { return StringView(_Chars.DataPtr(), Size); }
    operator StringView() const { return ToString(); }
    const char* Str() const { return &_Chars[0]; }
    char* StrMutable() { return &_Chars[0]; }

    bool IsEmpty() const { return Size == 0; }

    bool operator==(const FixedString<CAPACITY>& other) const { return Equals(other.ToString()); }
    bool Equals(const struct StringView& other) const {
        struct StringView _this = ToString();
        return _this.Equals(other);
    }

    template <u64 OTHER_CAPACITY>
    bool operator<(const FixedString<OTHER_CAPACITY>& other) const {
        struct StringView this_str = ToString();
        struct StringView other_str = other.ToString();
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
inline i32 IDFromString(const StringView& string) { return IDFromString(string.Str()); }

// |length| MUST NOT include the zero terminator.
StringView InternStringToArena(Arena* arena, const char* string, u64 length = 0);
StringView InternStringToArena(Arena* arena, StringView string);

StringView Concat(Arena* arena, StringView a, StringView b);

StringView RemovePrefix(Arena* arena, StringView path, StringView prefix);

// Printf ------------------------------------------------------------------------------------------

[[nodiscard]] StringView Printf(Arena* arena, const char* fmt, ...);
[[nodiscard]] StringView PrintfV(Arena* arena, const char* fmt, va_list args);
StringView ToString(Arena* arena, const std::source_location& location);

void PrintBacktrace(Arena* arena, u32 frames_to_skip = 0);

}  // namespace kdk
