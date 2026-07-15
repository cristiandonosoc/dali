# Dali — CLAUDE.md

## Project overview

Dali is a tower defense game built on a custom C++ engine. The engine is a port and improvement of
[Kandinsky](../kandinsky), a previous engine by the same author. See [kandinsky.md](docs/kandinsky.md)
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
- Do not generate tests unless explicitly prompted.

## Code conventions

- Namespace: `kdk`
- PascalCase for types, methods, and struct members (`Arena`, `ArenaPush`, `Size`)
- `E` prefix on enums (`EArenaType`, `EEntityType`)
- `k` prefix on `static constexpr` constants (`kMaxEntities`, `kEmptyStrPtr`)
- `_` prefix on private/internal struct members (`_Str`, `_Entity`)
- `g` prefix on globals (`gRunningInTest`)
- X macros used heavily for type registration (entity types, component types, asset types)
- Arena-based allocation — prefer `Arena*` + member methods (e.g. `arena->Push`) over `new`/`delete`
- Scratch conflicts: `Arena::GetScratch()` hands out shared arenas (nested scopes stack on the same
  one). A function that allocates **results** into a parameter arena while holding a scratch MUST
  pass that parameter as a conflict — `Arena::GetScratch(arena)` — or the scratch may be that very
  arena and its scope-exit reset reclaims the results. Greppable rule: any function with an `Arena*`
  parameter and a `GetScratch` call in its body needs the parameter declared as a conflict.
- No comments unless the WHY is non-obvious
- No exceptions. The project is built without exception support, so never use `try` / `catch` /
  `throw`. Signal failure with a return value (bool, sentinel, `std::optional`). When a third-party
  library can throw (e.g. yaml-cpp), call it through its non-throwing API (e.g. yaml-cpp's
  `Node::as<T>(fallback)`) rather than wrapping it in try/catch.
- No logic in headers. Function definitions with real logic live in a `.cpp`. Headers may only hold
  templates (which have to be in a header) and trivial one-line functions (simple accessors /
  forwarders, e.g. `Add`, `Neighbour`, `operator==`).
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

### Control flow

These rules exist for one reason: **step-through debuggability**. Every branch decision and every
side effect should sit on its own line where a breakpoint can land, and evaluation order should
never be in question.

- **Always brace.** Every `if` / `for` / `while` body gets braces, even a single statement. No
  `if (x) return;` — write `if (x) { return; }` on its own lines.
- **No side effects inside a larger expression.** Never embed an increment/decrement (or any
  mutation) in a bigger expression: not `arr[cursor++ % size]`, not `id = NextId++`. Do the
  mutation as its own statement first, then use the resulting value:

  ```cpp
  int index = cursor % size;
  cursor++;
  Hex src = arr[index];
  ```

- **No multi-condition `if`s — max 2, and only when tightly related.** A compound condition hides
  which sub-expression was true. Prefer guard-clause early-returns / `continue`s (one condition
  each), or nest, or build a bool up front:

  ```cpp
  bool condition = expr1;
  condition &= expr2;
  condition &= expr3;
  if (condition) { ... }
  ```

  The only accepted 2-condition forms (each is a single idea): a **null-check guard**,
  `if (n && n->Something())` — and `if (Foo* f = ...) { }` (init-statement / nested) is preferred;
  and a **range check**, `if (x > min && x < max)`. Note `&=` does not short-circuit, so it can't
  guard a null deref or a call that's only valid when a prior condition held — use the null-check /
  nested form there and only start the `&=` chain once the value is known valid.

## Clang-format

A `.clang-format` file is in the root. Claude does not need to run it — formatting is handled
separately.

## Key types (dali/core)

| Type | Description |
|------|-------------|
| `StringView` | Non-owning string (`const char*` + size). Use `Str()` for C API calls — never returns null. Distinguished `IsValid()` (non-null ptr) vs `IsEmpty()` (zero size). |
| `FixedString<N>` | Stack-owned null-terminated string with fixed capacity. Used for names stored in structs. |
| `Arena` | Linear allocator. Member methods `Push`, `PushZero`, `PushArray`; static `Allocate`/`Free`/`GetScratch`. |
| `ScopedArena` | RAII scope that restores an arena's offset on exit (debug builds poison the reclaimed range). Get one via `arena.GetScoped()`, or from `Arena::GetScratch(conflicts...)` for a global scratch arena — nested scratch scopes share an arena and stack. Stack-frame only — never move or store it. |
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

- **`PlatformState` is globally accessible from the game DLL via `GetGlobalPlatformState()` /
  `SetGlobalPlatformState()` (`dali/game/platform_state.h`), rebound on every `OnSOLoaded` and
  cleared on `OnSOUnloaded`.** Rationale: almost everything hanging off `PlatformState` (memory
  arenas, file IO, logging) is infrastructure that's the same for the whole process — threading it
  explicitly through every leaf call bought nothing. This mirrors a pattern Kandinsky already used
  (`platform::GetPlatformContext()`/`SetPlatformContext()`). The line we're keeping: infrastructure
  on `PlatformState` is ambient; domain/gameplay state hanging off `PlatformState::GameState` (e.g.
  a future `World`) is still passed explicitly, so it stays independently testable/instantiable.
  `LogError`/`LogWarning`/`Log` (`dali/game/log.h`) are the first thing built on top of this.

## Unspecified / deferred topics

The following are intentionally left open and will be decided as the port progresses:

- Specifics of the tower defense game (mechanics, levels, art style)
- Whether to keep OpenGL or switch renderers
- Final shape of the entity/component architecture (may differ from Kandinsky)
- Thread-safety of `GetGlobalPlatformState()`/`SetGlobalPlatformState()` — currently assumes a
  single-threaded game DLL; revisit if a job system or async loading is introduced
- Any other architectural decisions that arise during porting
