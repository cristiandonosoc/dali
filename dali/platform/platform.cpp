#include <dali/platform/platform.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>

#include <glad/gl.h>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

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
    SDL_Log("[%s] %s", ToString(severity).Str(), message.Str());
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

void* ImguiMalloc(size_t size, void*) { return malloc(size); }
void ImguiFree(void* ptr, void*) { free(ptr); }

bool InitImGui(PlatformState* ps) {
    // Setup Dear ImGui context. Set allocators before CreateContext so the platform's context is
    // allocated with the same functions the game will free through after a reload.
    IMGUI_CHECKVERSION();
    ImGui::SetAllocatorFunctions(ImguiMalloc, ImguiFree);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    auto* platform_data = (WindowPlatformData*)ps->MainWindow.PlatformData;
    ImGui_ImplSDL3_InitForOpenGL(platform_data->SDLWindow, platform_data->GLContext);
    ImGui_ImplOpenGL3_Init(nullptr);  // Let the platform decide version.

    ps->ImGuiState.Context = ImGui::GetCurrentContext();
    ps->ImGuiState.AllocFunc = ImguiMalloc;
    ps->ImGuiState.FreeFunc = ImguiFree;

    return true;
}

void ShutdownImGui(PlatformState*) {
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

}  // namespace platform_private

bool PlatformInit(PlatformState* ps) {
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

    if (!InitImGui(ps)) {
        SDL_Log("ERROR: Init ImGui");
        return false;
    }

    return true;
}

void PlatformShutdown(PlatformState* ps) {
    using namespace platform_private;

    ShutdownImGui(ps);
    ShutdownWindow(ps);

    Arena::Free(ps->Memory.FrameArena.GetPtr());
    Arena::Free(ps->Memory.PermanentArena.GetPtr());
}

namespace platform_private {

// Returns whether a quit has been asked or not.
bool PollWindowEvents(PlatformState*) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                return true;
                // TODO(cdc): Implement other event types.
        }
    }

    return false;
}

}  // namespace platform_private

EPlatformFrameResponse PlatformBeginFrame(PlatformState* ps) {
    using namespace platform_private;

    // Update time tracking.
    {
        auto* tt = &ps->TimeTracking;
        u64 current_frame_ticks = GetCPUTicks();

        tt->TotalSeconds = (current_frame_ticks - tt->StartFrameTicks) / 1'000'000'000.0f;
        tt->TotalSeconds -= tt->PauseOffsetSeconds;

        if (tt->LastFrameTicks != 0) [[unlikely]] {
            u64 delta_ticks = current_frame_ticks - tt->LastFrameTicks;
            // Transform to seconds.
            tt->DeltaSeconds = (double)(delta_ticks) / 1'000'000'000.0f;
        }

        tt->LastFrameTicks = current_frame_ticks;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (PollWindowEvents(ps)) {
        return EPlatformFrameResponse::QuitRequested;
    }

    return EPlatformFrameResponse::Continue;
}

void PlatformEndFrame(PlatformState*) { ImGui::EndFrame(); }

// API ---------------------------------------------------------------------------------------------

u64 GetCPUTicks() { return SDL_GetTicksNS(); }

}  // namespace kdk
