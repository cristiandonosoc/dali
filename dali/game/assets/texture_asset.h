#pragma once

#include <dali/game/assets/asset.h>
#include <dali/game/graphics/texture.h>

#include <optional>

namespace kdk {

struct AssetRegistry;

// The import-time transforms a texture has. These leave no queryable trace on the GL object (unlike
// size/channels/filter), so the asset must remember them to re-import. Filter is NOT here: it is
// live state on the Texture (Resource) and changeable without a re-bake.
struct TextureImportSettings {
    bool FlipVertically = false;  // Row-flip at decode. Change requires re-import.

    void Serialize(SerdeArchive* sa);
};

struct TextureAsset {
    static constexpr EAssetType kAssetType = EAssetType::Texture;
    static constexpr i32 kVersion = 1;
    // The root directory of a texture asset's id ("textures/goblin/walk"). The importer uses it to
    // suggest an id from a raw source path (the id's first directory is the asset type, not the
    // source's).
    static constexpr StringView kIdRoot = "textures"sv;
    // The only asset type with a baked payload beside its .yml (<id>.asset, tightly-packed pixels).
    static constexpr bool kHasPayload = true;

    AssetManifest Manifest = {};
    TextureImportSettings Settings = {};
    // The runtime GPU resource, and the authority on the texture's dimensions, channels, and filter
    // (no duplication here). Lives in the GL context, so it survives DLL reloads.
    Texture Resource = {};

    // Decodes |source_raw| (a raw image path relative to the working dir) and writes the baked pair
    // under assets/: <id>.asset (tightly-packed pixels) and <id>.yml (manifest + dims + settings).
    // |filter| seeds the texture's sampling mode. Does not upload. Overwrites any existing pair
    // (create == re-import).
    static bool Import(StringView source_raw,
                       AssetId id,
                       const TextureImportSettings& settings,
                       ETextureFilter filter);
    // Reads the baked pair for |id| and uploads to the GPU. nullopt if the manifest isn't a
    // texture, it was written by a newer build, or a file is missing.
    static std::optional<TextureAsset> LoadFromDisk(AssetId id);

    // Rewrites only the .yml (metadata tweak; no re-decode, no re-upload).
    bool SaveManifest();

    void Serialize(SerdeArchive* sa);
    // Re-runs Import from Manifest.Source with the current Settings, then reloads in place
    // (replaces this asset's GPU resource and fields). Use after changing a payload-affecting
    // setting.
    bool Reimport();

    bool ResolveReferences(AssetRegistry&) { return true; }  // no-op
};

}  // namespace kdk
