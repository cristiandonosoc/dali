#include <SDL3/SDL_timer.h>
#include <dali/core/defines.h>
#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/platform/arg_parser.h>
#include <dali/platform/game_library.h>
#include <dali/platform/platform.h>

#include <SDL3/SDL.h>

namespace kdk::main_private {

PlatformState* gPlatformState = nullptr;

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

    if (!PlatformInit(gPlatformState)) {
        return -1;
    }


    GameLibrary gl = GameLibrary::Create(so_path);
    if (!gl.Load(gPlatformState, false)) {
        printf("ERROR: Loading library");
        return -1;
    }

    // Verify the load-and-call flow: init once, then a few update/render ticks, then unload.
    gl.SO.OnGameInit(gPlatformState);

	while (true) {
		DEFER { PlatformEndFrame(gPlatformState); };

		gPlatformState->FrameNumber++;

        // Hot reloading.
        gl.MaybeReload(gPlatformState);

        EPlatformFrameResponse response = PlatformBeginFrame(gPlatformState);
		if (response == EPlatformFrameResponse::QuitRequested) {
			SDL_Log("Quit Requested!");
			break;
		}

        gl.SO.OnGameUpdate(gPlatformState);
        gl.SO.OnGameRender(gPlatformState);
    }

    gl.Unload(gPlatformState);
    PlatformShutdown(gPlatformState);

    free(gPlatformState);
    return 0;
}
