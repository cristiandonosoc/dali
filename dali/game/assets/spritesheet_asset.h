#pragma once

#include <dali/core/container.h>
#include <dali/core/math.h>
#include <dali/game/assets/asset.h>
#include <dali/game/assets/texture_asset.h>

#include <optional>

namespace kdk {

struct AssetRegistry;

// How a texture is carved into equally-sized cells. A cell index counts cells left-to-right,
// top-to-bottom.
struct SpriteGrid {
    i32 CellW = 0;
    i32 CellH = 0;
    i32 Margin = 0;   // Border (px) around the whole texture before the first cell.
    i32 Spacing = 0;  // Gap (px) between adjacent cells.
};

// A frame's sampled sub-rect within its texture, in normalized UVs. Baked at resolve time from the
// clip's grid + live texture dimensions; never serialized (the yml stores cell indices instead).
struct FrameUv {
    Vec2 Uv0 = {};
    Vec2 Uv1 = {};
};

// A named animation over a single texture, sliced by its own grid. The frame list is stored as cell
// indices (the source of truth, reimport-safe); at resolve time it's baked into _Frames (ready-to-
// sample UV rects) and the texture is linked, so drawing a frame is a direct index with no
// per-frame math and no sibling lookup. Playback rate and looping are the caller's concern, not the
// clip's.
struct SpriteSheetClip {
    FixedString<64> Name = {};  // "walk_down"
    AssetId Texture = {};       // "textures/goblin/walk_down"
    SpriteGrid Grid = {};
    FixedVector<i32, 64> Frames = {};  // cell indices, in playback order (serialized)
	float FPS = 8;

    // Resolved / baked by Resolve(); not serialized. All frames of a clip sample the same texture,
    // so the GL handle and cell size are clip-level, not per-frame.
    TextureAsset* _Resolved = nullptr;
    u32 _Handle = 0;                        // GL texture to bind when drawing this clip
    Vec2 _CellSize = {};                    // draw-quad size in px (uniform grid)
    FixedVector<FrameUv, 64> _Frames = {};  // parallel to Frames; what a draw call samples

    // Links Texture against |registry| and bakes _Handle / _CellSize / _Frames from the grid + live
    // texture dims. Returns whether the texture resolved.
    bool Resolve(AssetRegistry& registry);

    // How many whole cells the grid carves out of the resolved texture. 0 if unresolved or the grid
    // is degenerate. Used by the editor's frame picker.
    i32 CellCount() const;
    // The UV rect of cell |cell| in the resolved texture, computed live from the grid (for the
    // editor overlay). Zeroed if unresolved / out of range.
    FrameUv CellRect(i32 cell) const;

    // Maps elapsed |time| to a playback position (0..Frames.Size-1) using caller-supplied |fps| and
    // |loop|. Returns NONE if empty. loop == false clamps on the last position. Index _Frames
    // (baked UV) or Frames (cell index) with the result.
    i32 At(float time, float fps, bool loop) const;
};

// A spritesheet: one concept ("goblin"), assembled from the clips over its textures. Pure metadata
// (yml-only). Each clip carries its own texture reference and grid, so the sheet is just a named
// bag of clips.
struct SpriteSheetAsset {
    static constexpr EAssetType kAssetType = EAssetType::SpriteSheet;
    static constexpr i32 kVersion = 3;
    static constexpr StringView kIdRoot = "spritesheets"sv;
    static constexpr i32 kMaxClips = 64;

    AssetManifest Manifest = {};
    FixedVector<SpriteSheetClip, kMaxClips> Clips = {};

    // Writes the manifest for a new, empty sheet (no clips) — a spritesheet is created from just
    // its id; clips are added in the inspector. Overwrites any existing.
    static bool Create(AssetId id);
    // Reads the manifest at |id|. nullopt if it isn't a spritesheet or the version mismatches. Does
    // NOT resolve/bake clips (the registry does that in a second pass).
    static std::optional<SpriteSheetAsset> LoadFromDisk(AssetId id);
    bool SaveManifest() const;

    // Resolves and bakes every clip against |registry|. Logs each clip whose texture is missing;
    // returns whether all resolved.
    bool ResolveReferences(AssetRegistry& registry);

    const SpriteSheetClip* FindClip(StringView name) const;
};

// A design-time pointer to one clip inside a sprite sheet, stored as ids (sheet id + clip name)
// rather than a resolved pointer. Resolve() looks the clip up live against the registry on every
// call, so it can never dangle across clip edits or asset reloads. TODO(cdc): cache the resolved
// pointer once the data is known frozen, if the per-lookup cost ever shows up.
struct SpriteSheetClipReference {
    AssetId SpriteSheetId = {};
    FixedString<64> ClipName = {};

    // The referenced clip, looked up live. nullptr if unset, or the sheet/clip no longer exists.
    const SpriteSheetClip* Resolve(AssetRegistry& registry) const;
};

}  // namespace kdk
