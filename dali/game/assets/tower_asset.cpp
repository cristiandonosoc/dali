#include <dali/game/assets/tower_asset.h>

#include <dali/core/memory.h>
#include <dali/game/platform.h>

#include <yaml-cpp/yaml.h>

namespace kdk {

namespace tower_asset_private {

// The InstanceData block serializes as a named sub-map ("instance:"), mirroring the struct nesting
// (same shape as EnemyAsset). Emit and parse live together so the schema stays in one place.
void EmitInstanceData(YAML::Emitter& emit, const TowerAsset::InstanceData& data) {
    emit << YAML::Key << "instance" << YAML::Value << YAML::BeginMap;
    emit << YAML::Key << "range" << YAML::Value << data.Range;
    emit << YAML::Key << "fire_interval" << YAML::Value << data.FireInterval;
    emit << YAML::Key << "damage" << YAML::Value << data.Damage;
    emit << YAML::Key << "projectile_hit_radius" << YAML::Value << data.ProjectileHitRadius;
    emit << YAML::Key << "cost" << YAML::Value << data.Cost;
    emit << YAML::EndMap;
}

TowerAsset::InstanceData ParseInstanceData(const YAML::Node& node) {
    TowerAsset::InstanceData data = {};  // defaults stand in for any missing field
    YAML::Node inst = node["instance"];
    if (!inst) {
        return data;
    }
    data.Range = inst["range"].as<float>(data.Range);
    data.FireInterval = inst["fire_interval"].as<float>(data.FireInterval);
    data.Damage = inst["damage"].as<float>(data.Damage);
    data.ProjectileHitRadius = inst["projectile_hit_radius"].as<float>(data.ProjectileHitRadius);
    data.Cost = inst["cost"].as<int>(data.Cost);
    return data;
}

// The idle animation serializes as an "idle:" sub-map ({sprite_sheet, clip, flip_x}). The clip is
// looked up live at draw via SpriteSheetClipReference::Resolve, so nothing here needs relinking. The
// whole block is omitted when no sheet is set.
void EmitIdleClip(YAML::Emitter& emit, const SpriteSheetClipReference& clip) {
    if (!clip.SpriteSheetId.IsValid()) {
        return;
    }
    emit << YAML::Key << "idle" << YAML::Value << YAML::BeginMap;
    emit << YAML::Key << "sprite_sheet" << YAML::Value << clip.SpriteSheetId.Value.Str();
    emit << YAML::Key << "clip" << YAML::Value << clip.ClipName.Str();
    emit << YAML::Key << "flip_x" << YAML::Value << clip.FlipX;
    emit << YAML::EndMap;
}

SpriteSheetClipReference ParseIdleClip(const YAML::Node& node) {
    SpriteSheetClipReference clip = {};
    YAML::Node idle = node["idle"];
    if (!idle) {
        return clip;
    }
    clip.SpriteSheetId =
        AssetId::Normalize(StringView(idle["sprite_sheet"].as<std::string>("").c_str()));
    clip.ClipName = StringView(idle["clip"].as<std::string>("").c_str());
    clip.FlipX = idle["flip_x"].as<bool>(false);  // optional; sheets authored before flip default off
    return clip;
}

}  // namespace tower_asset_private

bool TowerAsset::Create(AssetId id) {
    if (!id.IsValid()) {
        LogError("TowerAsset::Create: non-canonical id '%s'", id.Value.Str());
        return false;
    }

    TowerAsset asset = {};
    asset.Manifest.Type = kAssetType;
    asset.Manifest.Version = kVersion;
    asset.Manifest.Id = id;
    asset.Manifest.HasPayload = false;
    return asset.SaveManifest();
}

std::optional<TowerAsset> TowerAsset::LoadFromDisk(AssetId id) {
    using namespace tower_asset_private;

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    FileContents contents = ReadFile(arena, AssetYmlPath(arena, id));
    if (!contents.IsValid()) {
        return std::nullopt;
    }
    YAML::Node node = YAML::Load((const char*)contents.Data.data());

    EAssetType type =
        AssetTypeFromString(StringView(node["type"].as<std::string>("invalid").c_str()));
    if (type != kAssetType) {
        return std::nullopt;
    }

    i32 version = node["version"].as<int>(0);
    if (version != kVersion) {
        LogError("TowerAsset: '%s' is version %d, expected %d - re-author needed",
                 id.Value.Str(),
                 version,
                 kVersion);
        return std::nullopt;
    }

    AssetId stored = AssetId::Normalize(StringView(node["id"].as<std::string>("").c_str()));
    if (!(stored == id)) {
        LogError("TowerAsset: id mismatch - manifest says '%s' but lives at '%s'",
                 stored.Value.Str(),
                 id.Value.Str());
    }

    TowerAsset asset = {};
    asset.Manifest.Type = kAssetType;
    asset.Manifest.Version = version;
    asset.Manifest.Id = id;
    asset.Manifest.HasPayload = false;
    asset.PerInstanceData = ParseInstanceData(node);
    asset.PerInstanceData.IdleClip = ParseIdleClip(node);
    return asset;
}

bool TowerAsset::SaveManifest() const {
    using namespace tower_asset_private;

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    StringView yml_path = AssetYmlPath(arena, Manifest.Id);

    YAML::Emitter emit;
    emit << YAML::BeginMap;
    emit << YAML::Key << "type" << YAML::Value << ToString(kAssetType).Str();
    emit << YAML::Key << "version" << YAML::Value << kVersion;
    emit << YAML::Key << "id" << YAML::Value << Manifest.Id.Value.Str();
    EmitInstanceData(emit, PerInstanceData);
    EmitIdleClip(emit, PerInstanceData.IdleClip);
    emit << YAML::EndMap;

    if (!WriteFile(yml_path, std::span<const u8>((const u8*)emit.c_str(), emit.size()))) {
        LogError("TowerAsset: cannot write manifest '%s'", yml_path.Str());
        return false;
    }
    return true;
}

bool TowerAsset::ResolveReferences(AssetRegistry&) {
    // Nothing to link: IdleClip is a SpriteSheetClipReference resolved live at draw time, not a
    // cached pointer. Present because the registry calls ResolveReferences on every asset type
    // uniformly (X-macro), so each type must provide it even when it is a no-op.
    return true;
}

}  // namespace kdk
