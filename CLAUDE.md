# Dali — CLAUDE.md

## Project overview

Dali is a tower defense game built on a custom C++ engine. The engine is a port and improvement of
[Kandinsky](../kandinsky), a previous engine by the same author. See [kandinsky.md](kandinsky.md)
for a full reference of that codebase — most of what Dali needs comes from there.

The goal is to keep the code fun and interesting without being gratuitously over-engineered. That
said, it is already a custom game engine, so some complexity is inherent.

## Directory structure

```
engine/     Engine code, game-agnostic where possible (memory, strings, math, rendering, etc.)
  core/     Foundational types: defines, memory arenas, StringView, containers, algorithms
game/       Game-specific code. Intended to grow much larger than engine/
assets/     Art assets: textures, models, fonts, shaders (binary / authored files)
data/       Game data: levels, scene files, config (engine-specific formats, e.g. YAML)
third_party/ Vendored dependencies (do not modify)
```

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
- Arena-based allocation — prefer `Arena*` + `ArenaPush` over `new`/`delete`
- No comments unless the WHY is non-obvious

## Clang-format

A `.clang-format` file is in the root. Claude does not need to run it — formatting is handled
separately.

## Key types (engine/core)

| Type | Description |
|------|-------------|
| `StringView` | Non-owning string (`const char*` + size). Use `Str()` for C API calls — never returns null. Distinguished `IsValid()` (non-null ptr) vs `IsEmpty()` (zero size). |
| `FixedString<N>` | Stack-owned null-terminated string with fixed capacity. Used for names stored in structs. |
| `Arena` | Linear allocator. `ArenaPush`, `ArenaPushZero`, `ArenaPushArray`. Two types: `FixedSize` (traps on overflow) and `Extendable` (chains new blocks). |
| `ScopedArena` | RAII scope that resets an arena on exit. Get one via `GetScratchArena()`. |
| `Array<T, N>` | Fixed-size stack array with bounds-checked access. |
| `FixedVector<T, N>` | Fixed-capacity growable array. |

## What has been ported so far

- `engine/core`: defines, memory (arenas + block arenas), StringView, containers, algorithm,
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

## Unspecified / deferred topics

The following are intentionally left open and will be decided as the port progresses:

- Specifics of the tower defense game (mechanics, levels, art style)
- Whether to keep OpenGL or switch renderers
- Final shape of the entity/component architecture (may differ from Kandinsky)
- Any other architectural decisions that arise during porting
