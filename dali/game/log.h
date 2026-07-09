#pragma once

#include <dali/core/api.h>

namespace kdk {

void Log(ELogSeverity severity, const char* fmt, ...);

void LogInfo(const char* fmt, ...);
void LogWarning(const char* fmt, ...);
void LogError(const char* fmt, ...);

}  // namespace kdk
