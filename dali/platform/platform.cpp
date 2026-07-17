#include <SDL3/SDL_video.h>
#include <dali/platform/platform.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>

#include <glad/gl.h>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <nfd.hpp>

namespace kdk {

namespace platform_private {

// Owns the OS/GL handles behind Window::PlatformData. Kept out of dali/core/api.h since it's a
// platform-only concern (SDL types can't cross the platform<->game contract).
struct WindowPlatformData {
    SDL_Window* SDLWindow = nullptr;
    SDL_GLContext GLContext = nullptr;
};

void Log(ELogSeverity severity, StringView message) {
    SDL_Log("[%s] %s", ToString(severity).Str(), message.Str());
}

// Named with a Platform prefix so they never collide with the WIN32 ::ReadFile / ::WriteFile that
// <windows.h> (pulled in transitively) declares.
FileContents PlatformReadFile(Arena* arena, StringView path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.Str(), &size);
    if (!data) {
        return {};
    }
    // +1 for a convenience null terminator (text callers read Data as a C string); Data's size
    // excludes it.
    std::span<u8> buffer = arena->Push(size + 1);
    std::memcpy(buffer.data(), data, size);
    buffer[size] = 0;
    SDL_free(data);
    return FileContents{.Data = std::span<u8>(buffer.data(), size)};
}

bool PlatformWriteFile(StringView path, std::span<const u8> data) {
    auto scratch = Arena::GetScratch();
    StringView dir = paths::GetDirname(scratch, path);
    if (!dir.IsEmpty()) {
        SDL_CreateDirectory(dir.Str());  // makes intermediate directories; no-op if it exists
    }
    return SDL_SaveFile(path.Str(), data.data(), data.size_bytes());
}

bool OpenFileDialog(Arena* arena, StringView* out_path, std::span<const FileDialogFilter> filters) {
    // Map the neutral filters to NFD's item type on the stack (16 is plenty for a dialog).
    nfdfilteritem_t items[16] = {};
    u32 count = 0;
    for (const FileDialogFilter& filter : filters) {
        if (count >= 16) {
            break;
        }
        items[count].name = filter.Name;
        items[count].spec = filter.Extensions;
        count++;
    }

    NFD::UniquePath nfd_path;
    nfdresult_t result = NFD::OpenDialog(nfd_path, items, count);
    if (result != NFD_OKAY) {
        if (result == NFD_ERROR) {
            SDL_Log("ERROR: file dialog: %s", NFD::GetError());
        }
        return false;  // NFD_CANCEL lands here too, silently.
    }

    *out_path = InternStringToArena(arena, nfd_path.get());
    return true;
}

bool OpenFolderDialog(Arena* arena, StringView* out_path) {
    NFD::UniquePath nfd_path;
    nfdresult_t result = NFD::PickFolder(nfd_path);
    if (result != NFD_OKAY) {
        if (result == NFD_ERROR) {
            SDL_Log("ERROR: folder dialog: %s", NFD::GetError());
        }
        return false;  // NFD_CANCEL lands here too, silently.
    }

    *out_path = InternStringToArena(arena, nfd_path.get());
    return true;
}

void OpenContainingFolder(StringView path) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    // If |path| is a file, target its containing directory. If it doesn't exist (a mistyped source,
    // say), fall back to the dirname too; a live directory is used as-is.
    StringView target = path;
    SDL_PathInfo info = {};
    bool is_dir = SDL_GetPathInfo(path.Str(), &info);
    is_dir &= (info.type == SDL_PATHTYPE_DIRECTORY);
    if (!is_dir) {
        target = paths::GetDirname(arena, path);
    }

    // The file manager wants a file:// URL with forward slashes (SDL_OpenURL yields '\' paths on
    // Windows otherwise).
    char* buffer = (char*)arena->Push(target.Size + 1).data();
    for (u64 i = 0; i < target.Size; ++i) {
        char c = target[i];
        buffer[i] = (c == '\\') ? '/' : c;
    }
    buffer[target.Size] = '\0';

    StringView url = Printf(arena, "file:///%s", buffer);
    if (!SDL_OpenURL(url.Str())) {
        SDL_Log("ERROR: opening folder '%s': %s", url.Str(), SDL_GetError());
    }
}

bool GetFileModTime(StringView path, i64* out_ns, DateTime* out_datetime, bool datetime_local) {
    SDL_PathInfo info = {};
    if (!SDL_GetPathInfo(path.Str(), &info)) {
        return false;
    }
    if (out_ns) {
        *out_ns = (i64)info.modify_time;  // SDL_Time is nanoseconds since the Unix epoch
    }
    if (out_datetime) {
        SDL_DateTime dt = {};
        if (!SDL_TimeToDateTime(info.modify_time, &dt, datetime_local)) {
            return false;
        }
        out_datetime->Year = dt.year;
        out_datetime->Month = dt.month;
        out_datetime->Day = dt.day;
        out_datetime->Hour = dt.hour;
        out_datetime->Minute = dt.minute;
        out_datetime->Second = dt.second;
        out_datetime->UtcOffsetSeconds = dt.utc_offset;
    }
    return true;
}

ProcessResult RunProcess(Arena* arena, std::span<const StringView> args) {
    ProcessResult result = {};
    if (args.empty()) {
        return result;
    }

    // |arena| receives Stdout below, so it must not also be the scratch.
    auto scratch = Arena::GetScratch(arena);
    Arena* scratch_arena = scratch;

    // SDL wants a NULL-terminated argv of C strings. A StringView may be a non-terminated substring,
    // so each arg is interned (copied + null-terminated) into scratch.
    std::span<const char*> argv = scratch_arena->PushArray<const char*>(args.size() + 1);
    for (u64 i = 0; i < args.size(); ++i) {
        argv[i] = InternStringToArena(scratch_arena, args[i]).Str();
    }
    argv[args.size()] = nullptr;

    SDL_Process* process = SDL_CreateProcess(argv.data(), true);  // pipe stdio so we can read stdout
    if (!process) {
        SDL_Log("ERROR: RunProcess '%s': %s", argv[0], SDL_GetError());
        return result;  // Launched stays false
    }
    result.Launched = true;

    // Blocks until the process exits, returning all of stdout in an SDL-owned buffer.
    size_t out_size = 0;
    int exit_code = 0;
    void* out = SDL_ReadProcess(process, &out_size, &exit_code);
    result.ExitCode = exit_code;
    if (out) {
        std::span<u8> buffer = arena->Push(out_size + 1);
        std::memcpy(buffer.data(), out, out_size);
        buffer[out_size] = 0;  // convenience null terminator; not counted in the size
        result.Stdout = StringView((const char*)buffer.data(), out_size);
        SDL_free(out);
    }
    SDL_DestroyProcess(process);
    return result;
}

bool InitWindow(PlatformState* ps, StringView window_name, int window_width, int window_height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("ERROR: Initializing SDL: %s\n", SDL_GetError());
        return false;
    }

    SDL_Window* sdl_window =
        SDL_CreateWindow(window_name.Str(), window_width, window_height, SDL_WINDOW_OPENGL);
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
    ps->MainWindow.Name = window_name;
    ps->MainWindow.Width = window_width;
    ps->MainWindow.Height = window_height;
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

bool PlatformInit(PlatformState* ps, const PlatformInitConfig& config) {
    using namespace platform_private;

    ps->API.Log = Log;
    ps->API.ReadFile = PlatformReadFile;
    ps->API.WriteFile = PlatformWriteFile;
    ps->API.GLGetProcAddress = (decltype(ps->API.GLGetProcAddress))SDL_GL_GetProcAddress;
    ps->API.OpenFileDialog = OpenFileDialog;
    ps->API.OpenFolderDialog = OpenFolderDialog;
    ps->API.OpenContainingFolder = OpenContainingFolder;
    ps->API.GetFileModTime = GetFileModTime;
    ps->API.RunProcess = RunProcess;

    auto scratch = Arena::GetScratch();
    ps->BasePath = paths::GetBaseDir(scratch);
    SDL_Log("Running from: %s", ps->BasePath.Str());

    ps->Memory.PermanentArena = Arena::Allocate("PermanentArena"sv, 100 * MEGABYTE);
    ps->Memory.FrameArena = Arena::Allocate("FrameArena"sv, 50 * MEGABYTE);

    if (!InitWindow(ps, config.WindowName, config.WindowWidth, config.WindowHeight)) {
        SDL_Log("ERROR: Initializing window");
        return false;
    }

    if (!InitImGui(ps)) {
        SDL_Log("ERROR: Init ImGui");
        return false;
    }

    // Native file dialogs (asset importing). Non-fatal if it fails: OpenFileDialog just returns
    // false and the user can still type paths by hand.
    if (NFD::Init() != NFD_OKAY) {
        SDL_Log("WARNING: NFD init failed: %s", NFD::GetError());
    }

    return true;
}

void PlatformShutdown(PlatformState* ps) {
    using namespace platform_private;

    NFD::Quit();
    ShutdownImGui(ps);
    ShutdownWindow(ps);

    Arena::Free(ps->Memory.FrameArena.GetPtr());
    Arena::Free(ps->Memory.PermanentArena.GetPtr());
}

namespace platform_private {

// EKey/EMouseButton mirror the SDL codes 1:1, so the poll loop below indexes the input bitsets with
// the raw SDL values and needs no translation table. Verify that mirroring here — this is the one
// TU that sees both dali/core/input.h and <SDL3/SDL.h>. If any of these fire, the enum in input.h
// has drifted from SDL and must be fixed.
static_assert(kKeyCount == SDL_SCANCODE_COUNT);
static_assert((u32)EKey::W == SDL_SCANCODE_W);
static_assert((u32)EKey::Space == SDL_SCANCODE_SPACE);
static_assert((u32)EKey::Slash == SDL_SCANCODE_SLASH);
static_assert((u32)EKey::Escape == SDL_SCANCODE_ESCAPE);
static_assert((u32)EKey::LCtrl == SDL_SCANCODE_LCTRL);
static_assert((u32)EKey::Up == SDL_SCANCODE_UP);
static_assert((u32)EMouseButton::Left == SDL_BUTTON_LEFT);
static_assert((u32)EMouseButton::Right == SDL_BUTTON_RIGHT);
static_assert((u32)EMouseButton::X2 == SDL_BUTTON_X2);

// Returns whether a quit has been asked or not. Also drives InputState for the frame: per-frame
// edges (pressed/released, scroll, move) are cleared up front, held level (down) persists.
bool PollWindowEvents(PlatformState* ps) {
    auto* input = &ps->Input;

    input->KeyPressed.reset();
    input->KeyReleased.reset();
    input->MousePressed.reset();
    input->MouseReleased.reset();
    input->MouseScroll = {};

    bool got_motion = false;
    bool quit = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type) {
            case SDL_EVENT_QUIT: quit = true; break;

            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat) {
                    input->KeyPressed[event.key.scancode] = true;
                    input->KeyDown[event.key.scancode] = true;
                }
                break;
            case SDL_EVENT_KEY_UP:
                input->KeyReleased[event.key.scancode] = true;
                input->KeyDown[event.key.scancode] = false;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                input->MousePressed[event.button.button] = true;
                input->MouseDown[event.button.button] = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                input->MouseReleased[event.button.button] = true;
                input->MouseDown[event.button.button] = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                got_motion = true;
                input->MousePosition = {event.motion.x, event.motion.y};
                input->MousePositionGL = {event.motion.x, ps->MainWindow.Height - event.motion.y};
                input->MouseMove = {event.motion.xrel, event.motion.yrel};
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                input->MouseScroll.x += event.wheel.x;
                input->MouseScroll.y += event.wheel.y;
                break;

                // TODO(cdc): Window resize, focus, text input, etc.
        }
    }

    if (!got_motion) {
        input->MouseMove = {};
    }

    // ImGui::NewFrame() already ran this frame (PlatformBeginFrame), so WantCapture* is valid.
    ImGuiIO& io = ImGui::GetIO();
    input->KeyboardOverride = io.WantCaptureKeyboard;
    input->MouseOverride = io.WantCaptureMouse;

    return quit;
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

void PlatformEndFrame(PlatformState* ps) {
    using namespace platform_private;

    auto* platform_data = (WindowPlatformData*)ps->MainWindow.PlatformData;

    // Finalizes this frame's ImGui draw data (calls ImGui::EndFrame internally).
    ImGui::Render();

    // TODO(cdc): Clear/viewport belong in the render layer once it exists — raw GL here is
    // bootstrapping so ImGui has a defined backbuffer to draw onto. See CLAUDE.md render-layer
    // rule.
    glViewport(0, 0, ps->MainWindow.Width, ps->MainWindow.Height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // The game's OnGameRender will eventually draw the scene here, before ImGui composites on top.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(platform_data->SDLWindow);
}

// API ---------------------------------------------------------------------------------------------

u64 GetCPUTicks() { return SDL_GetTicksNS(); }

}  // namespace kdk
