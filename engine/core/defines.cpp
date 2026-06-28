#include <engine/core/defines.h>

#include <engine/core/memory.h>
#include <engine/core/string.h>

#include <stb/stb_sprintf.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool gRunningInTest = false;

namespace kdk {
namespace internal {

namespace defines_private {

struct RunningInTest_Setter {
    RunningInTest_Setter() {
        const char* env = nullptr;
        char buf[1024];
        size_t required_size;
        if (errno_t err = getenv_s(&required_size, buf, 1024, env) != 0) {
            gRunningInTest = false;
        } else {
            if (required_size == 0) {
                gRunningInTest = false;
            } else {
                gRunningInTest = true;
            }
        }
    }
};
RunningInTest_Setter gRunningInTest_Setter;

void DoAssert(Arena* arena,
              const char* expr,
              const char* filename,
              u32 lineno,
              const char* function,
              const char* message) {
    StringView path = paths::CleanPathFromBazel(StringView(filename));
    fprintf(stderr, "***ASSERT FAILED *********************************************************\n");
    fprintf(stderr, "Expr: %s\n", expr);
    fprintf(stderr, "At: %s (%s:%d)\n", function, path.Str(), lineno);
    if (message) {
        fprintf(stderr, "Message: %s\n", message);
    }

    PrintBacktrace(arena, 2);
    std::abort();
}

}  // namespace defines_private

void HandleAssert(const char* expr, const char* filename, u32 lineno, const char* function) {
    // Create an arena on the fly (the program is dying anyway).
    Arena arena = AllocateArena("AssertArena"sv, 64 * KILOBYTE);
    defines_private::DoAssert(&arena, expr, filename, lineno, function, nullptr);
}

void HandleAssertf(const char* expr,
                   const char* filename,
                   u32 lineno,
                   const char* function,
                   const char* fmt,
                   ...) {
    // Create an arena on the fly (the program is dying anyway).
    Arena arena = AllocateArena("AssertfArena"sv, 64 * KILOBYTE);

    auto buffer = ArenaPush(&arena, 2 * STB_SPRINTF_MIN);
    char* message = (char*)buffer.data();
    va_list va;
    va_start(va, fmt);
    stbsp_vsprintf(message, fmt, va);
    va_end(va);

    defines_private::DoAssert(&arena, expr, filename, lineno, function, message);
}

}  // namespace internal
}  // namespace kdk
