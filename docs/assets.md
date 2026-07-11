# Dali — Asset System

Status: **texture slice implemented**; spritesheet and other types are designed here but not yet
built.

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
assets/textures/goblin/walk.asset   # payload  — optional (present for textures/meshes)
```

- The **`.yml` manifest** is human-readable and authoritative: it describes the asset (type,
  version, dimensions, import settings) and how to interpret the payload. The crawler keys on
  `.yml`; the `.asset` is read only if the manifest says there is one (`HasPayload`). Composing
  assets (spritesheet, material) are **yml-only**.
- The **`.asset` payload** is the processed binary blob (for a texture: tightly-packed pixels).
  Opaque, versioned, ours.

## Asset identifiers

An **asset id** is the single string that is the reference, the registry key, and the on-disk stem.
Rules (enforced by `AssetId`):

- **lowercase**, **no spaces**, **forward slashes**, **no extension**
- rooted at `assets/` (e.g. `textures/goblin/walk` ⇒ `assets/textures/goblin/walk.{yml,asset}`)

`AssetId::Normalize(raw)` coerces arbitrary input into canonical form (lowercases, `\`→`/`,
space→`_`, strips a leading `assets/`, strips the extension, collapses repeated slashes).
`AssetId::IsValid()` answers *“is this already canonical?”* — a stored id that isn't is a **bug**,
caught loudly, never silently fixed.

Rationale for path-ids over opaque GUIDs: they're greppable and debuggable, and a broken reference is
a `grep` away. The cost — renaming/moving under `assets/` breaks referrers — is acceptable for a
solo project. (GUIDs solve rename-at-scale, a problem we don't have.)

## References between assets

Composing assets reference other assets **by id string**, root-relative (not relative to the
referring file):

```yaml
# a spritesheet manifest
texture: "textures/goblin/walk"    # ← resolves through the registry, same namespace as the folder tree
```

The registry resolves references after load; a missing target is a **refuse-and-log** error, and the
message contains the path (the debugging win of string ids). The manifest also embeds its own `id`,
which the crawl cross-checks against the id derived from the file's path — a mismatch means a moved or
mis-authored file and is logged.

## The editor loop

The importer is the **Assets tab** (mode switch in the top bar). Each asset type has a creation
*form*, split by where its input comes from:

- **Source-importing** (texture, mesh): form points at a `raw/` file + output id + settings →
  processes into a baked payload.
- **Composing** (spritesheet, material): form references existing *assets* (a picker over the
  registry) + settings → mostly metadata, usually yml-only.

Flow: pick type → fill form → **Create** (writes the manifest + payload, adds to the live registry) →
select it in the list → tweak fields → **Save** (rewrite `.yml` only) or **Re-import** (re-run the
processor from the stored `source`). Creating with an existing id *is* re-import — one code path.

`Save` vs `Re-import`: some fields are load-time only (e.g. filter), some re-process the payload
(e.g. flip). `Save` rewrites metadata; `Re-import` rebakes. (For now, a filter change is only
visible after Re-import, since the filter is applied at GPU-upload time.)

## Memory

- **Registry + all asset metadata** live *by value* inside `GameState`, in the platform-owned
  `PermanentArena`. It's a self-contained value blob (~115 KB for 256 texture slots): ids and paths
  are inline `FixedString` (no interning, no pointers to keep alive), so it **survives DLL reloads
  untouched**. We crawl once at init and do **not** re-load on reload.
- **Texel data** lives in GPU/driver memory, keyed by the `GLuint` handle in `Texture`. It also
  survives reloads (the GL context is platform-owned; the DLL only reloads its GLAD table). So a
  loaded texture's CPU footprint is ~a handle + metadata; the pixels are on the GPU.
- **Transient decode/read buffers** (the raw file bytes during import/load) borrow a scratch arena
  (`Arena::GetScratch()`, 32 MB) and give it back when the op finishes. Nothing large persists
  CPU-side. If payloads ever outgrow scratch (big meshes), a dedicated `AssetLoadingArena` is a
  localized swap.

## Code map

```
dali/game/graphics/
  texture.h/.cpp          Texture: GPU resource. ETextureFilter. FromPixels(buffer) / Load(file).

dali/game/assets/
  asset.h/.cpp            EAssetType, AssetId (Normalize/IsValid), AssetManifest, GetAssetsRoot.
  texture_asset.h/.cpp    TextureAsset: Import(raw)->baked, LoadFromDisk, SaveManifest, Reimport.
  registry.h/.cpp         AssetRegistry: CrawlAndLoad, LoadTexture, FindTexture. Lives in GameState.
  asset_editor.h/.cpp     The Assets-tab UI: creation form + list + inspector.
```

## Deliberately deferred

GUID identity, filesystem watchers / auto-reimport, dependency graphs, per-platform cooking, texture
compression (BCn), atlas packing, streaming / lazy load, a generic X-macro asset-type table (there
are too few types to earn it yet). Each is a real feature; none is needed to prove the pipeline.

## Not yet built

`Spritesheet` (references a texture, holds a grid + inline animation clips) and everything past
textures. The texture path exercises every seam — id, manifest, payload, registry, editor — so those
types slot in without a redesign.
