#include <dali/game/platform.h>

#include <dali/core/defines.h>
#include <dali/core/memory.h>
#include <dali/core/string.h>

#include <cstdarg>

namespace kdk {

namespace platform_private {

PlatformState* gPlatformState = nullptr;

void LogV(ELogSeverity severity, const char* fmt, va_list args) {
    auto scratch = Arena::GetScratch();
    StringView message = PrintfV(scratch, fmt, args);
    GetGlobalPlatformState()->API.Log(severity, message);
}

}  // namespace platform_private

PlatformState* GetGlobalPlatformState() {
    ASSERT(platform_private::gPlatformState);
    return platform_private::gPlatformState;
}

void SetGlobalPlatformState(PlatformState* ps) { platform_private::gPlatformState = ps; }

void Log(ELogSeverity severity, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    platform_private::LogV(severity, fmt, args);
    va_end(args);
}

void LogInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    platform_private::LogV(ELogSeverity::Info, fmt, args);
    va_end(args);
};

void LogWarning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    platform_private::LogV(ELogSeverity::Warning, fmt, args);
    va_end(args);
}

void LogError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    platform_private::LogV(ELogSeverity::Error, fmt, args);
    va_end(args);
}

FileContents ReadFile(Arena* arena, StringView path) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.ReadFile) {
        return {};
    }
    return ps->API.ReadFile(arena, path);
}

bool WriteFile(StringView path, std::span<const u8> data) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.WriteFile) {
        return false;
    }
    return ps->API.WriteFile(path, data);
}

bool GetFileModTime(StringView path, i64* out_ns, DateTime* out_datetime, bool datetime_local) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.GetFileModTime) {
        return false;
    }
    return ps->API.GetFileModTime(path, out_ns, out_datetime, datetime_local);
}

ProcessResult RunProcess(Arena* arena, std::span<const StringView> args) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.RunProcess) {
        return {};
    }
    return ps->API.RunProcess(arena, args);
}

}  // namespace kdk
