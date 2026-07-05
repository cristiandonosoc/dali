#include <dali/core/api.h>

namespace kdk {

StringView ToString(ELogSeverity severity) {
    // clang-format off
    switch (severity) {
        case ELogSeverity::Info: return "INFO"sv;
        case ELogSeverity::Warning: return "WARNING"sv;
        case ELogSeverity::Error: return "ERROR"sv;
    }
    // clang-format on
    ASSERT(false);
    return "<UNKNOWN>"sv;
}

bool LoadedGameSO::IsValid() const {
    // clang-format off
    return OnGameInit != nullptr &&
           OnGameUpdate != nullptr &&
           OnGameRender != nullptr &&
           OnSOLoaded != nullptr &&
           OnSOUnloaded != nullptr;
    // clang-format on
}

}  // namespace kdk
