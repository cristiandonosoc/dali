#include <dali/game/log.h>

#include <dali/core/memory.h>
#include <dali/core/string.h>
#include <dali/game/platform_state.h>

#include <cstdarg>

namespace kdk {

namespace log_private {

void LogV(ELogSeverity severity, const char* fmt, va_list args) {
    auto scratch = Arena::GetScratch();
    StringView message = PrintfV(scratch, fmt, args);
    GetGlobalPlatformState()->API.Log(severity, message);
}

}  // namespace log_private

void Log(ELogSeverity severity, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_private::LogV(severity, fmt, args);
    va_end(args);
}

void LogInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_private::LogV(ELogSeverity::Info, fmt, args);
    va_end(args);
};

void LogWarning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_private::LogV(ELogSeverity::Warning, fmt, args);
    va_end(args);
}

void LogError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_private::LogV(ELogSeverity::Error, fmt, args);
    va_end(args);
}

}  // namespace kdk
