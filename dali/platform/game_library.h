#pragma once

#include <dali/core/api.h>
#include <dali/core/string.h>

#include <SDL3/SDL_loadso.h>

namespace kdk {

// Platform-side owner of the loaded game DLL. Wraps the contract's LoadedGameSO with the OS module
// handle and the bookkeeping needed to hot-reload: the path we watch, the temp copy we actually
// load (so the build system can overwrite the original while we run), and the last-seen write time.
//
// SDL owns the actual loading (SDL_LoadObject / SDL_LoadFunction), so this stays port-agnostic.
struct GameLibrary {
    // Minimum seconds between (re)loads, so a burst of build-system writes can't trigger a reload
    // storm. Checked in MaybeReload against |PlatformState::TotalSeconds|.
    static constexpr f64 kReloadThrottleSeconds = 10.0;

    LoadedGameSO SO = {};

    // The built DLL we watch for changes.
    FixedString<256> Path = {};
    // The temp copy we actually load, under <dir of Path>/loaded_game_dlls. A timestamped name per
    // load avoids the OS file lock on |Path| and stays unique across process runs.
    FixedString<256> LoadedPath = {};

    SDL_SharedObject* _Handle = nullptr;
    SDL_Time _ModifiedTime = 0;  // |Path| last-write time captured at the last successful load.
    f64 _LastLoadTime = 0;       // |PlatformState::TotalSeconds| at the last successful load.

    static GameLibrary Create(StringView dll_path);

    // Copies |Path| to a fresh temp file, loads it, resolves the five entry points, and fires
    // OnSOLoaded. Returns false (and loads nothing) on any failure.
    bool Load(PlatformState* ps, bool is_reload);

    // Fires OnSOUnloaded, unloads the module, and clears the resolved SO. Safe on an empty library.
    void Unload(PlatformState* ps);

    // True if |Path| on disk is newer than the currently loaded image.
    bool IsDirty() const;

    // Reloads (Unload + Load) if IsDirty(). Returns true if a reload actually happened.
    bool MaybeReload(PlatformState* ps);
};

}  // namespace kdk
