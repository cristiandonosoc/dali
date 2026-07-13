# Dali — Asset System

Status: **texture + spritesheet + enemy implemented.** Textures import raw art into a baked payload;
spritesheets are concept-level animation assets composed from textures; enemies are data-defined
gameplay blueprints. Meshes/materials are designed for but not built.

## Why

Raw art (`.png`, models, …) needs *processing* before the engine can use it — a PNG must be decoded
to pixels and uploaded to the GPU; a mesh must be parsed (Assimp) into vertex buffers. Doing that at
load time is slow and drags the importer libraries (stb, Assimp) into the shipping game binary.

So we split **source** from **baked**:

- `raw/` — source art. Referenced only by the importer, never by the game at runtime.
- `assets/` — processed, ready-to-use assets. This is all the game loads.

The game crawls `assets/` once at startup and never re-processes. Re-importing is an explicit action.

## On-disk shape

Every asset is a **manifest** plus an optional **payload**, sharing a stem:

```
assets/textures/goblin/walk.yml     # manifest — always present
assets/textures/goblin/walk.asset   # payload  — optional (textures/meshes have one)
assets/spritesheets/goblin.yml      # composing assets are yml-only (no payload)
assets/enemies/goblin.yml           # blueprints are yml-only too
```

- The **`.yml` manifest** is human-readable and authoritative: it describes the asset (type,
  version, dims, import settings) and how to interpret the payload. The crawler keys on `.yml`; the
  `.asset` is read only if the manifest says there is one (`HasPayload`).
- The **`.asset` payload** is the processed binary blob (for a texture: tightly-packed pixels).
  Opaque, versioned, ours.
- Every manifest carries a **`version`**. On mismatch the loader **refuses and logs** (“re-import” /
  “re-author”) — no migration. We’re pre-release, so bumping a schema is free.

## File IO goes through the platform

Asset code never touches `std::fstream`. All reads/writes go through the platform file API
(`PlatformState::API.ReadFile` / `WriteFile`), wrapped game-side in `dali/game/file.h`
(`ReadFile`/`WriteFile`) the same way `log.h` wraps logging. `WriteFile` **creates missing parent
directories**; `ReadFile` returns an arena buffer that is null-terminated one byte past its end, so
YAML text can be parsed straight from it. This keeps file IO on the platform (the port surface). The
editor also reaches a few OS-only services this way — see *Platform services the editor uses*.

## Asset identifiers

An **asset id** is the single string that is the reference, the registry key, and the on-disk stem.
Rules (enforced by `AssetId`):

- **lowercase**, **no spaces**, **forward slashes**, **no extension**
- rooted at `assets/` (e.g. `textures/goblin/walk` ⇒ `assets/textures/goblin/walk.{yml,asset}`)

`AssetId::Normalize(raw)` coerces arbitrary input into canonical form (lowercases, `\`→`/`,
space→`_`, strips a leading `assets/`, strips the extension via `paths::RemoveExtension`, collapses
repeated slashes). `AssetId::IsValid()` answers *“is this already canonical?”* — a stored id that
isn't is a **bug**, caught loudly, never silently fixed.

Each asset type declares its **id root** (`TextureAsset::kIdRoot = "textures"`,
`SpritesheetAsset::kIdRoot = "spritesheets"`, `EnemyAsset::kIdRoot = "enemies"`). The importer uses it
to *suggest* an id from a source (a raw texture `raw/sprites/goblin/U_Walk.png` → `goblin/U_Walk`:
strip `raw/`, drop the category dir — the root is added by the create form, see below).

Rationale for path-ids over opaque GUIDs: they're greppable and debuggable, and a broken reference is
a `grep` away. The cost — renaming/moving under `assets/` breaks referrers — is acceptable for a solo
project. (GUIDs solve rename-at-scale, a problem we don't have.)

### Short ids in the UI

The full id is the one true identity **everywhere it is stored, keyed, referenced, serialized, or
logged**. But the root (`textures/`, `enemies/`, …) is redundant in type-contextual UI, so it is
stripped at the **UI edges only** — purely a presentation/entry convenience, no data-model change.
Three helpers in `asset.{h,cpp}`:

- `IdRootForType(EAssetType)` — the `type → root` map. Hardcoded in `asset.cpp` (so the module stays
  free of the concrete asset headers); **keep in sync with the structs' `kIdRoot`**.
- `ShortId(type, id)` — the root-stripped view for display (`textures/goblin/walk` → `goblin/walk`).
  Returns a view into the id's own buffer, so it stays a valid C string; selection still keys on the
  full id.
- `AssetIdFromShort(type, input)` — the inverse for entry: prepends the tab's root and normalizes.
  **Tolerant** — an already-full pasted id isn't doubled.

So create forms hold the *short* id (defaults like `goblin/walk`, `goblin`), the `->` preview shows
the full canonical id, and the Database / per-type lists / spritesheet texture-ref picker all *show*
short ids. Inspector “Id:” lines and every on-disk reference keep the full id.

## References, resolution, and the crawl

Assets reference other assets **by id string**, root-relative. The crawl is **two-pass** because a
referrer may load before its target:

1. **Load** — walk `assets/**/*.yml`; `PeekManifest` reads the common header (`type`) to dispatch to
   the right loader (`LoadTexture` / `LoadSpritesheet` / `LoadEnemyBlueprint`). Each loader also
   cross-checks the manifest's embedded `id` against the id derived from the file path (mismatch =
   moved/mis-authored → logged).
2. **Resolve** — `AssetRegistry::ResolveReferences()` links every asset's references.

**Resolution is asset-owned.** The registry only *orchestrates* (`for each sheet:
sheet.ResolveReferences(*this)`); the asset knows its own reference fields and wires them via the
registry’s lookups, logging any missing target. This keeps the volatile knowledge (which refs an
asset has) on the asset and off the registry — the shape that scales to heterogeneous refs
(`model → meshes + materials + textures`) and is the precondition for a future generic type table.
The asset header forward-declares `AssetRegistry` and includes `registry.h` only in its `.cpp`, so the
asset↔registry cycle stays compile-clean. (Enemies have no references in v1, so they don't participate
in this pass yet.)

Pointer note: resolved pointers (e.g. a spritesheet’s `_Resolved` texture) point into the registry’s
`FixedVector` holders, which never move elements — but **any re-crawl invalidates them**, so
`ResolveReferences` re-runs after every crawl (and after a standalone load in the editor).

## Textures

Source-importing, has a payload. `TextureAsset` = `{ Manifest, Settings{FlipVertically}, Resource }`.
The `Texture` GPU resource is **self-describing** (handle, width, height, channels, filter) — the
asset holds no duplicate dims. Import decodes the source with stb and writes raw pixels to `.asset` +
dims/settings to `.yml`; load reads the payload and uploads via `Texture::FromPixels`.

- **Filter** is live GL state on `Texture` (`SetFilter`, no re-bake) — that’s why it isn’t an import
  “setting”. **Flip** is a decode-time transform that leaves no queryable trace, so it lives in
  `Settings` and needs a **Re-import**. The struct boundary *is* the Save-vs-Re-import boundary.
- **Save** rewrites `.yml`; **Re-import** re-decodes from the stored `source`. Creating with an
  existing id *is* re-import — one code path.

## Spritesheets (v3)

A spritesheet is a **concept** (“goblin”), not one texture: it's just a named **bag of clips**. Each
clip is self-contained — it carries its own texture reference and its own grid, and its frames are
baked to ready-to-sample UV rects at resolve time. There is no separate texture-reference layer (v2
had one; the clip → ref lookup and per-draw UV math were the awkward seam that motivated the v3
collapse).

**One struct:** `SpriteClip { Name, Texture(id), Grid, Frames[cell indices] }` — plus resolved/baked
fields (`_Resolved`, `_Handle`, `_CellSize`, `_Frames[UV rects]`) that are **not serialized**.

- **Frames are stored as cell indices** (the source of truth on disk). They're reimport-safe: the
  grid is in pixels, so anything that changes the texture geometry already forces you to re-slice, at
  which point the bake re-runs anyway. See the design note below.
- **Grid** (cell size, margin, spacing) lives on the clip, because a clip slices one texture one way.
- **Baked geometry is clip-level, not per-frame.** Every frame of a clip samples the same texture, so
  the GL handle (`_Handle`) and cell size (`_CellSize`) hoist to the clip; a baked frame (`FrameUv`)
  is *just* a UV rectangle (`{Uv0, Uv1}`).

```yaml
type: spritesheet
version: 3
id: spritesheets/goblin
clips:
  - name: walk_down
    texture: textures/goblin/walk_down
    grid: { cell_w: 64, cell_h: 64, margin: 0, spacing: 0 }
    frames: [0, 1, 2, 3]
```

**Playback params are the caller’s, not the clip’s.** A clip is just frames; `fps` and `loop` are
supplied by whoever plays it. `SpriteClip::At(time, fps, loop)` maps elapsed time to a **playback
position** (`0..Frames.Size-1`). The runtime draw chain — no lookup, no per-frame math:

```
clip → pos = clip.At(time, fps, loop) → draw(clip._Handle, clip._CellSize, clip._Frames[pos])
```

**Bake happens at resolve time, never at load.** `SpriteClip::Resolve` links the texture and fills
`_Handle` / `_CellSize` / `_Frames` from the grid + live texture dims. This runs in the registry's
second pass (`ResolveReferences`, after all textures are loaded — a sheet can be crawled before its
texture), and the editor re-runs it after *every* clip edit (texture/grid/frame change) so the baked
frames the preview samples stay current. `CellRect`/`CellCount` compute live from the grid (the
editor's frame-picker overlay uses them, so it's correct even before a re-bake).

> **Why bake and not recompute-live?** Baked UVs depend on texture pixel dims (`u/texW`), so they'd
> go “stale” if a texture is re-imported at a different resolution. That's a non-problem: the grid is
> in pixels, so a resolution change already invalidates the slicing and sends you back to the editor
> to re-slice — the re-bake is a byproduct of a step you were taking anyway. There's no scenario
> where live-recompute quietly gets something right that baking gets wrong.

## Enemies (v1)

An enemy asset is a **lightweight CDO** — the design-time definition of one enemy type (“goblin”),
stamped into runtime instances by `World::SpawnEnemy`. Composing / yml-only (no payload), so it slots
onto the same seams as spritesheets. `EnemyAsset = { Manifest, InstanceData Data }`.

**`InstanceData` is the spawn-snapshot boundary.** It is a small struct — `Speed, MaxHealth, Damage,
Reward, Color` — embedded on *both* the blueprint (`EnemyAsset::Data`) and the runtime `Enemy`
(`Enemy::Data`). Applying a blueprint is therefore one memberwise copy (`enemy.Data = blueprint.Data`),
so a new stat is added in exactly one place and can’t be forgotten. The rule the boundary encodes:
**a field in `InstanceData` is snapshotted at spawn; anything on `EnemyAsset` outside it (future
references, resolved pointers) is not.**

```yaml
type: enemy
version: 1
id: enemies/goblin
instance:
  speed: 100
  max_health: 10
  damage: 5
  reward: 5
  color: "aa734403"   # rrggbbaa, double-quoted so a numeric-looking hex can't be misread
```

- **Colour** serializes as one `rrggbbaa` hex scalar (channels written out explicitly, endian-safe),
  not an `{r,g,b,a}` map. Parsing is tolerant: anything that isn’t exactly 8 hex digits (with an
  optional `#`/`0x`) leaves the default, so a malformed value degrades instead of failing the load.
- **`instance:` is a sub-map** mirroring the `InstanceData` struct — consistent with how `SpriteGrid`
  serializes as `grid:`. Struct nesting and yaml nesting are decoupled (serde is hand-written), but
  we keep them aligned.

### Blueprint → instance

The runtime `Enemy` carries `Health` (the live pool), `InstanceData Data` (the snapshot), and
`AssetId Blueprint` (the link back). `SpawnEnemy(const EnemyAsset&, Hex, health_scale)` copies `Data`,
scales `MaxHealth` on the *copy*, seeds `Health` from it, and records the blueprint id. Two
consequences:

- **Snapshot, not live** — editing a blueprint never mutates enemies already walking (correct CDO
  behaviour), and it dodges the pointer-invalidation hazard: the instance holds a stable `AssetId`,
  not an `EnemyAsset*` that a re-crawl/reload could dangle.
- **The sim stays registry-agnostic** — `SpawnEnemy` takes the blueprint *by reference*; the caller
  (`GameUpdate` / `UpdateWave`) resolves `World::DefaultEnemy` against the registry and passes it in,
  falling back to a default-stat `EnemyAsset` if the id is missing, so the game runs before any enemy
  asset is authored. `World::DefaultEnemy` is the single blueprint waves spawn until wave-composition
  (per-wave / per-spawner types) exists.

## The editor (Assets tab)

Mode switch in the top bar → **Assets**, then a **secondary type bar**: **Database | Texture |
Spritesheet | Enemy**. Database is the landing pane (`AssetEditor::ShowDatabase`, default on); the
per-type entries set `CurrentType`.

- **Database** — a read-only, type-agnostic overview. Left: every loaded asset, grouped by type,
  shown by short id. Right: the selected asset’s **manifest metadata** (type, id, version, source,
  payload), its `.yml` path, its **last-modified time** (local, formatted with the UTC offset), and a
  **git state** line. A **“Verify all (git)”** button runs one `git status` over the whole tree and
  marks every modified asset (`* `, coloured) in the list with a `modified: N` count — see below.
- **Texture tab** — a create form: Browse (native file dialog → path relativized to the working dir),
  a suggested (short) id, flip/filter, Create/Re-import. Inspector: metadata, live filter,
  Save/Re-import, and a zoomable preview.
- **Spritesheet tab** — **Create takes just an id** (a concept has no single source). The inspector
  is a single **Clips** section (there's no separate texture-reference pane in v3):
  - **Add a clip** — name + a texture picked from the registry's loaded textures; grid + frames are
    set in the clip's own editor once it exists.
  - **The selected clip expands** to: a texture combo, grid inputs (cell w/h, margin, spacing),
    Fill-all / Clear, a **grid-overlay frame picker** (cells drawn live from the clip's grid;
    click a cell to append it to the sequence, cells already in the clip are highlighted), and
    **playback** (editor-local fps/loop, Play/Pause/Restart, current baked frame preview; time
    advances off `ImGui::GetIO().DeltaTime`). Any edit re-resolves the clip so the baked frames the
    preview samples stay current.
  - **Save** writes the manifest.
- **Enemy tab** — Create takes just a (short) id; the inspector edits `InstanceData` in place
  (stat drags + a colour picker) and **Save** writes the manifest. No preview machinery — the colour
  swatch is the whole visual.

## Platform services the editor uses

Beyond file IO, the editor reaches three OS-only capabilities through the platform contract
(`dali/core/api.h`, filled in `PlatformInit`, wrapped game-side). **All are set at startup, so they
need a `main.exe` restart to populate — a plain DLL hot-reload leaves the pointers null and the
wrappers degrade gracefully (unknown time / “git unavailable”).**

- **File times** — `GetFileModTime(path, i64* out_ns, DateTime* out_datetime = nullptr,
  bool datetime_local = true)`. The raw `i64` (nanoseconds since the epoch) is the primitive — good
  for sorting and “changed since load” without another call. The `DateTime` breakdown is an optional
  convenience the platform fills (only it has the timezone rules; via `SDL_TimeToDateTime`,
  DST-correct, carrying `UtcOffsetSeconds` so a value is self-describing). Defaults live on the
  game-side wrapper (`file.h`), not the contract pointer — default args are illegal on a
  pointer-to-function.
- **Subprocess** — `RunProcess(arena, std::span<const StringView> args) → ProcessResult{Launched,
  ExitCode, Stdout}` (`dali/game/process.h`, over `SDL_Process`). **argv is a vector, never a shell
  string** (injection-safe). No working-directory parameter (the SDL API has none) — pass a tool flag
  such as `git -C <dir>`. It **blocks** and a spawn is far costlier than a file op, so callers must
  run it **on-demand / cached, never per-frame**. Editor-only in practice.
- **Git dirty check** — built on the two above. `Verify all` runs
  `git -C <basedir> status --porcelain -- assets` once, then maps each dirty path back to its asset:
  the porcelain path (`assets/textures/goblin/walk.yml`) is fed through `AssetId::Normalize`, which
  strips the `assets/` prefix and the extension, so a dirty **`.yml` and `.asset` collapse to the same
  id** (both files considered). The result is cached as a dirty-id set (`GitDirtyIds`) that marks the
  list and drives the detail pane’s Modified/Clean line, stable until the next Verify.

> These three (plus the existing `OpenFileDialog` / `OpenContainingFolder`) are **editor-only**
> capabilities that currently ship ungated — there is no separate ship build yet. When one exists,
> gating them together behind that config (a subprocess primitive especially shouldn’t ship) is a
> single future task.

## Memory

- **Registry + all asset metadata** live *by value* inside `GameState`, in the platform-owned
  `PermanentArena`. Ids and paths are inline `FixedString` (no interning, no pointers to keep alive),
  so it **survives DLL reloads untouched**. We crawl once at init and do **not** re-load on reload.
  The registry is three holders — `Textures`, `Spritesheets`, `EnemyBlueprints`.
- **Texel data** lives in GPU/driver memory, keyed by the `GLuint` handle in `Texture`. It also
  survives reloads (the GL context is platform-owned; the DLL only reloads its GLAD table). A loaded
  texture’s CPU footprint is ~a handle + metadata; the pixels are on the GPU.
- **Transient decode/read buffers** borrow a scratch arena (`Arena::GetScratch()`, 32 MB) and give it
  back when the op finishes. If payloads outgrow scratch (big meshes), a dedicated `AssetLoadingArena`
  is a localized swap.

## Code map

```
dali/core/
  api.h                   Platform↔game contract: DateTime, ProcessResult, and the PlatformAPI
                          pointers (ReadFile/WriteFile/GetFileModTime/RunProcess/dialogs/…).
dali/game/
  file.h/.cpp             ReadFile / WriteFile / GetFileModTime — wrappers over the platform file API.
  process.h/.cpp          RunProcess — wrapper over the platform subprocess API (editor tooling).
  graphics/
    texture.h/.cpp        Texture: self-describing GPU resource. ETextureFilter, FromPixels, SetFilter.
  assets/
    asset.h/.cpp          EAssetType, AssetId (Normalize/IsValid), AssetManifest, path helpers,
                          PeekManifest (crawl dispatch), IdRootForType/ShortId/AssetIdFromShort.
    texture_asset.h/.cpp  TextureAsset: Import(raw)->baked, LoadFromDisk, SaveManifest, Reimport.
    spritesheet_asset.h/.cpp
                          SpriteGrid, FrameUv (baked UV rect), SpriteClip (Texture+Grid+Frames;
                          Resolve bakes _Handle/_CellSize/_Frames; CellCount/CellRect/At),
                          SpritesheetAsset: Create(id), LoadFromDisk, SaveManifest, ResolveReferences.
    enemy_asset.h/.cpp    InstanceData (the spawn snapshot), EnemyAsset: Create(id), LoadFromDisk,
                          SaveManifest. Colour <-> rrggbbaa hex.
    registry.h/.cpp       AssetRegistry: CrawlAndLoad (two-pass), ResolveReferences (orchestration),
                          Load/Find per type (three holders). Lives by value in GameState.
    asset_editor.h/.cpp   The Assets-tab UI (Database + per-type tabs, forms, inspectors, previews,
                          git verify).
```

## Deliberately deferred

GUID identity, filesystem watchers / auto-reimport, dependency graphs, per-platform cooking, texture
compression (BCn), atlas packing, streaming / lazy load, an **editor/ship build split** (to gate the
editor-only platform services above), and the **generic X-macro asset-type table** (asset-owned
resolution makes it possible; at 3 hand-written holders the per-type loops are starting to earn it,
but not yet). Each is a real feature; none is needed yet.

## Not yet built

- **Enemy visuals** — the v2 `EnemyAsset`: a spritesheet + default clip reference, resolved via the
  asset-owned `ResolveReferences` seam, so a spawned enemy draws its current animation frame in the
  world (instance holds the blueprint id, resolves the sprite through it). v1 draws a flat colour.
- **Wave composition** — which blueprint each wave / spawner emits; today `World::DefaultEnemy` is the
  single type.
- **Meshes / materials** — the next source-importing + composing pair; they slot onto the same seams
  (id, manifest, payload, two-pass resolve) without a redesign.
```
