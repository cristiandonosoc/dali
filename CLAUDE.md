# Dali — CLAUDE.md

## Project overview

Dali is a tower defense game built on a custom C++ engine. The engine is a port and improvement of
[Kandinsky](../kandinsky), a previous engine by the same author. See [kandinsky.md](kandinsky.md)
for a full reference of that codebase — most of what Dali needs comes from there.

The goal is to keep the code fun and interesting without being gratuitously over-engineered. That
said, it is already a custom game engine, so some complexity is inherent.

## Directory structure

```
dali/       All first-party source. Includes are self-documenting: `#include <dali/core/string.h>`.
  core/     Foundational, game-agnostic types shared by BOTH binaries: defines, memory arenas,
            StringView, containers, algorithms, math. Depends on nothing above it.
  platform/ Thin host executable — the port surface (rewrite this for a new OS/console). Owns
            SDL, window, GL context, IO, threading, and game-DLL hot-reload. Depends on dali/core.
  game/     The hot-reloadable DLL: engine systems + gameplay, all in one. Depends on dali/core
            (and the platform↔game contract header). Intended to grow much larger than the rest.
assets/     Art assets: textures, models, fonts, shaders (binary / authored files)
data/       Game data: levels, scene files, config (engine-specific formats, e.g. YAML)
third_party/ Vendored dependencies (do not modify)
```

All first-party code lives under `dali/` so every include reads `<dali/...>`. The engine/game
split from Kandinsky is intentionally collapsed: everything reloadable lives in `dali/game/`. The
only boundary kept sharp is `dali/platform/` — it is both the port surface and the hot-reload
surface (see Architecture decisions).

## Build system

Bazel. Run from the repo root.

```
bazel test //...      # build everything and run all tests
bazel build //...     # build without running tests
```

## Instructions for Claude

- After completing a task (once all changes are done, not after each individual substep), run
  `bazel test //...` to verify everything compiles and tests pass.

## Code conventions

- Namespace: `kdk`
- PascalCase for types, methods, and struct members (`Arena`, `ArenaPush`, `Size`)
- `E` prefix on enums (`EArenaType`, `EEntityType`)
- `k` prefix on `static constexpr` constants (`kMaxEntities`, `kEmptyStrPtr`)
- `_` prefix on private/internal struct members (`_Str`, `_Entity`)
- `g` prefix on globals (`gRunningInTest`)
- X macros used heavily for type registration (entity types, component types, asset types)
- Arena-based allocation — prefer `Arena*` + member methods (e.g. `arena->Push`) over `new`/`delete`
- No comments unless the WHY is non-obvious
- No anonymous namespaces. File-local helpers go in a `<name_of_file>_private` namespace instead
  (`game_library.cpp` → `game_library_private`), so the translation unit is named at the call site:

  ```cpp
  namespace game_library_private {

  } // namespace game_library_private
  ```
- `using namespace` only inside a function body, never at file/namespace scope. A TU-scope
  `using` leaks its names into whatever else shares the TU under a unity build, which we may want
  to support one day — keep the effect local to the function.

### Struct layout

We don't use classes, we use structs. Members are declared in this fixed order:

```cpp
struct Name {
    Members;                          // public data
    int _PrivateMembers = 0;          // "private" — public but `_`-prefixed by convention
    static Foo StaticFunctions();     // factory / lifecycle helpers with no `this`
    void MemberFunctions();           // operate on `this`
};
```

This is a bit unusual — most C++ style guides put methods before data, or interleave
public/private access sections. Trade-offs:

- **Pros:** data-first reads like a plain aggregate (which these structs are — no access
  control, brace-initializable); a single glance shows the memory layout before behavior; the
  `_` naming convention replaces `private:` sections, so there's one obvious ordering with no
  access-specifier bookkeeping; static vs. member split makes "constructs one" vs. "acts on one"
  visually separable.
- **Cons:** goes against reader expectations and most tooling defaults (clang-format won't enforce
  it); `_`-prefixed members are only conventionally private — the compiler won't stop external
  access; data-first means you scroll past all fields to reach the API of a large struct.

## Clang-format

A `.clang-format` file is in the root. Claude does not need to run it — formatting is handled
separately.

## Key types (dali/core)

| Type | Description |
|------|-------------|
| `StringView` | Non-owning string (`const char*` + size). Use `Str()` for C API calls — never returns null. Distinguished `IsValid()` (non-null ptr) vs `IsEmpty()` (zero size). |
| `FixedString<N>` | Stack-owned null-terminated string with fixed capacity. Used for names stored in structs. |
| `Arena` | Linear allocator. Member methods `Push`, `PushZero`, `PushArray`; static `Allocate`/`Free`/`GetScratch`. |
| `ScopedArena` | RAII scope that resets an arena's offset on exit. Get one via `arena.GetScoped()`. |
| `ScratchArena` | RAII lease over a global scratch arena, from `Arena::GetScratch()`. Marks its arena in-use for its lifetime so nested calls never collide; resets and releases it on exit. Stack-frame only — never move or store it. |
| `Array<T, N>` | Fixed-size stack array with bounds-checked access. |
| `FixedVector<T, N>` | Fixed-capacity growable array. |

## What has been ported so far

- `dali/core`: defines, memory (arenas + block arenas), StringView, containers, algorithm,
  function

## What still needs to come from Kandinsky

See [kandinsky.md](kandinsky.md) for full detail. Rough porting order (to be discussed):

- `core`: math, color, time, serde (YAML)
- Entity/component system
- Graphics (OpenGL, shaders, models, textures, lights, line batchers)
- Platform / PlatformState / window / input
- Scene management
- Asset registry + hot-reload
- Systems (schedule, enemy, camera)
- Gameplay layer (terrain, buildings, enemies, spawners, projectiles)
- App harness / game DLL hot-reload

## Architecture decisions

Decisions made during the port (kept here so they're a recorded constraint, not tribal memory):

- **GL calls go through the render layer, never sprinkled inline.** OpenGL is loaded with GLAD2
  (`gl` 4.6 Core), generated *without* MX for now — the function table is global. If we later
  enable MX (a second context, or to hoist the table into host-owned memory so it survives DLL
  reload without re-loading), every call site would need the `GladGLContext`. Keeping GL access
  funnelled through the render wrapper / uniform-setter layer means that switch — and a renderer
  swap in general — stays a one-place change. New rendering code must follow this: no raw `glXxx`
  in gameplay or engine code outside the render layer.

- **GLAD is generated from the web generator (no pip/CLI dependency).** Options: `gl` 4.6 Core,
  **alias** + **debug** on, everything else (header-only, loader, merge, mx, on-demand) off; loaded
  via `gladLoadGL(SDL_GL_GetProcAddress)`. Extensions start empty and are added in the generator as
  needed (additive — safe to regenerate), then guarded at runtime with the `GLAD_GL_<ext>` flag
  (generation-time inclusion ≠ driver support). The committed `gl.c`/`gl.h` are self-documenting,
  but regeneration overwrites them, so the reproducible recipe is the generator **permalink**, kept
  here: `<paste permalink after first generation>`.

## Unspecified / deferred topics

The following are intentionally left open and will be decided as the port progresses:

- Specifics of the tower defense game (mechanics, levels, art style)
- Whether to keep OpenGL or switch renderers
- Final shape of the entity/component architecture (may differ from Kandinsky)
- Any other architectural decisions that arise during porting
