#include <dali/platform/arg_parser.h>
#include <dali/platform/game_library.h>

#include <dali/core/filesystem.h>

namespace kdk::main_private {

PlatformState* gPlatformState = nullptr;

void InitPlatform(PlatformState* ps) { (void)ps; }

void ShutdownPlatform(PlatformState* ps) { (void)ps; }

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

    // Verify the load-and-call flow: init once, then a few update/render ticks, then unload.
    gl.SO.OnGameInit(gPlatformState);
    for (int i = 0; i < 3; ++i) {
        gl.SO.OnGameUpdate(gPlatformState);
        gl.SO.OnGameRender(gPlatformState);
    }
    gl.Unload(gPlatformState);

    free(gPlatformState);
    return 0;
}
