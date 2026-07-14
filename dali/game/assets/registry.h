#pragma once

#include <dali/core/container.h>
#include <dali/game/assets/enemy_asset.h>
#include <dali/game/assets/spritesheet_asset.h>
#include <dali/game/assets/texture_asset.h>

namespace kdk {

// The loaded set of assets. Lives by value inside GameState (in the platform-owned PermanentArena),
// so it is a self-contained value blob that survives DLL reloads untouched.
// TODO(cdc): linear lookups and one holder per type.
//			  A generic X-macro table can come once there are enough types.
struct AssetRegistry {
    static constexpr i32 kMaxTextureAssets = 256;
    static constexpr i32 kMaxSpriteSheetAssets = 256;
    static constexpr i32 kMaxEnemyAssets = 256;

    FixedVector<TextureAsset, kMaxTextureAssets> TextureAssets = {};
    FixedVector<SpriteSheetAsset, kMaxSpriteSheetAssets> SpriteSheetAssets = {};
    FixedVector<EnemyAsset, kMaxEnemyAssets> EnemyAssets = {};

    // Destroys every loaded asset and reloads by crawling the assets/ directory, then resolves
    // cross-asset references. Call once at init; also the "Rescan" action in the editor.
    void CrawlAndLoad();
    // Links each spritesheet's _Texture to its referenced texture. Runs after a crawl (references
    // can't resolve mid-load) and after a standalone spritesheet load. Logs a missing reference.
    void ResolveReferences();

    // Loads (or reloads) a single asset by id from disk, replacing an existing entry with the same
    // id (freeing a texture's GPU resource first). nullptr on failure.
    TextureAsset* LoadTexture(AssetId id);
    SpriteSheetAsset* LoadSpriteSheet(AssetId id);
    EnemyAsset* LoadEnemyBlueprint(AssetId id);

    // Finds a loaded asset by id, or nullptr.
    TextureAsset* FindTexture(AssetId id);
    SpriteSheetAsset* FindSpriteSheet(AssetId id);
    EnemyAsset* FindEnemyBlueprint(AssetId id);
};

}  // namespace kdk
