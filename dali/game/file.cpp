#include <dali/game/file.h>

#include <dali/game/platform_state.h>

namespace kdk {

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

}  // namespace kdk
