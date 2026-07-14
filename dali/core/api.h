#pragma once

#include <dali/core/defines.h>
#include <dali/core/input.h>
#include <dali/core/memory.h>
#include <dali/core/string.h>

#include <span>

namespace kdk {

// The platform<->game contract. Included by BOTH the platform exe and the game DLL. No
// implementation lives here: types and function-pointer typedefs only, so it never drags SDL, GL
// or <windows.h> into either binary.

// PLATFORM ----------------------------------------------------------------------------------------

enum class ELogSeverity : u8 {
    Info,
    Warning,
    Error,
};
StringView ToString(ELogSeverity severity);

struct Window {
    FixedString<128> Name = {};
    i32 Width = 0;
    i32 Height = 0;

    // Opaque handle to the platform's window object (SDL_Window* under the hood).
    void* PlatformData = nullptr;
};

struct FileContents {
    std::span<u8> Data = {};
    bool IsValid() const { return Data.data() != nullptr; }
};

// One file-type filter for a native file dialog, e.g. {"Images", "png,jpg,jpeg"}. Extensions are
// comma-separated, no dots.
struct FileDialogFilter {
    const char* Name = nullptr;
    const char* Extensions = nullptr;
};

// A wall-clock timestamp broken into calendar fields, plus the zone offset it was rendered in
// (self-describing, so display can label it and a failed offset can't be mistaken for UTC). The
// platform fills this from a raw timestamp; it is the only structured time in the contract.
struct DateTime {
    i32 Year = 0;
    i32 Month = 0;   // 1-12
    i32 Day = 0;     // 1-31
    i32 Hour = 0;    // 0-23
    i32 Minute = 0;  // 0-59
    i32 Second = 0;  // 0-59
    i32 UtcOffsetSeconds = 0;  // seconds east of UTC (0 == UTC); DST-correct for this instant
};

// The outcome of a synchronous subprocess run (see PlatformAPI::RunProcess).
struct ProcessResult {
    bool Launched = false;   // whether the process actually started (false == spawn failed)
    i32 ExitCode = 0;        // the process's exit code; meaningful only when Launched
    StringView Stdout = {};  // captured stdout, interned into the caller's arena (may be empty)
};

// Services the platform provides to the game. Plain function pointers (not virtuals): the struct is
// a POD filled in by the platform, so it crosses the DLL boundary with no vtable coupling and a
// reload never invalidates it.
struct PlatformAPI {
    // Reads a whole file into |arena|. Invalid FileContents on failure. The buffer is null-
    // terminated one byte past Data's end (not counted in the size), so text callers can read
    // Data.data() as a C string.
    FileContents (*ReadFile)(Arena* arena, StringView path) = nullptr;
    // Writes |data| to |path|, creating any missing parent directories. false on failure.
    bool (*WriteFile)(StringView path, std::span<const u8> data) = nullptr;

    // Appends one line to the platform log.
    void (*Log)(ELogSeverity severity, StringView message) = nullptr;

    // Resolves a GL entry point (wraps SDL_GL_GetProcAddress). The game's GL loader calls this from
    // OnSOLoaded to rebuild its function table after a reload.
    void* (*GLGetProcAddress)(const char* name) = nullptr;

    // Opens a native, modal "open file" dialog. On success returns true and writes the chosen
    // absolute path into |out_path| (interned into |arena|); returns false on cancel or error.
    // |filters| are the file-type filters offered (empty for "any file").
    bool (*OpenFileDialog)(Arena* arena,
                           StringView* out_path,
                           std::span<const FileDialogFilter> filters) = nullptr;

    // Opens a native, modal "select folder" dialog. On success returns true and writes the chosen
    // absolute directory path into |out_path| (interned into |arena|); false on cancel or error.
    bool (*OpenFolderDialog)(Arena* arena, StringView* out_path) = nullptr;

    // Opens |path| in the OS file manager. If |path| is a file, opens its containing directory
    // instead. |path| should be absolute. No-op on failure.
    void (*OpenContainingFolder)(StringView path) = nullptr;

    // Queries |path|'s last-modified time. The raw number is the primitive; the calendar breakdown
    // is an optional convenience the platform fills (only it has the timezone rules).
    //   |out_ns|       - if non-null, filled with nanoseconds since the Unix epoch (SDL_Time).
    //   |out_datetime| - if non-null, filled with the broken-down date (see |datetime_local|).
    //   |datetime_local| - true breaks |out_datetime| into local wall clock (DST-correct for that
    //                      instant); false into UTC. Ignored when |out_datetime| is null.
    // false if the path does not exist or cannot be queried (out-params left untouched). No default
    // args here: they are illegal on a pointer-to-function; the game-side wrapper (file.h) supplies
    // them.
    bool (*GetFileModTime)(StringView path,
                           i64* out_ns,
                           DateTime* out_datetime,
                           bool datetime_local) = nullptr;

    // Runs |args| synchronously (argv[0] is the executable, resolved on PATH), blocking until it
    // exits, and captures its stdout into |arena|. Injection-safe: args are a vector, never a shell
    // string. No working-directory parameter (the SDL process API has none) - pass a tool-specific
    // flag such as `git -C <dir>`. Spawning a process is far costlier than a file op, so callers
    // must run it on-demand / cache the result, never per-frame. Editor tooling only in practice.
    ProcessResult (*RunProcess)(Arena* arena, std::span<const StringView> args) = nullptr;
};

// Host-owned memory. Lives in the platform exe, so it survives DLL reloads untouched.
struct PlatformMemory {
    // The game allocates its GameState here; persists across reloads.
    Arena PermanentArena = {};
    // Reset by the platform every frame.
    Arena FrameArena = {};
};

struct PlatformImGuiState {
    using ImGuiMemAllocFunc = void* (*)(size_t size, void* user_data);
    using ImGuiMemFreeFunc = void (*)(void* ptr, void* user_data);

    void* Context = nullptr;
    ImGuiMemAllocFunc AllocFunc = nullptr;
    ImGuiMemFreeFunc FreeFunc = nullptr;
};

struct PlatformTimeTracking {
    // The cpu ticks at the moment this time tracking was started.
    u64 StartFrameTicks = 0;
    u64 LastFrameTicks = 0;
    // How much time we have been paused, which have to be offseted.
    double PauseOffsetSeconds = 0;

    double DeltaSeconds = 0;
    double TotalSeconds = 0;
};

struct PlatformGameLibraryState {
    int ReloadCount = 0;
};

// The single handle the platform passes to the game on every entry point. Everything that must
// outlive a DLL reload is reachable from here, so reloading the game image loses nothing.
struct PlatformState {
    FixedString<128> BasePath;

	u64 FrameNumber = 0;

    PlatformAPI API = {};
    PlatformMemory Memory = {};
    PlatformImGuiState ImGuiState = {};
    PlatformTimeTracking TimeTracking = {};
    PlatformGameLibraryState GameLibraryState = {};
    InputState Input = {};

    // The main window, created and owned by the platform.
    Window MainWindow = {};

    // The game's root state. Allocated once by OnGameInit into Memory.PermanentArena; opaque here.
    void* GameState = nullptr;

    // The game clears this to request shutdown.
    bool Running = true;
};

// GAME API ----------------------------------------------------------------------------------------
//
// The entry points the game DLL exports. The platform resolves them by name after LoadLibrary; the
// KDK_*_NAME strings must match the exported symbols in game.cpp.

#define KDK_ON_GAME_INIT_NAME "OnGameInit"
#define KDK_ON_GAME_UPDATE_NAME "OnGameUpdate"
#define KDK_ON_GAME_RENDER_NAME "OnGameRender"
#define KDK_ON_SO_LOADED_NAME "OnSOLoaded"
#define KDK_ON_SO_UNLOADED_NAME "OnSOUnloaded"

// The export contract, spelled out: the exact set of functions the game DLL must export, their
// signatures, and (via the KDK_*_NAME strings above, which match these member names) their symbol
// names. Only the platform ever instantiates this — it resolves each pointer after load and holds
// the table in its GameLibrary. The OS module handle and reload timestamp are platform-only
// concerns and live in that wrapper (dali/platform/game_library.h), not here.
struct LoadedGameSO {
    // Called once, after memory and window are ready. Allocates GameState into PermanentArena.
    bool (*OnGameInit)(PlatformState*) = nullptr;
    // Advances the simulation one frame.
    bool (*OnGameUpdate)(PlatformState*) = nullptr;
    // Renders one frame.
    bool (*OnGameRender)(PlatformState*) = nullptr;
    // Called immediately after every (re)load. Re-sync process-global state a fresh DLL image lost:
    // GL function table, ImGui context, etc.
    bool (*OnSOLoaded)(PlatformState*, bool is_reload) = nullptr;
    // Called immediately before the DLL is unloaded for a reload. Tear down what OnSOLoaded set up.
    bool (*OnSOUnloaded)(PlatformState*) = nullptr;

    bool IsValid() const;
};

}  // namespace kdk
