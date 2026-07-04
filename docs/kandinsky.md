# Kandinsky Reference

Kandinsky is the predecessor engine at `../kandinsky`. Dali is a port and improvement of it.
This document is a reference for what exists there, so we know what to bring over and how it works.

Kandinsky namespace is `kdk` (same as Dali). The codebase lives under `kandinsky/kandinsky/`.

---

## Core (`kandinsky/core/`)

### defines.h / defines.cpp
- Integer type aliases: `i8/u8`, `i16/u16`, `i32/u32`, `i64/u64`, `f32/f64`, `b8`, `b32`
- `NONE = -1` sentinel
- `ASSERT(expr)` / `ASSERTF(expr, fmt, ...)` — traps with backtrace via `DoAssert`
- `DEBUG_BREAK()` — platform debug break
- `DEFER { ... }` — scope-exit cleanup (RAII lambda)
- `ResetStruct(ptr)` — zero-fills a struct
- `KDK_BUILD_DEBUG` — debug build flag
- Windows-only for now

### memory.h / memory.cpp
- `Arena` — linear allocator. `FixedSize` (traps) or `Extendable` (chains new blocks).
- `ArenaPush`, `ArenaPushZero`, `ArenaPushArray`, `ArenaPushArrayZero`, `ArenaPushInit`, `ArenaCopy`
- `AllocateArena(name, size)`, `FreeArena`, `CarveArena`
- `ArenaReset` — resets offset (frees Extendable chain links)
- `ScopedArena` — RAII that resets offset on scope exit
- `GetScratchArena(conflict1, conflict2)` — returns one of 4 thread-local scratch arenas that don't conflict with the given arenas
- `BlockArena<BLOCK_SIZE, BLOCK_COUNT>` — pool allocator for fixed-size blocks
- `BlockArenaManager` — manages multiple BlockArena sizes (1KB, 4KB, 16KB in Kandinsky)
- `ToMemoryString(arena, bytes)` — human-readable size string

### string.h / string.cpp
- `String` (non-owning view, `const char*` + `u64 Size`). **In Dali this is `StringView`.**
- `FixedString<N>` — stack-owned, null-terminated, truncates on overflow
- `InternStringToArena(arena, str)` — copies string into arena, returns view
- `Concat`, `RemovePrefix`, `Printf(arena, fmt, ...)`, `ToString(arena, source_location)`
- `PrintBacktrace(arena, frames_to_skip)` — Windows DbgHelp stack trace
- `CompileHash(str)` — constexpr djb2 hash; `""_hash` literal operator
- `IDFromString(str)` — hash + 1 (0 = none)
- `paths::` — `IsAbsolute`, `GetBaseDir`, `GetDirname`, `GetBasename`, `GetExtension`,
  `RemoveExtension`, `ChangeExtension`, `PathJoin(arena, a, b, ...)`, `ListDir`, `CleanPathFromBazel`
- `GetEnv(arena, var)` — read environment variable

### container.h
- `Array<T, N>` — fixed-size stack array, bounds-checked `operator[]`, `.DataPtr()`, `.ToSpan()`
- `FixedVector<T, N>` — fixed-capacity growable array with `Push`, `Pop`, `At`, `IsEmpty`, `Clear`
- `AllocatedVector<T>` — growable array backed by an arena (`Reserve`, `Push`, etc.)
- `DynArray<T>` — arena-backed dynamic array with explicit `SetArena`
- `Optional<T>` — move-only optional (no copy), `HasValue()`, `GetValue()`
- `Iterator<T>` — range iterator

### algorithm.h
Custom algorithms (contains, find, sort utilities over the above containers).

### math.h / math.cpp
- GLM-based: `Vec2/3/4`, `UVec2/3/4`, `IVec2`, `Mat4`, `Quat`
- `Transform` struct: `Position (Vec3)`, `Rotation (Quat)`, `Scale (Vec3)`
- `SQUARE(x)` macro
- Ray casting, frustum, grid coord helpers, `GetModelMatrix(Transform)`

### color.h / color.cpp
- `Color32` — RGBA u8 packed color with named constants (`Cyan`, `Red`, etc.)
- Conversions between `Color32` and `Vec4`

### time.h / time.cpp
- `SDL_Time`-based timing helpers

### serde.h / serde.cpp
- YAML-based serialization/deserialization using `yaml-cpp`
- `SerdeArchive` — holds backend, mode (Serialize/Deserialize), arenas, current YAML node, errors
- `SERDE(sa, owner, member)` macro — serialize a named member
- `Serde(sa, name, ptr)` — dispatch to backend
- Specializations for all primitive types, enums, `Vec2/3/4`, `Quat`, `Transform`, `Color32`,
  `String`, `FixedString<N>`, `Array<T,N>`, `FixedVector<T,N>`, `DynArray<T>`
- `GetSerializedString(arena, sa)` — emit YAML to string

### function.h
- `Function<R(Args...)>` — custom callable type; avoids `std::function` heap allocation for small
  captures. Used everywhere callbacks are needed.

### intrin.h
- SIMD / compiler intrinsic helpers (minimal)

---

## Entity System (`kandinsky/entity.h`, `entity_manager.h`, `entity_manager.cpp`)

### Core concepts
- `EntityID` — packed `i32`: 16-bit index, 8-bit generation, 8-bit `EEntityType`
- `Entity` — base struct: `EntityID`, `EntityFlags` (bitfield), `FixedString<128> Name`, `Transform`
- `EntitySignature` — `i32` bitmask of active components (bit per `EEntityComponentType`)
- `kMaxEntities = 16384`, `kMaxComponentTypes = 31`

### Registration via X macros
Entity types in `ENTITY_TYPES(X)`: `Player`, `Spawner`, `Enemy`, `Building`, `Projectile`, `Test`

Component types in `COMPONENT_TYPES(X)`: `StaticModel`, `PointLight`, `DirectionalLight`,
`Spotlight`, `Health`, `Billboard`, `Test`, `Test2`

### `GENERATE_ENTITY(ENUM_NAME)` macro
Injects into a typed entity struct: `kEntityName`, `kEntityType`, `_Entity*`, `GetEntityID()`,
`GetEntity()`, `GetTransform()`, `GetModelMatrix()`, `Archetype*`

### `GENERATE_COMPONENT(component_name)` macro
Injects: `kComponentName`, `kComponentType`, `_OwnerID`, `_ComponentIndex`, `GetOwner()`

### EntityManager
- `EntityData`: parallel arrays — `Generations`, `Signatures`, `Entities`, `EntityTypeWrappers`,
  `ModelMatrices`, plus per-type `FixedVector<EntityID> Entity_X_Alive`
- Per-component-type `EntityComponentHolder<T>` — maps entity↔component index, stores component array
- API: `CreateEntity<T>`, `DestroyEntity`, `CloneEntity`, `GetTypedEntity<T>`
- `AddComponent<T>`, `RemoveComponent<T>`, `GetComponent<T>`, `HasComponent<T>`
- `VisitEntities<T>`, `VisitComponents<T>`, `VisitAllEntities`
- `CalculateModelMatrices` — updates `Mat4` for all entities

### Archetypes
- Entities used as templates for creating other entities
- `InitArchetypes`, `SaveArchetypes`, `LoadArchetypes` — YAML serde
- `ArchetypeID` wraps `EntityID` to prevent aliasing

### Validation
- `ValidationError { Message, Position, EntityID }`
- `ValidateEntity`, `ValidateScene`

### ImGui integration
- `BuildEntityListImGui`, `BuildEntityDebuggerImGui`, `BuildImGui(EntityID)`, `BuildGizmos`
- `BuildEntityImGuiContext` tracks selected entity/component in the editor

---

## Graphics (`kandinsky/graphics/`)

### opengl.h / opengl.cpp
- `InitOpenGL(ps)` / `ShutdownOpenGL(ps)`
- `LineBatcher` — GPU-backed line renderer for debug drawing (`Name`, VAO/VBO, batch list)
- `LineBatcherRegistry` — named collection of LineBatchers
- `DrawGrid(rs)` — debug grid overlay

### shader.h / shader.cpp
- `Shader { ID, Path, LastLoadTime, Program (GLuint) }`
- `CreateShader(assets, path)` — loads GLSL from disk
- `ReevaluateShaders(assets)` — hot-reload: checks `SHADER_MARKER` file timestamp
- Uniform setters: `SetBool`, `SetI32`, `SetFloat`, `SetVec2/3/4`, `SetMat4`, etc.

### model.h / model.cpp
- `Mesh`, `Model` (collection of meshes + materials), `Material`, `Texture`
- Loaded via Assimp
- `StaticModelComponent { MeshHandle, MaterialHandle }` — component attaching a model to an entity

### texture.h / texture.cpp
- `Texture { Width, Height, Channels, TextureID (GLuint) }`
- `CreateTexture(assets, path)` — stb_image load

### light.h / light.cpp
- `Light` — point, directional, spotlight variants
- `PointLightComponent`, `DirectionalLightComponent`, `SpotlightComponent`

### render_state.h / render_state.cpp
- `RenderState` — camera matrices (`M_View`, `M_Proj`, `M_ViewProj`, `M_Model`, etc.),
  light span, seconds, mouse pos, current entity ID
- `SetCamera`, `SetLights`, `ChangeModelMatrix`, `SetBaseUniforms`, `SetUniforms`
- `EntityPicker` — SSBO-based GPU entity picking by pixel

### font.h / font.cpp
- `Font` (FreeType / stb-based), `TextRenderer`
- Screen-space text rendering

---

## Platform (`kandinsky/platform.h`, `platform.cpp`)

### PlatformState
The central "god object" passed everywhere. Contains:
- `BasePath`, `ArchetypesFilePath`
- `Window`, `InputState`
- `EditorTimeTracking`, `RuntimeTimeTracking`, `CurrentTimeTracking*`
- `GameMode`, `SystemManager`
- `EditorCamera`, `GameCamera`, `DebugCamera`, `CurrentCamera*`
- `ImGuiState`, `EditorState`
- `Memory { PermanentArena, StringArena, AssetLoadingArena, FrameArena, BlockArenaManager }`
- `GameLibrary { Path, LoadedLibrary, ... }` — hot-reload state
- `ShaderLoading` — shader hot-reload timing
- `LineBatchers`, `DebugLineBatcher`
- `EditorScene`, `GameplayScene`, `CurrentScene*`
- `Archetypes` EntityManager
- `EntityManager*` (points into current scene)
- `SelectedEntityID`, `HoverEntityID`, `SelectedArchetypeID`, `EntityPicker`
- `Assets` (AssetRegistry)
- `Rendering { GlobalVAO, TextRenderer }`
- `RenderState`
- `GameState` (void* for game DLL)

### Modes
- `ERunningMode`: Editor, GameRunning, GamePaused, GameEndRequested
- `EEditorMode`: Selection, Terrain
- `StartPlay`, `PausePlay`, `ResumePlay`, `EndPlay`

### Namespaced helpers (`platform::`)
- `GetCPUTicks()`, `GetPlatformContext()`, `SetPlatformContext(ps)`
- `GetFrameArena()`, `GetPermanentArena()`, `GetStringArena()`
- `InternToStringArena(str)` — intern into the permanent string arena

### TimeTracking
- `DeltaSeconds`, `TotalSeconds`, `PauseOffsetSeconds`

---

## Window (`kandinsky/window.h`)
SDL3-based window + OpenGL context creation. `Window { SDLWindow*, GLContext }`.

## Input (`kandinsky/input.h`)
`InputState` — keyboard and mouse state, SDL event polling.

---

## Scene (`kandinsky/scene.h`, `scene.cpp`)

- `Scene { Name, Path, ESceneType, EntityManager, Terrain, ValidationErrors, ... }`
- `ESceneType`: Editor, Game
- `InitScene`, `StartScene`, `CloneScene`
- `LoadScene(ps, path)` / `SaveScene(ps, scene, path)` — YAML serde
- `ValidateScene` — collects `ValidationError` for all entities

---

## Assets (`kandinsky/asset.h`, `asset_registry.h/cpp`)

### Asset types (registered via `ASSET_TYPES(X)`)
`Mesh`, `Model`, `Texture`, `Material`, `Shader`, `Font`

### AssetHandle
Packed `i32`: 8-bit type + 24-bit index. Typed variants (`MeshAssetHandle`, etc.) prevent
cross-type aliasing.

### AssetRegistry
- Holds per-type asset arrays (behind `AssetRegistry` opaque struct)
- `FindAssetHandle(assets, type, path)` — lookup by path
- `FindXxxAsset(assets, handle)` — typed getter
- `CreateShader`, `CreateTexture`, `CreateModel`, etc. — loading entry points
- `ImGui_XxxAssetHandle(registry, label, handle)` — editor picker widgets

---

## Systems (`kandinsky/systems/`)

- `ESystemType`: Schedule, Enemy, Camera
- `GENERATE_SYSTEM(ENUM_NAME)` — injects type constants and platform state backpointer
- `SystemManager` — holds all system instances
- `GetSystem<T>(sm)` — typed accessor

### ScheduleSystem
Deferred callbacks: schedule work to run at a future tick/time.

### EnemySystem
Update loop for enemies: movement via flow field, attack logic.

### CameraSystem
Camera follow / editor camera control.

---

## Gameplay (`kandinsky/gameplay/`)

### terrain.h / terrain.cpp
- `Terrain { Tiles[32x32], FlowField[32x32], PlacedEntities[32x32] }`
- `ETerrainTileType`: None, Grass, Path
- `CalculateFlowField(ps, terrain, target_pos)` — BFS flow field toward target
- `DebugDrawFlowField`, `PlaceEntity`, `GetPlacedEntity`
- `Render(ps, terrain)` — tile rendering
- ImGui terrain editor integration

### building.h — `BuildingEntity { GENERATE_ENTITY(Building), ... }`
Tower/building entities placed on the terrain grid.

### enemy.h — `EnemyEntity { GENERATE_ENTITY(Enemy), ... }`
Enemies that follow the flow field toward a target.

### spawner.h — `SpawnerEntity { GENERATE_ENTITY(Spawner), ... }`
Triggers enemy wave spawns.

### projectile.h — `ProjectileEntity { GENERATE_ENTITY(Projectile), ... }`
Fired by buildings at enemies.

### player.h — `PlayerEntity { GENERATE_ENTITY(Player), ... }`
Player-controlled entity (in Kandinsky this is minimal).

### health_component.h — `HealthComponent { GENERATE_COMPONENT(Health), HP, MaxHP }`

### gamemode.h — `GameMode`
Top-level game state machine (waves, score, win/lose conditions).

### ui.h / ui.cpp
In-game HUD rendering.

---

## App Harness (`kandinsky/app_harness.cpp`, `main.cpp`)

- `main.cpp` — entry point, SDL init, main loop
- `app_harness.cpp` — game DLL hot-reload:
  - Watches the game `.dll` file timestamp
  - On change: calls `OnSharedObjectUnloaded`, reloads DLL, calls `OnSharedObjectLoaded`
  - Game DLL exports `__KDKEntryPoint_GameInit`, `__KDKEntryPoint_GameUpdate`,
    `__KDKEntryPoint_GameRender`
  - Allows iterating on game code without restarting the process

---

## ImGui integration (`kandinsky/imgui.h`, `imgui.cpp`, `imgui_widgets.h`)
- ImGui + ImGuizmo integration
- `imgui_widgets.h` — engine-specific widgets (asset pickers, entity inspectors)
- Debug windows: memory, timings, entity list, entity debugger, archetypes, terrain, camera,
  input, schedule

---

## Camera (`kandinsky/camera.h`, `camera.cpp`)
- `Camera { Position, Yaw, Pitch, FOV, Near, Far, ... }`
- Editor camera: WASD + mouse look
- Game camera: follow target

---

## Build structure (Bazel)
- `kandinsky/BUILD.bazel` — main engine library
- `kandinsky/core/BUILD.bazel`, `graphics/BUILD.bazel`, `systems/BUILD.bazel`,
  `gameplay/BUILD.bazel`, `utils/BUILD.bazel`
- `apps/tower_defense/` — the game app (separate from the engine library)
- `apps/` — other test/experiment apps

---

## Notable patterns

**X macros** — used everywhere to register types without repetition. A single `ENTITY_TYPES(X)` or
`COMPONENT_TYPES(X)` macro drives enum generation, array allocation, visitor dispatch, serde, and
ImGui all from one list.

**Arena discipline** — nearly all allocations go through an `Arena*`. Permanent data goes into
`PermanentArena` or `StringArena`. Per-frame temp work uses `GetScratchArena()`. No `new`/`delete`
in game/engine code.

**No-throw, no-exception** — `ASSERT` traps and aborts. Errors are returned as values or collected
into error lists (serde errors, validation errors).

**Hot-reload** — both game DLL and shaders can be reloaded without restarting. `PlatformState` is
passed by pointer so the DLL always has a current reference.

**Serde = Serialize + Deserialize in one pass** — `Serialize(sa, ptr)` functions handle both
directions based on `sa->Mode`. `SERDE(sa, owner, member)` expands to one call that works both ways.
