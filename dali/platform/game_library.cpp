#include <dali/platform/game_library.h>

#include <dali/core/filesystem.h>

#include <SDL3/SDL.h>

namespace kdk {

namespace game_library_private {

bool ResolveEntryPoints(SDL_SharedObject* handle, LoadedGameSO* out) {
    auto load = [&](const char* name) -> SDL_FunctionPointer {
        SDL_FunctionPointer fn = SDL_LoadFunction(handle, name);
        if (!fn) {
            SDL_Log("ERROR: game DLL missing entry point '%s'", name);
        }
        return fn;
    };

    using EntryFn = bool (*)(PlatformState*);
    out->OnGameInit = (EntryFn)load(KDK_ON_GAME_INIT_NAME);
    out->OnGameUpdate = (EntryFn)load(KDK_ON_GAME_UPDATE_NAME);
    out->OnGameRender = (EntryFn)load(KDK_ON_GAME_RENDER_NAME);
    out->OnSOLoaded = (EntryFn)load(KDK_ON_SO_LOADED_NAME);
    out->OnSOUnloaded = (EntryFn)load(KDK_ON_SO_UNLOADED_NAME);

    if (!out->IsValid()) {
        *out = {};
        return false;
    }

    return true;
}

SDL_Time GetModifiedTime(const char* path) {
    SDL_PathInfo info = {};
    if (!SDL_GetPathInfo(path, &info)) {
        return 0;
    }
    return info.modify_time;
}

}  // namespace game_library_private

GameLibrary GameLibrary::Create(StringView dll_path) {
    GameLibrary gl = {};
    gl.Path.Set(dll_path);
    SDL_Log("[platform] Tracking DLL in %s", dll_path.Str());
    return gl;
}

bool GameLibrary::Load(PlatformState* ps) {
    ASSERT(!Path.IsEmpty());

    ScratchArena scratch = Arena::GetScratch();

    // Loaded copies go to <dir of Path>/loaded_game_dlls (created on demand), not beside the
    // original DLL, so they stay corralled in one sweepable place instead of cluttering the game
    // directory.
    StringView loaded_dir = paths::PathJoin(scratch,
                                            paths::GetDirname(scratch, Path.ToString()),
                                            StringView("loaded_game_dlls"));
    if (!SDL_CreateDirectory(loaded_dir.Str())) {
        SDL_Log("ERROR: creating loaded-DLL dir '%s': %s", loaded_dir.Str(), SDL_GetError());
        return false;
    }

    // Fresh temp name per load so neither the running image nor the original DLL is locked. A
    // wall-clock timestamp also keeps the name unique across process runs, so a stale copy left
    // mapped by a previous (crashed) run can never collide with this one.
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    StringView stem = paths::RemoveExtension(scratch, paths::GetBasename(scratch, Path.ToString()));
    const char* loaded_path =
        Printf(scratch, "%s/%s_%lld.dll", loaded_dir.Str(), stem.Str(), (long long)now).Str();

    if (!SDL_CopyFile(Path.Str(), loaded_path)) {
        SDL_Log("ERROR: copying game DLL '%s' -> '%s': %s",
                Path.Str(),
                loaded_path,
                SDL_GetError());
        return false;
    }

    SDL_SharedObject* handle = SDL_LoadObject(loaded_path);
    if (!handle) {
        SDL_Log("ERROR: loading game DLL '%s': %s", loaded_path, SDL_GetError());
        return false;
    }

    LoadedGameSO so = {};
    if (!game_library_private::ResolveEntryPoints(handle, &so)) {
        SDL_UnloadObject(handle);
        return false;
    }

    if (!so.OnSOLoaded(ps)) {
        SDL_Log("ERROR: OnSOLoaded failed for '%s'", loaded_path);
        SDL_UnloadObject(handle);
        return false;
    }

    SO = so;
    _Handle = handle;
    LoadedPath.Set(loaded_path);
    _ModifiedTime = game_library_private::GetModifiedTime(Path.Str());
    _LastLoadTime = ps->TimeTracking.TotalSeconds;

    SDL_Log("Loaded game DLL '%s'", loaded_path);
    return true;
}

void GameLibrary::Unload(PlatformState* ps) {
    if (!SO.IsValid()) {
        return;
    }

    SO.OnSOUnloaded(ps);
    SDL_UnloadObject(_Handle);

    SO = {};
    _Handle = nullptr;
    LoadedPath = {};
}

bool GameLibrary::IsDirty() const {
    if (Path.IsEmpty()) {
        return false;
    }
    SDL_Time current_mod_time = game_library_private::GetModifiedTime(Path.Str());
    return current_mod_time > _ModifiedTime;
}

bool GameLibrary::MaybeReload(PlatformState* ps) {
    // Throttle: never reload more than once per kReloadThrottleSeconds, so a burst of build-system
    // writes can't trigger a reload storm.
    if (ps->TimeTracking.TotalSeconds < _LastLoadTime + kReloadThrottleSeconds) {
        return false;
    }

    if (!IsDirty()) {
        return false;
    }

    // Sometimes build systems touch the SO before it is ready (or they do in succession).
    // We wait for a certain amount of frames since we detect it outdated to give a chance to
    // the build system to do its thing.
    {
        static u32 gSafetyFrames = 0;
        static constexpr u32 kSafetyFrameThreshold = 20;

        gSafetyFrames++;
        if (gSafetyFrames < kSafetyFrameThreshold) {
            return true;
        }
        gSafetyFrames = 0;
    }

    Unload(ps);
    if (!Load(ps)) {
        SDL_Log("ERROR: reload failed; game DLL is now unloaded");
        return false;
    }
    ps->GameLibraryState.ReloadCount++;

    return true;
}

}  // namespace kdk
