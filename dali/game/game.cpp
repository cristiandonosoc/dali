#include <dali/core/api.h>
#include <dali/game/platform_state.h>

#include <cstdio>

// The hot-reloadable game DLL. For now every entry point just prints, so we can verify the
// platform's load-and-call flow end to end before there is any real gameplay. The five functions
// are exported with C linkage so their symbol names match the KDK_*_NAME strings the platform
// resolves (see dali/core/api.h).

extern "C" {

KDK_API bool OnGameInit(kdk::PlatformState* ps) {
    (void)ps;
    printf("[game] OnGameInit\n");
    return true;
}

KDK_API bool OnGameUpdate(kdk::PlatformState* ps) {
    (void)ps;
    printf("[game] OnGameUpdate\n");
    return true;
}

KDK_API bool OnGameRender(kdk::PlatformState* ps) {
    (void)ps;
    printf("[game] OnGameRender\n");
    return true;
}

KDK_API bool OnSOLoaded(kdk::PlatformState* ps) {
    kdk::SetGlobalPlatformState(ps);
    printf("[game] OnSOLoaded\n");
    return true;
}

KDK_API bool OnSOUnloaded(kdk::PlatformState* ps) {
    (void)ps;
    printf("[game] OnSOUnloaded\n");
    kdk::SetGlobalPlatformState(nullptr);
    return true;
}

}  // extern "C"
