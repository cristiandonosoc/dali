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

// Recursively walks |abs_dir|, loading each .yml it finds. |rel_prefix| is the id-space path (always
// forward-slashed) accumulated from the assets root down to |abs_dir|, so the id derives from the
// recursion rather than by string-stripping an absolute (possibly backslashed) path.
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
        registry->LoadTexture(id);
    }
}

}  // namespace registry_private

void AssetRegistry::CrawlAndLoad() {
    using namespace registry_private;

    // Free existing GPU textures before dropping the entries, or the handles leak.
    for (TextureAsset& tex : Textures) {
        tex.Resource.Destroy();
    }
    Textures.Clear();

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView root = GetAssetsRoot(arena);
    CrawlDir(this, arena, root, StringView());

    LogInfo("AssetRegistry: loaded %d textures", Textures.Size);
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

    if (Textures.IsFull()) {
        LogError("AssetRegistry: texture capacity (%d) reached, dropping '%s'",
                 kMaxTextures,
                 id.Value.Str());
        loaded->Resource.Destroy();
        return nullptr;
    }
    return &Textures.Push(*loaded);
}

TextureAsset* AssetRegistry::FindTexture(AssetId id) {
    for (TextureAsset& tex : Textures) {
        if (tex.Manifest.Id == id) {
            return &tex;
        }
    }
    return nullptr;
}

}  // namespace kdk
