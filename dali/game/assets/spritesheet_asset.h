#pragma once

#include <dali/core/container.h>
#include <dali/core/math.h>
#include <dali/game/assets/asset.h>
#include <dali/game/assets/texture_asset.h>

#include <optional>

namespace kdk {

struct AssetRegistry;

// How a texture is carved into equally-sized cells. A frame index counts cells left-to-right,
// top-to-bottom.
struct SpriteGrid {
    i32 CellW = 0;
    i32 CellH = 0;
    i32 Margin = 0;   // Border (px) around the whole texture before the first cell.
    i32 Spacing = 0;  // Gap (px) between adjacent cells.
};

// One texture a spritesheet pulls from, plus how it's sliced. The grid lives here (per texture),
// because a texture is cut one way; clips then select frames out of it.
struct SpriteTextureRef {
    AssetId Texture = {};  // "textures/goblin/walk_down"
    SpriteGrid Grid = {};
    // Resolved by the registry (SpritesheetAsset::ResolveReferences); null until then.
    TextureAsset* _Resolved = nullptr;

    // A frame's location within this texture: the GL handle plus its uv rectangle.
    struct FrameUV {
        u32 Handle = 0;
        i32 Width = 0;
        i32 Height = 0;
        Vec2 Uv0 = {};
        Vec2 Uv1 = {};
    };

    // How many whole cells fit. 0 if unresolved or the grid is degenerate.
    i32 FrameCount() const;
    // The uv rectangle of |frame|. Zeroed if unresolved / out of range.
    FrameUV FrameRect(i32 frame) const;
};

// A named animation: an ordered sequence of frames taken from one of the sheet's texture refs.
// Playback rate and looping are the caller's concern, not the clip's (see At).
struct SpriteClip {
    FixedString<64> Name = {};  // "walk_down"
    AssetId Texture = {};       // which texture ref (by its texture id)
    FixedVector<i32, 64> Frames = {};

    // Maps elapsed |time| to one of Frames using caller-supplied |fps| and |loop|. Returns a
    // frame index (into the ref's cells), or NONE if empty. loop == false clamps on the last frame.
    i32 At(float time, float fps, bool loop) const;
};

// A spritesheet: one concept ("goblin"), assembled from many textures and the clips over them.
// Pure metadata (yml-only). A concept can pull from several textures (each little animation may be
// its own texture), so it holds a list of texture refs and a list of clips built against them.
struct SpritesheetAsset {
    static constexpr EAssetType kAssetType = EAssetType::Spritesheet;
    static constexpr i32 kVersion = 2;
    static constexpr StringView kIdRoot = "spritesheets"sv;
    static constexpr i32 kMaxTextureRefs = 32;
    static constexpr i32 kMaxClips = 64;

    AssetManifest Manifest = {};
    FixedVector<SpriteTextureRef, kMaxTextureRefs> Textures = {};
    FixedVector<SpriteClip, kMaxClips> Clips = {};

    // Writes the manifest for a new, empty sheet (no textures, no clips) — a spritesheet is created
    // from just its id; textures and clips are added in the inspector. Overwrites any existing.
    static bool Create(AssetId id);
    // Reads the manifest at |id|. nullopt if it isn't a spritesheet or the version mismatches. Does
    // NOT resolve texture references (the registry does that).
    static std::optional<SpritesheetAsset> LoadFromDisk(AssetId id);
    bool SaveManifest() const;

    // Resolves every texture ref against |registry|. Logs each missing ref; returns whether all
    // resolved.
    bool ResolveReferences(AssetRegistry& registry);

    const SpriteTextureRef* FindTextureRef(AssetId texture) const;
    const SpriteClip* FindClip(StringView name) const;
};

}  // namespace kdk
