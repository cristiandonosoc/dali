#include <dali/game/platform_state.h>

#include <dali/core/defines.h>

namespace kdk {

namespace platform_state_private {

PlatformState* gPlatformState = nullptr;

}  // namespace platform_state_private

PlatformState* GetGlobalPlatformState() {
    ASSERT(platform_state_private::gPlatformState);
    return platform_state_private::gPlatformState;
}

void SetGlobalPlatformState(PlatformState* ps) { platform_state_private::gPlatformState = ps; }

}  // namespace kdk
