#include <dali/platform/platform.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>

#include <glad/gl.h>

#include <SDL3/SDL.h>

namespace kdk {

namespace platform_private {

const char kWindowName[] = "DALI";
constexpr int kWindowWidth = 1024;
constexpr int kWindowHeight = 728;

// Owns the OS/GL handles behind Window::PlatformData. Kept out of dali/core/api.h since it's a
// platform-only concern (SDL types can't cross the platform<->game contract).
struct WindowPlatformData {
    SDL_Window* SDLWindow = nullptr;
    SDL_GLContext GLContext = nullptr;
};

void Log(ELogSeverity severity, StringView message) {
    SDL_Log("[%s] %s", ToString(severity), message.Str());
}

bool InitWindow(PlatformState* ps) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("ERROR: Initializing SDL: %s\n", SDL_GetError());
        return false;
    }

    SDL_Window* sdl_window =
        SDL_CreateWindow(kWindowName, kWindowWidth, kWindowHeight, SDL_WINDOW_OPENGL);
    if (!sdl_window) {
        SDL_Log("ERROR: Creating SDL Window: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetWindowPosition(sdl_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_GLContext gl_context = SDL_GL_CreateContext(sdl_window);
    if (!gl_context) {
        SDL_Log("ERROR: Creating OpenGL Context: %s\n", SDL_GetError());
        return false;
    }

    if (!SDL_GL_MakeCurrent(sdl_window, gl_context)) {
        SDL_Log("ERROR: Making OpenGL context current: %s\n", SDL_GetError());
        return false;
    }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_Log("ERROR: Loading OpenGL functions with GLAD\n");
        return false;
    }

    if (!SDL_GL_SetSwapInterval(1)) {
        SDL_Log("Unable to set VSYNC: %s\n", SDL_GetError());
    }

    SDL_ShowWindow(sdl_window);

    auto* platform_data = ps->Memory.PermanentArena.PushInit<WindowPlatformData>();
    platform_data->SDLWindow = sdl_window;
    platform_data->GLContext = gl_context;

    ResetStruct(&ps->MainWindow);
    ps->MainWindow.Name = kWindowName;
    ps->MainWindow.Width = kWindowWidth;
    ps->MainWindow.Height = kWindowHeight;
    ps->MainWindow.PlatformData = platform_data;

    return true;
}

void ShutdownWindow(PlatformState* ps) {
    if (!ps->MainWindow.PlatformData) {
        return;
    }

    auto* platform_data = (WindowPlatformData*)ps->MainWindow.PlatformData;
    if (platform_data->GLContext) {
        SDL_GL_DestroyContext(platform_data->GLContext);
    }
    if (platform_data->SDLWindow) {
        SDL_DestroyWindow(platform_data->SDLWindow);
    }

    ps->MainWindow.PlatformData = nullptr;
}

}  // namespace platform_private

bool InitPlatform(PlatformState* ps) {
    using namespace platform_private;

    ps->API.Log = Log;

    auto scratch = Arena::GetScratch();
    ps->BasePath = paths::GetBaseDir(scratch);
    SDL_Log("Running from: %s", ps->BasePath.Str());

    ps->Memory.PermanentArena = Arena::Allocate("PermanentArena"sv, 100 * MEGABYTE);
    ps->Memory.FrameArena = Arena::Allocate("FrameArena"sv, 50 * MEGABYTE);

    if (!InitWindow(ps)) {
        SDL_Log("ERROR: Initializing window");
        return false;
    }

    return true;
}

void ShutdownPlatform(PlatformState* ps) {
    platform_private::ShutdownWindow(ps);

    Arena::Free(ps->Memory.FrameArena.GetPtr());
    Arena::Free(ps->Memory.PermanentArena.GetPtr());
}

}  // namespace kdk
