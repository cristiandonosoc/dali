#pragma once

#include <dali/core/container.h>
#include <dali/game/assets/texture_asset.h>

namespace kdk {

// The loaded set of assets. Lives by value inside GameState (in the platform-owned PermanentArena),
// so it is a self-contained value blob that survives DLL reloads untouched. TODO(perf): linear
// lookups and one holder per type; a generic X-macro table can come once there are enough types.
struct AssetRegistry {
    static constexpr i32 kMaxTextures = 256;

    FixedVector<TextureAsset, kMaxTextures> Textures = {};

    // Destroys every loaded asset and reloads by crawling the assets/ directory. Call once at init;
    // also the "Rescan" action in the editor.
    void CrawlAndLoad();
    // Loads (or reloads) a single texture by id from disk. Replaces an existing entry with the same
    // id (freeing its GPU resource first). nullptr on failure.
    TextureAsset* LoadTexture(AssetId id);
    // Finds a loaded texture by id, or nullptr.
    TextureAsset* FindTexture(AssetId id);
};

}  // namespace kdk
