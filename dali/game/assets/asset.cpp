#include <dali/game/assets/asset.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/file.h>
#include <dali/game/log.h>

#include <yaml-cpp/yaml.h>

namespace kdk {

namespace asset_private {

// Characters allowed in a canonical id.
bool IsCanonicalChar(char c) {
    bool ok = (c >= 'a' && c <= 'z');
    ok |= (c >= '0' && c <= '9');
    ok |= (c == '/');
    ok |= (c == '_');
    ok |= (c == '-');
    return ok;
}

// As above, plus '.', which is kept through the normalize copy pass so the extension survives long
// enough to be stripped as a unit.
bool IsAllowedDuringNormalize(char c) {
    bool ok = IsCanonicalChar(c);
    ok |= (c == '.');
    return ok;
}

}  // namespace asset_private

StringView ToString(EAssetType type) {
    switch (type) {
        case EAssetType::Invalid: return "invalid"sv;
        case EAssetType::Texture: return "texture"sv;
        case EAssetType::Spritesheet: return "spritesheet"sv;
        case EAssetType::COUNT: break;
    }
    ASSERT(false);
    return "invalid"sv;
}

EAssetType AssetTypeFromString(StringView str) {
    if (str == "texture"sv) {
        return EAssetType::Texture;
    }
    if (str == "spritesheet"sv) {
        return EAssetType::Spritesheet;
    }
    return EAssetType::Invalid;
}

AssetId AssetId::Normalize(StringView raw) {
    using namespace asset_private;

    // Pass 1: lowercase, '\'->'/', ' '->'_', drop disallowed chars, collapse repeated slashes and
    // never emit a leading slash.
    char buffer[512] = {};
    i32 len = 0;
    for (u64 i = 0; i < raw.Size; ++i) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c == '\\') {
            c = '/';
        }
        if (c == ' ') {
            c = '_';
        }
        if (!IsAllowedDuringNormalize(c)) {
            continue;
        }
        if (c == '/' && len == 0) {
            continue;
        }
        if (c == '/' && buffer[len - 1] == '/') {
            continue;
        }
        if (len >= (i32)sizeof(buffer) - 1) {
            break;
        }
        buffer[len] = c;
        len++;
    }
    if (len > 0 && buffer[len - 1] == '/') {
        len--;
    }
    buffer[len] = '\0';

    // Strip a leading "assets/" (a full assets-relative path is accepted) and the extension via the
    // shared dali/core path utilities rather than hand-rolled parsing. Both take an arena, hence the
    // scratch lease.
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView work = StringView(buffer, (u64)len);
    work = RemovePrefix(arena, work, "assets/"sv);
    work = paths::RemoveExtension(arena, work);

    AssetId id = {};
    id.Value.Set(work);
    return id;
}

bool AssetId::IsValid() const {
    using namespace asset_private;

    StringView s = Value.ToString();
    if (s.IsEmpty()) {
        return false;
    }
    if (s[0] == '/') {
        return false;
    }
    if (s[s.Size - 1] == '/') {
        return false;
    }
    for (u64 i = 0; i < s.Size; ++i) {
        if (!IsCanonicalChar(s[i])) {
            return false;
        }
    }
    return true;
}

StringView GetAssetsRoot(Arena* arena) {
    StringView base = paths::GetBaseDir(arena);
    return paths::PathJoin(arena, base, "assets"sv);
}

StringView AssetYmlPath(Arena* arena, AssetId id) {
    return Printf(arena, "%s/%s.yml", GetAssetsRoot(arena).Str(), id.Value.Str());
}

StringView AssetPayloadPath(Arena* arena, AssetId id) {
    return Printf(arena, "%s/%s.asset", GetAssetsRoot(arena).Str(), id.Value.Str());
}

bool PeekManifest(AssetId id, AssetManifest* out) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    FileContents contents = ReadFile(arena, AssetYmlPath(arena, id));
    if (!contents.IsValid()) {
        return false;
    }
    YAML::Node node = YAML::Load((const char*)contents.Data.data());

    // We build without exceptions: as<T>(fallback) never throws on a missing/mistyped field.
    out->Type = AssetTypeFromString(StringView(node["type"].as<std::string>("invalid").c_str()));
    out->Version = node["version"].as<int>(0);
    out->Id = id;
    return true;
}

}  // namespace kdk
