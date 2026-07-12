# Dali — Asset System

Status: **texture + spritesheet implemented.** Textures import raw art into a baked payload;
spritesheets are concept-level animation assets composed from textures. Meshes/materials are designed
for but not built.

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
YAML text can be parsed straight from it. This keeps file IO on the platform (the port surface).

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
`SpritesheetAsset::kIdRoot = "spritesheets"`). The importer uses it to *suggest* an id from a source
(a raw texture `raw/sprites/goblin/U_Walk.png` → `textures/goblin/U_Walk`: strip `raw/`, drop the
category dir, prepend the type root).

Rationale for path-ids over opaque GUIDs: they're greppable and debuggable, and a broken reference is
a `grep` away. The cost — renaming/moving under `assets/` breaks referrers — is acceptable for a solo
project. (GUIDs solve rename-at-scale, a problem we don't have.)

## References, resolution, and the crawl

Assets reference other assets **by id string**, root-relative. The crawl is **two-pass** because a
referrer may load before its target:

1. **Load** — walk `assets/**/*.yml`; `PeekManifest` reads the common header (`type`) to dispatch to
   the right loader (`LoadTexture` / `LoadSpritesheet`). Each loader also cross-checks the manifest's
   embedded `id` against the id derived from the file path (mismatch = moved/mis-authored → logged).
2. **Resolve** — `AssetRegistry::ResolveReferences()` links every asset's references.

**Resolution is asset-owned.** The registry only *orchestrates* (`for each sheet:
sheet.ResolveReferences(*this)`); the asset knows its own reference fields and wires them via the
registry’s lookups, logging any missing target. This keeps the volatile knowledge (which refs an
asset has) on the asset and off the registry — the shape that scales to heterogeneous refs
(`model → meshes + materials + textures`) and is the precondition for a future generic type table.
The asset header forward-declares `AssetRegistry` and includes `registry.h` only in its `.cpp`, so the
asset↔registry cycle stays compile-clean.

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

## Spritesheets (v2)

A spritesheet is a **concept** (“goblin”), not one texture. Two layers:

- **Texture references** (`SpriteTextureRef { Texture(id), Grid, _Resolved }`) — a concept pulls from
  *several* textures (each little animation may be its own PNG). The **grid** (cell size, margin,
  spacing) lives here, per texture, because a texture is sliced one way. Frame geometry
  (`FrameCount`, `FrameRect(frame) → {handle, uv0, uv1}`) is on the ref.
- **Clips** (`SpriteClip { Name, Texture(id), Frames }`) — named animations built *against* a
  reference. A clip picks one ref (by texture id) and an ordered list of frame indices.

```yaml
type: spritesheet
version: 2
id: spritesheets/goblin
textures:
  - { texture: textures/goblin/walk_down, grid: { cell_w: 64, cell_h: 64, margin: 0, spacing: 0 } }
clips:
  - { name: walk_down, texture: textures/goblin/walk_down, frames: [0, 1, 2, 3] }
```

**Playback params are the caller’s, not the clip’s.** A clip is just frames; `fps` and `loop` are
supplied by whoever plays it. `SpriteClip::At(time, fps, loop)` maps elapsed time to a frame index.
The runtime draw chain:

```
clip → sheet.FindTextureRef(clip.Texture) → ref.FrameRect(clip.At(time, fps, loop)) → draw (handle + uv rect)
```

> **Known rough edge:** the spritesheet API is functional but wants a cleaner pass (e.g. a single
> `Resolve(clip, time, fps, loop) → FrameUV` convenience, per-frame clip editing beyond “fill all”,
> and cascade cleanup so removing a texture ref doesn’t leave clips dangling).

## The editor (Assets tab)

Mode switch in the top bar → **Assets**, then a **secondary type bar** (Texture | Spritesheet)
selects the tab.

- **Texture tab** — a create form: Browse (native file dialog → path relativized to the working dir),
  a suggested id, flip/filter, Create/Re-import. Inspector: metadata, live filter, Save/Re-import,
  and a zoomable preview.
- **Spritesheet tab** — **Create takes just an id** (a concept has no single source). The inspector
  is where it’s assembled:
  - **Texture References** — a list of the registry’s *loaded textures* (already-referenced ones
    greyed) to Add; the sheet’s refs below, each collapsing to its grid inputs + a **grid-overlay
    preview** (cell boundaries drawn from the ref’s own `FrameRect`, so the overlay matches what the
    runtime samples) only when selected.
  - **Clips** — add (name + ref picker; frames default to all of the ref); the selected clip expands
    to **playback** (editor-local fps/loop, Play/Pause/Restart, live frame preview via the resolve
    chain; time advances off `ImGui::GetIO().DeltaTime`).
  - **Save** writes the manifest.

## Memory

- **Registry + all asset metadata** live *by value* inside `GameState`, in the platform-owned
  `PermanentArena`. Ids and paths are inline `FixedString` (no interning, no pointers to keep alive),
  so it **survives DLL reloads untouched**. We crawl once at init and do **not** re-load on reload.
- **Texel data** lives in GPU/driver memory, keyed by the `GLuint` handle in `Texture`. It also
  survives reloads (the GL context is platform-owned; the DLL only reloads its GLAD table). A loaded
  texture’s CPU footprint is ~a handle + metadata; the pixels are on the GPU.
- **Transient decode/read buffers** borrow a scratch arena (`Arena::GetScratch()`, 32 MB) and give it
  back when the op finishes. If payloads outgrow scratch (big meshes), a dedicated `AssetLoadingArena`
  is a localized swap.

## Code map

```
dali/game/
  file.h/.cpp             ReadFile / WriteFile — thin wrappers over the platform file API.
  graphics/
    texture.h/.cpp        Texture: self-describing GPU resource. ETextureFilter, FromPixels, SetFilter.
  assets/
    asset.h/.cpp          EAssetType, AssetId (Normalize/IsValid), AssetManifest, path helpers,
                          PeekManifest (crawl dispatch).
    texture_asset.h/.cpp  TextureAsset: Import(raw)->baked, LoadFromDisk, SaveManifest, Reimport.
    spritesheet_asset.h/.cpp
                          SpriteGrid, SpriteTextureRef (+ FrameCount/FrameRect), SpriteClip (+ At),
                          SpritesheetAsset: Create(id), LoadFromDisk, SaveManifest, ResolveReferences.
    registry.h/.cpp       AssetRegistry: CrawlAndLoad (two-pass), ResolveReferences (orchestration),
                          Load/Find per type. Lives by value in GameState.
    asset_editor.h/.cpp   The Assets-tab UI (per-type tabs, forms, inspectors, previews).
```

## Deliberately deferred

GUID identity, filesystem watchers / auto-reimport, dependency graphs, per-platform cooking, texture
compression (BCn), atlas packing, streaming / lazy load, and the **generic X-macro asset-type table**
(asset-owned resolution now makes it possible, but 2 types don’t earn it — the per-holder loops in
the registry are the thing it would collapse). Each is a real feature; none is needed yet.

## Not yet built

- **Gameplay integration** — an enemy referencing a spritesheet + clip name, holding its own
  time/fps/loop, drawing its current frame in the world. This lives in gameplay, not the asset system,
  and reuses the runtime resolve chain.
- **Meshes / materials** — the next source-importing + composing pair; they slot onto the same seams
  (id, manifest, payload, two-pass resolve) without a redesign.
