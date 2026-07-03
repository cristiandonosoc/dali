#include <dali/core/api.h>

namespace kdk {

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
