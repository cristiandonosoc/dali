#pragma once

#include <dali/core/container.h>
#include <dali/game/assets/enemy_asset.h>
#include <dali/game/assets/spritesheet_asset.h>
#include <dali/game/assets/texture_asset.h>
#include <dali/game/assets/tower_asset.h>
#include "dali/game/assets/asset.h"

namespace kdk {

// The loaded set of assets. Lives by value inside GameState (in the platform-owned PermanentArena),
// so it is a self-contained value blob that survives DLL reloads untouched.
// TODO(cdc): linear lookups and one holder per type.
struct AssetRegistry {
#define X(ASSET_NAME, MAX_QUANTITY, ...)                          \
    static constexpr i32 kMax##ASSET_NAME##Assets = MAX_QUANTITY; \
    FixedVector<ASSET_NAME##Asset, MAX_QUANTITY> ASSET_NAME##Assets = {};

    XASSET_TYPES(X);
#undef X

    // Destroys every loaded asset and reloads by crawling the assets/ directory, then resolves
    // cross-asset references. Call once at init; also the "Rescan" action in the editor.
    void CrawlAndLoad();

    // Links each spritesheet's _Texture to its referenced texture. Runs after a crawl (references
    // can't resolve mid-load) and after a standalone spritesheet load. Logs a missing reference.
    void ResolveReferences();

    // Loads (or reloads) a single asset by id from disk, replacing an existing entry with the same
    // id (freeing a texture's GPU resource first). nullptr on failure.
#define X(ASSET_NAME, ...)                                  \
    ASSET_NAME##Asset* Load##ASSET_NAME##Asset(AssetId id); \
    ASSET_NAME##Asset* Find##ASSET_NAME##Asset(AssetId id);

    XASSET_TYPES(X);
#undef X
};

}  // namespace kdk
