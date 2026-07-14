#include <dali/game/assets/enemy_asset.h>

#include <dali/core/memory.h>
#include <dali/game/assets/registry.h>
#include <dali/game/file.h>
#include <dali/game/log.h>

#include <yaml-cpp/yaml.h>

#include <cstdio>

namespace kdk {

namespace enemy_asset_private {

// Color is stored as one "rrggbbaa" hex scalar (channels left-to-right), rather than a {r,g,b,a}
// sub-map: compact and the usual way a color is authored. Byte order is written out explicitly so
// it does not depend on Color32's union endianness.
i32 HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Parses "rrggbbaa" (an optional '#' or "0x" prefix is tolerated) into |out|. Leaves |out|
// untouched and returns false unless the string is exactly 8 hex digits.
bool ParseColorHex(StringView s, Color32* out) {
    u64 start = 0;
    if (s.Size >= 1) {
        if (s[0] == '#') {
            start = 1;
        }
    }
    if (s.Size >= 2) {
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                start = 2;
            }
        }
    }
    if (s.Size - start != 8) {
        return false;
    }

    u32 value = 0;
    for (u64 i = start; i < s.Size; ++i) {
        i32 digit = HexDigit(s[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (u32)digit;
    }
    out->R = (u8)((value >> 24) & 0xffu);
    out->G = (u8)((value >> 16) & 0xffu);
    out->B = (u8)((value >> 8) & 0xffu);
    out->A = (u8)(value & 0xffu);
    return true;
}

// The InstanceData block serializes as a named sub-map ("instance:"), mirroring the struct nesting
// the same way SpriteGrid serializes as "grid:". Emit and parse live together so the schema is one
// place.
void EmitInstanceData(YAML::Emitter& emit, const EnemyAsset::InstanceData& data) {
    emit << YAML::Key << "instance" << YAML::Value << YAML::BeginMap;
    emit << YAML::Key << "speed" << YAML::Value << data.Speed;
    emit << YAML::Key << "max_health" << YAML::Value << data.MaxHealth;
    emit << YAML::Key << "damage" << YAML::Value << data.Damage;
    emit << YAML::Key << "reward" << YAML::Value << data.Reward;
    char color_hex[9] = {};
    snprintf(color_hex,
             sizeof(color_hex),
             "%02x%02x%02x%02x",
             data.Color.R,
             data.Color.G,
             data.Color.B,
             data.Color.A);
    emit << YAML::Key << "color" << YAML::Value << YAML::DoubleQuoted << color_hex;
    emit << YAML::EndMap;
}

EnemyAsset::InstanceData ParseInstanceData(const YAML::Node& node) {
    EnemyAsset::InstanceData data = {};  // defaults stand in for any missing field
    YAML::Node inst = node["instance"];
    if (!inst) {
        return data;
    }
    data.Speed = inst["speed"].as<float>(data.Speed);
    data.MaxHealth = inst["max_health"].as<float>(data.MaxHealth);
    data.Damage = inst["damage"].as<float>(data.Damage);
    data.Reward = inst["reward"].as<int>(data.Reward);
    if (YAML::Node color = inst["color"]) {
        // On a malformed value ParseColorHex leaves data.Color at its default rather than failing
        // the whole load.
        ParseColorHex(StringView(color.as<std::string>("").c_str()), &data.Color);
    }
    return data;
}

// The yml key for each facing's slot in the "walk:" map. Parallel to EFacing; index with (i32)facing.
constexpr StringView kFacingKeys[(i32)EFacing::COUNT] = {
    "down"sv,
    "up"sv,
    "left"sv,
    "right"sv,
};

// The walk animation serializes as a "walk:" map with one sub-block per facing that has a clip set
// ({sprite_sheet, clip, flip_x}). Each clip is looked up live at draw via
// SpriteSheetClipReference::Resolve, so nothing here needs relinking. Slots with no sheet — and the
// whole block when every slot is empty — are omitted.
void EmitWalkClips(YAML::Emitter& emit, const WalkClips& walk) {
    bool any = false;
    for (const DirectionalClip& slot : walk.ByFacing) {
        any |= slot.Clip.SpriteSheetId.IsValid();
    }
    if (!any) {
        return;
    }

    emit << YAML::Key << "walk" << YAML::Value << YAML::BeginMap;
    for (i32 i = 0; i < (i32)EFacing::COUNT; ++i) {
        const DirectionalClip& slot = walk.ByFacing[i];
        if (!slot.Clip.SpriteSheetId.IsValid()) {
            continue;
        }
        emit << YAML::Key << kFacingKeys[i].Str() << YAML::Value << YAML::BeginMap;
        emit << YAML::Key << "sprite_sheet" << YAML::Value << slot.Clip.SpriteSheetId.Value.Str();
        emit << YAML::Key << "clip" << YAML::Value << slot.Clip.ClipName.Str();
        if (slot.FlipX) {
            emit << YAML::Key << "flip_x" << YAML::Value << true;
        }
        emit << YAML::EndMap;
    }
    emit << YAML::EndMap;
}

WalkClips ParseWalkClips(const YAML::Node& node) {
    WalkClips walk = {};
    YAML::Node walk_node = node["walk"];
    if (!walk_node) {
        return walk;
    }
    for (i32 i = 0; i < (i32)EFacing::COUNT; ++i) {
        YAML::Node slot_node = walk_node[kFacingKeys[i].Str()];
        if (!slot_node) {
            continue;
        }
        DirectionalClip& slot = walk.ByFacing[i];
        slot.Clip.SpriteSheetId =
            AssetId::Normalize(StringView(slot_node["sprite_sheet"].as<std::string>("").c_str()));
        slot.Clip.ClipName = StringView(slot_node["clip"].as<std::string>("").c_str());
        slot.FlipX = slot_node["flip_x"].as<bool>(false);
    }
    return walk;
}

}  // namespace enemy_asset_private

EFacing FacingFromDir(Vec2 dir) {
    if (Abs(dir.x) >= Abs(dir.y)) {
        if (dir.x >= 0.0f) {
            return EFacing::Right;
        }
        return EFacing::Left;
    }
    if (dir.y >= 0.0f) {
        return EFacing::Down;
    }
    return EFacing::Up;
}

bool EnemyAsset::Create(AssetId id) {
    if (!id.IsValid()) {
        LogError("EnemyAsset::Create: non-canonical id '%s'", id.Value.Str());
        return false;
    }

    EnemyAsset asset = {};
    asset.Manifest.Type = kAssetType;
    asset.Manifest.Version = kVersion;
    asset.Manifest.Id = id;
    asset.Manifest.HasPayload = false;
    return asset.SaveManifest();
}

std::optional<EnemyAsset> EnemyAsset::LoadFromDisk(AssetId id) {
    using namespace enemy_asset_private;

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
        LogError("EnemyAsset: '%s' is version %d, expected %d - re-author needed",
                 id.Value.Str(),
                 version,
                 kVersion);
        return std::nullopt;
    }

    AssetId stored = AssetId::Normalize(StringView(node["id"].as<std::string>("").c_str()));
    if (!(stored == id)) {
        LogError("EnemyAsset: id mismatch - manifest says '%s' but lives at '%s'",
                 stored.Value.Str(),
                 id.Value.Str());
    }

    EnemyAsset asset = {};
    asset.Manifest.Type = kAssetType;
    asset.Manifest.Version = version;
    asset.Manifest.Id = id;
    asset.Manifest.HasPayload = false;
    asset.PerInstanceData = ParseInstanceData(node);
    asset.PerInstanceData.Walk = ParseWalkClips(node);
    return asset;
}

bool EnemyAsset::SaveManifest() const {
    using namespace enemy_asset_private;

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    StringView yml_path = AssetYmlPath(arena, Manifest.Id);

    YAML::Emitter emit;
    emit << YAML::BeginMap;
    emit << YAML::Key << "type" << YAML::Value << ToString(kAssetType).Str();
    emit << YAML::Key << "version" << YAML::Value << kVersion;
    emit << YAML::Key << "id" << YAML::Value << Manifest.Id.Value.Str();
    EmitInstanceData(emit, PerInstanceData);
    EmitWalkClips(emit, PerInstanceData.Walk);
    emit << YAML::EndMap;

    if (!WriteFile(yml_path, std::span<const u8>((const u8*)emit.c_str(), emit.size()))) {
        LogError("EnemyAsset: cannot write manifest '%s'", yml_path.Str());
        return false;
    }
    return true;
}

}  // namespace kdk
