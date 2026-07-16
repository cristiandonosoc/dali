#include <dali/game/assets/texture_asset.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/platform.h>
#include <dali/game/serde.h>

#include <stb/stb_image.h>

namespace kdk {

void TextureImportSettings::Serialize(SerdeArchive* sa) { SERDE(sa, this, FlipVertically); }

void TextureAsset::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Manifest);
    SERDE(sa, this, Settings);
    // Dimensions/channels/filter describe the payload and ARE the Resource's own state - the asset
    // keeps no second copy. Serializing them in place means saving reads the live GL object, and
    // loading leaves them sitting in a blank Resource for LoadFromDisk to hand to FromPixels. Handle
    // is deliberately absent: it is a GL name, meaningless across runs.
    Serde(sa, "Width", &Resource.Width);
    Serde(sa, "Height", &Resource.Height);
    Serde(sa, "Channels", &Resource.Channels);
    Serde(sa, "Filter", &Resource.Filter);
}

bool TextureAsset::Import(StringView source_raw,
                          AssetId id,
                          const TextureImportSettings& settings,
                          ETextureFilter filter) {
    if (!id.IsValid()) {
        LogError("TextureAsset::Import: non-canonical id '%s'", id.Value.Str());
        return false;
    }

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    // Accept an absolute source as well as a working-dir-relative one (Browse relativizes, but a
    // path could still be pasted in absolute).
    StringView source_abs = source_raw;
    if (!paths::IsAbsolute(source_raw)) {
        source_abs = paths::PathJoin(arena, paths::GetBaseDir(arena), source_raw);
    }

    stbi_set_flip_vertically_on_load(settings.FlipVertically);
    i32 width = 0;
    i32 height = 0;
    i32 channels = 0;
    u8* data = stbi_load(source_abs.Str(), &width, &height, &channels, 0);
    if (!data) {
        LogError("TextureAsset::Import: cannot read source '%s'", source_abs.Str());
        return false;
    }
    DEFER { stbi_image_free(data); };

    // Write the payload: tightly-packed pixels, exactly width*height*channels bytes.
    StringView asset_path = AssetPayloadPath(arena, id);
    std::span<const u8> pixels(data, (size_t)(width * height * channels));
    if (!WriteFile(asset_path, pixels)) {
        LogError("TextureAsset::Import: cannot write payload '%s'", asset_path.Str());
        return false;
    }

    // The manifest is written through the same path as any other save, so an imported .yml and a
    // re-saved one cannot disagree. Resource stands in as the dims carrier here; nothing is uploaded.
    TextureAsset asset = {};
    asset.Manifest.Type = kAssetType;
    asset.Manifest.Version = kVersion;
    asset.Manifest.Id = id;
    asset.Manifest.Source.Set(source_raw);
    asset.Manifest.HasPayload = kHasPayload;
    asset.Settings = settings;
    asset.Resource.Width = width;
    asset.Resource.Height = height;
    asset.Resource.Channels = channels;
    asset.Resource.Filter = filter;
    if (!asset.SaveManifest()) {
        return false;
    }

    LogInfo("TextureAsset: imported '%s' (%dx%d, %d ch)", id.Value.Str(), width, height, channels);
    return true;
}

std::optional<TextureAsset> TextureAsset::LoadFromDisk(AssetId id) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    std::optional<TextureAsset> asset = LoadAssetFromDisk<TextureAsset>(id);
    if (!asset) {
        return std::nullopt;
    }

    // The envelope stops at the manifest; only the type that HAS a payload knows how to finish. The
    // dims Serialize left in Resource say how to read the pixels back.
    StringView asset_path = AssetPayloadPath(arena, id);
    FileContents payload = ReadFile(arena, asset_path);
    if (!payload.IsValid()) {
        LogError("TextureAsset: missing payload '%s'", asset_path.Str());
        return std::nullopt;
    }

    Texture info = asset->Resource;
    asset->Resource =
        Texture::FromPixels(payload.Data, info.Width, info.Height, info.Channels, info.Filter);
    if (!asset->Resource.IsValid()) {
        return std::nullopt;
    }
    return asset;
}

bool TextureAsset::SaveManifest() { return SaveAssetManifest(this); }

bool TextureAsset::Reimport() {
    if (Manifest.Source.IsEmpty()) {
        LogError("TextureAsset::Reimport: '%s' has no source", Manifest.Id.Value.Str());
        return false;
    }
    if (!Import(Manifest.Source.ToString(), Manifest.Id, Settings, Resource.Filter)) {
        return false;
    }
    std::optional<TextureAsset> reloaded = LoadFromDisk(Manifest.Id);
    if (!reloaded) {
        return false;
    }
    Resource.Destroy();
    *this = *reloaded;
    return true;
}

}  // namespace kdk
