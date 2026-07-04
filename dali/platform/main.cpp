#include <SDL3/SDL_timer.h>
#include <dali/core/container.h>
#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/platform/arg_parser.h>
#include <dali/platform/game_library.h>

#include <SDL3/SDL.h>

namespace kdk::main_private {

PlatformState* gPlatformState = nullptr;

const char gWindowName[] = "DALI";
constexpr int gWindowWidth = 1024;
constexpr int gWindowHeight = 728;

bool InitWindow(Window* window) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("ERROR: Initializing SDL: %s\n", SDL_GetError());
        return false;
    }

    // Setup window.
    SDL_Window* sdl_window =
        SDL_CreateWindow(gWindowName, gWindowWidth, gWindowHeight, SDL_WINDOW_OPENGL);
    if (!sdl_window) {
        SDL_Log("ERROR: Creating SDL Window: %s\n", SDL_GetError());
        return false;
    }

    // void* native_handle = window_private::NFD_GetNativeWindowFromSDLWindow(sdl_window);
    // if (!native_handle) {
    //     SDL_Log("ERROR: Getting native window handle from SDL window");
    //     return false;
    // }

    SDL_SetWindowPosition(sdl_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // TODO(cdc): Setup OpenGL context.

    SDL_ShowWindow(sdl_window);

    ResetStruct(window);
    window->Name = gWindowName;
    window->Width = gWindowWidth;
    window->Height = gWindowHeight;
    // window->SDLWindow = sdl_window,
    // window->NativeWindowHandle = native_handle,
    // window->GLContext = gl_context,
    // window->GLSLVersion = glsl_version,

    return true;
}

bool InitPlatform(PlatformState* ps) {
    (void)ps;

    auto scratch = Arena::GetScratch();
    ps->BasePath = paths::GetBaseDir(scratch);
    // ps->ArchetypesFilePath =
    //     paths::PathJoin(platform::GetStringArena(), ps->BasePath, "scenes/archetypes.yml"sv);
    SDL_Log("Running from: %s", ps->BasePath.Str());

    // Init Memory.
    ps->Memory.PermanentArena = Arena::Allocate("PermanentArena"sv, 100 * MEGABYTE);
    ps->Memory.FrameArena = Arena::Allocate("FrameArena"sv, 50 % MEGABYTE);

    if (!InitWindow(&ps->MainWindow)) {
        SDL_Log("ERROR: Init window");
        return false;
    }

    return true;
}

void ShutdownPlatform(PlatformState* ps) {
    Arena::Free(ps->Memory.FrameArena.GetPtr());
    Arena::Free(ps->Memory.PermanentArena.GetPtr());
}

}  // namespace kdk::main_private

int main(int argc, const char* argv[]) {
    using namespace kdk;
    using namespace kdk::main_private;

    ArgParser ap = {};
    AddStringArgument(&ap, "so"sv, NULL, true);

    if (!ParseArguments(&ap, argc, argv)) {
        printf("ERROR: parsing arguments");
        return -1;
    }

    Arena init_arena = Arena::Allocate("InitArena"sv, 64 * KILOBYTE);
    DEFER { Arena::Free(&init_arena); };

    StringView so_path;
    {
        bool ok = FindStringValue(ap, "so"sv, &so_path);
        ASSERT(ok);

        if (!paths::IsAbsolute(so_path)) {
            so_path = paths::PathJoin(&init_arena, paths::GetBaseDir(&init_arena), so_path);
        }
    }

    // Allocate platform state. malloc gives raw memory, so run the default member initializers.
    gPlatformState = (PlatformState*)malloc(sizeof(PlatformState));
    ResetStruct(gPlatformState);

    GameLibrary gl = GameLibrary::Create(so_path);
    if (!gl.Load(gPlatformState)) {
        printf("ERROR: Loading library");
        return -1;
    }

    if (!InitPlatform(gPlatformState)) {
        return -1;
    }

    // Verify the load-and-call flow: init once, then a few update/render ticks, then unload.
    gl.SO.OnGameInit(gPlatformState);
    for (int i = 0; i < 10; ++i) {
        gl.SO.OnGameUpdate(gPlatformState);
        gl.SO.OnGameRender(gPlatformState);
		SDL_Delay(1000);
    }
    gl.Unload(gPlatformState);

    free(gPlatformState);
    return 0;
}
