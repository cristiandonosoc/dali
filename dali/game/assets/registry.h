#pragma once

#include <dali/core/container.h>
#include <dali/game/assets/spritesheet_asset.h>
#include <dali/game/assets/texture_asset.h>

namespace kdk {

// The loaded set of assets. Lives by value inside GameState (in the platform-owned PermanentArena),
// so it is a self-contained value blob that survives DLL reloads untouched. TODO(perf): linear
// lookups and one holder per type; a generic X-macro table can come once there are enough types.
struct AssetRegistry {
    static constexpr i32 kMaxTextures = 256;
    static constexpr i32 kMaxSpritesheets = 256;

    FixedVector<TextureAsset, kMaxTextures> Textures = {};
    FixedVector<SpritesheetAsset, kMaxSpritesheets> Spritesheets = {};

    // Destroys every loaded asset and reloads by crawling the assets/ directory, then resolves
    // cross-asset references. Call once at init; also the "Rescan" action in the editor.
    void CrawlAndLoad();
    // Links each spritesheet's _Texture to its referenced texture. Runs after a crawl (references
    // can't resolve mid-load) and after a standalone spritesheet load. Logs a missing reference.
    void ResolveReferences();

    // Loads (or reloads) a single asset by id from disk, replacing an existing entry with the same
    // id (freeing a texture's GPU resource first). nullptr on failure.
    TextureAsset* LoadTexture(AssetId id);
    SpritesheetAsset* LoadSpritesheet(AssetId id);

    // Finds a loaded asset by id, or nullptr.
    TextureAsset* FindTexture(AssetId id);
    SpritesheetAsset* FindSpritesheet(AssetId id);
};

}  // namespace kdk
