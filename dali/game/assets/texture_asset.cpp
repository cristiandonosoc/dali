#include <dali/game/assets/texture_asset.h>

#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/file.h>
#include <dali/game/log.h>

#include <stb/stb_image.h>
#include <yaml-cpp/yaml.h>

namespace kdk {

namespace texture_asset_private {

// Emits the texture manifest to disk (creating parent directories). Shared by Import and
// SaveManifest so the schema lives in one place.
bool WriteManifest(Arena* arena,
                   AssetId id,
                   StringView source,
                   i32 width,
                   i32 height,
                   i32 channels,
                   ETextureFilter filter,
                   bool flip) {
    StringView yml_path = AssetYmlPath(arena, id);

    YAML::Emitter emit;
    emit << YAML::BeginMap;
    emit << YAML::Key << "type" << YAML::Value << ToString(TextureAsset::kAssetType).Str();
    emit << YAML::Key << "version" << YAML::Value << TextureAsset::kVersion;
    emit << YAML::Key << "id" << YAML::Value << id.Value.Str();
    emit << YAML::Key << "source" << YAML::Value << source.Str();
    emit << YAML::Key << "width" << YAML::Value << width;
    emit << YAML::Key << "height" << YAML::Value << height;
    emit << YAML::Key << "channels" << YAML::Value << channels;
    emit << YAML::Key << "flip" << YAML::Value << (flip ? 1 : 0);
    emit << YAML::Key << "filter" << YAML::Value << (i32)filter;
    emit << YAML::EndMap;

    if (!WriteFile(yml_path, std::span<const u8>((const u8*)emit.c_str(), emit.size()))) {
        LogError("TextureAsset: cannot write manifest '%s'", yml_path.Str());
        return false;
    }
    return true;
}

}  // namespace texture_asset_private

bool TextureAsset::Import(StringView source_raw,
                          AssetId id,
                          const TextureImportSettings& settings,
                          ETextureFilter filter) {
    using namespace texture_asset_private;

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

    if (!WriteManifest(arena, id, source_raw, width, height, channels, filter, settings.FlipVertically)) {
        return false;
    }

    LogInfo("TextureAsset: imported '%s' (%dx%d, %d ch)", id.Value.Str(), width, height, channels);
    return true;
}

std::optional<TextureAsset> TextureAsset::LoadFromDisk(AssetId id) {
    using namespace texture_asset_private;

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    FileContents yml = ReadFile(arena, AssetYmlPath(arena, id));
    if (!yml.IsValid()) {
        return std::nullopt;
    }
    YAML::Node node = YAML::Load((const char*)yml.Data.data());

    // We build without exceptions: as<T>(fallback) never throws on a missing/mistyped field.
    EAssetType type = AssetTypeFromString(StringView(node["type"].as<std::string>("invalid").c_str()));
    if (type != kAssetType) {
        return std::nullopt;  // Not our kind; another loader may claim it.
    }

    i32 version = node["version"].as<int>(0);
    if (version != kVersion) {
        LogError("TextureAsset: '%s' is version %d, expected %d - re-import needed",
                 id.Value.Str(),
                 version,
                 kVersion);
        return std::nullopt;
    }

    // The manifest embeds its own id; it must match the id derived from the file's path.
    AssetId stored = AssetId::Normalize(StringView(node["id"].as<std::string>("").c_str()));
    if (!(stored == id)) {
        LogError("TextureAsset: id mismatch - manifest says '%s' but lives at '%s'",
                 stored.Value.Str(),
                 id.Value.Str());
    }

    // Dimensions/channels/filter describe the payload and become the Resource's own state; they are
    // read into locals here rather than kept as separate asset members.
    i32 width = node["width"].as<int>(0);
    i32 height = node["height"].as<int>(0);
    i32 channels = node["channels"].as<int>(0);
    ETextureFilter filter = (ETextureFilter)node["filter"].as<int>(0);

    TextureAsset asset = {};
    asset.Manifest.Type = kAssetType;
    asset.Manifest.Version = version;
    asset.Manifest.Id = id;
    asset.Manifest.Source = StringView(node["source"].as<std::string>("").c_str());
    asset.Manifest.HasPayload = true;
    asset.Settings.FlipVertically = node["flip"].as<int>(0) != 0;

    StringView asset_path = AssetPayloadPath(arena, id);
    FileContents payload = ReadFile(arena, asset_path);
    if (!payload.IsValid()) {
        LogError("TextureAsset: missing payload '%s'", asset_path.Str());
        return std::nullopt;
    }

    asset.Resource = Texture::FromPixels(payload.Data, width, height, channels, filter);
    if (!asset.Resource.IsValid()) {
        return std::nullopt;
    }
    return asset;
}

bool TextureAsset::SaveManifest() const {
    using namespace texture_asset_private;

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    return WriteManifest(arena,
                         Manifest.Id,
                         Manifest.Source.ToString(),
                         Resource.Width,
                         Resource.Height,
                         Resource.Channels,
                         Resource.Filter,
                         Settings.FlipVertically);
}

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
