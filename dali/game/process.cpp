#include <dali/game/process.h>

#include <dali/game/platform_state.h>

namespace kdk {

ProcessResult RunProcess(Arena* arena, std::span<const StringView> args) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.RunProcess) {
        return {};
    }
    return ps->API.RunProcess(arena, args);
}

}  // namespace kdk
