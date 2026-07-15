#pragma once

#include <dali/game/assets/asset.h>
#include <dali/game/assets/spritesheet_asset.h>

#include <optional>

namespace kdk {

struct AssetRegistry;

struct TowerAsset {
    static constexpr EAssetType kAssetType = EAssetType::Tower;
    static constexpr i32 kVersion = 1;
    static constexpr StringView kIdRoot = "towers"sv;

    AssetManifest Manifest = {};

    struct InstanceData {
        float Range = 180.0f;
        float FireInterval = 2.0f;
        float Damage = 5.0f;
        float ProjectileHitRadius = 8.0f;

        int Cost = 40;

        SpriteSheetClipReference IdleClip = {};
    };
    InstanceData PerInstanceData = {};

    // Writes the manifest for a new blueprint with default stats. Overwrites any existing.
    static bool Create(AssetId id);
    // Reads the manifest at |id|. nullopt if it isn't an enemy or the version mismatches.
    static std::optional<TowerAsset> LoadFromDisk(AssetId id);
    bool SaveManifest() const;

    bool ResolveReferences(AssetRegistry& registry);
};

}  // namespace kdk
