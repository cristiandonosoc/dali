#include <dali/game/assets/registry.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/log.h>

namespace kdk {

namespace registry_private {

bool HasExtension(StringView path, StringView ext) {
    if (path.Size < ext.Size) {
        return false;
    }
    u64 offset = path.Size - ext.Size;
    for (u64 i = 0; i < ext.Size; ++i) {
        if (path[offset + i] != ext[i]) {
            return false;
        }
    }
    return true;
}

// Recursively walks |abs_dir|, loading each .yml it finds. |rel_prefix| is the id-space path
// (always forward-slashed) accumulated from the assets root down to |abs_dir|, so the id derives
// from the recursion rather than by string-stripping an absolute (possibly backslashed) path.
void CrawlDir(AssetRegistry* registry, Arena* arena, StringView abs_dir, StringView rel_prefix) {
    std::span<paths::DirEntry> entries = paths::ListDir(arena, abs_dir);
    for (const paths::DirEntry& entry : entries) {
        StringView name = paths::GetBasename(arena, entry.Path);
        if (name.IsEmpty()) {
            continue;
        }
        StringView rel = name;
        if (!rel_prefix.IsEmpty()) {
            rel = Printf(arena, "%s/%s", rel_prefix.Str(), name.Str());
        }

        if (entry.IsDir()) {
            CrawlDir(registry, arena, entry.Path, rel);
            continue;
        }
        if (!entry.IsFile()) {
            continue;
        }
        if (!HasExtension(name, ".yml"sv)) {
            continue;
        }

        AssetId id = AssetId::Normalize(rel);

        // Peek the type to dispatch to the right loader (each loader re-reads the file; the peek
        // avoids trying every loader against every file).
        AssetManifest header = {};
        if (!PeekManifest(id, &header)) {
            continue;
        }
        switch (header.Type) {
            case EAssetType::Texture: {
                registry->LoadTexture(id);
                break;
            }
            case EAssetType::SpriteSheet: {
                registry->LoadSpriteSheet(id);
                break;
            }
            case EAssetType::Enemy: {
                registry->LoadEnemyBlueprint(id);
                break;
            }
            default: {
                LogWarning("AssetRegistry: unknown asset type in '%s'", id.Value.Str());
                break;
            }
        }
    }
}

}  // namespace registry_private

void AssetRegistry::CrawlAndLoad() {
    using namespace registry_private;

    // Free existing GPU textures before dropping the entries, or the handles leak. SpriteSheets own
    // no GPU resource, so clearing them is enough.
    for (TextureAsset& tex : TextureAssets) {
        tex.Resource.Destroy();
    }
    TextureAssets.Clear();
    SpriteSheetAssets.Clear();
    EnemyAssets.Clear();

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView root = GetAssetsRoot(arena);
    CrawlDir(this, arena, root, StringView());

    // A spritesheet may be crawled before the texture it references, so references are resolved in
    // a second pass once everything is loaded.
    ResolveReferences();

    LogInfo("AssetRegistry: loaded %d textures, %d spritesheets, %d enemies",
            TextureAssets.Size,
            SpriteSheetAssets.Size,
            EnemyAssets.Size);
}

void AssetRegistry::ResolveReferences() {
    // Orchestration only: each asset knows its own references and resolves them against us.
    for (SpriteSheetAsset& asset : SpriteSheetAssets) {
        asset.ResolveReferences(*this);
    }
    // Enemy blueprints hold a SpriteSheetClipReference resolved live at draw, so nothing to link here.
}

TextureAsset* AssetRegistry::LoadTexture(AssetId id) {
    std::optional<TextureAsset> loaded = TextureAsset::LoadFromDisk(id);
    if (!loaded) {
        return nullptr;
    }

    if (TextureAsset* existing = FindTexture(id)) {
        existing->Resource.Destroy();
        *existing = *loaded;
        return existing;
    }

    if (TextureAssets.IsFull()) {
        LogError("AssetRegistry: texture capacity (%d) reached, dropping '%s'",
                 kMaxTextureAssets,
                 id.Value.Str());
        loaded->Resource.Destroy();
        return nullptr;
    }
    return &TextureAssets.Push(*loaded);
}

TextureAsset* AssetRegistry::FindTexture(AssetId id) {
    for (TextureAsset& tex : TextureAssets) {
        if (tex.Manifest.Id == id) {
            return &tex;
        }
    }
    return nullptr;
}

SpriteSheetAsset* AssetRegistry::LoadSpriteSheet(AssetId id) {
    std::optional<SpriteSheetAsset> loaded = SpriteSheetAsset::LoadFromDisk(id);
    if (!loaded) {
        return nullptr;
    }

    // Does not resolve the texture reference here — the caller runs ResolveReferences once all
    // assets are present (during a crawl the referenced texture may not be loaded yet).
    if (SpriteSheetAsset* existing = FindSpriteSheet(id)) {
        *existing = *loaded;
        return existing;
    }

    if (SpriteSheetAssets.IsFull()) {
        LogError("AssetRegistry: spritesheet capacity (%d) reached, dropping '%s'",
                 kMaxSpriteSheetAssets,
                 id.Value.Str());
        return nullptr;
    }
    return &SpriteSheetAssets.Push(*loaded);
}

SpriteSheetAsset* AssetRegistry::FindSpriteSheet(AssetId id) {
    for (SpriteSheetAsset& sheet : SpriteSheetAssets) {
        if (sheet.Manifest.Id == id) {
            return &sheet;
        }
    }
    return nullptr;
}

EnemyAsset* AssetRegistry::LoadEnemyBlueprint(AssetId id) {
    std::optional<EnemyAsset> loaded = EnemyAsset::LoadFromDisk(id);
    if (!loaded) {
        return nullptr;
    }

    if (EnemyAsset* existing = FindEnemyBlueprint(id)) {
        *existing = *loaded;
        return existing;
    }

    if (EnemyAssets.IsFull()) {
        LogError("AssetRegistry: enemy capacity (%d) reached, dropping '%s'",
                 kMaxEnemyAssets,
                 id.Value.Str());
        return nullptr;
    }
    return &EnemyAssets.Push(*loaded);
}

EnemyAsset* AssetRegistry::FindEnemyBlueprint(AssetId id) {
    for (EnemyAsset& enemy : EnemyAssets) {
        if (enemy.Manifest.Id == id) {
            return &enemy;
        }
    }
    return nullptr;
}

}  // namespace kdk
