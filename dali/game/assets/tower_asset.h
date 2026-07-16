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
    static constexpr bool kHasPayload = false;  // Composing asset: yml only.

    AssetManifest Manifest = {};

    // The stats copied verbatim from a TowerAsset (the blueprint) onto a placed Tower. This struct
    // IS the placement-snapshot boundary: every field here is snapshotted, so the idle-clip
    // reference travels with each placed tower (resolved live at draw, never a cached pointer).
    // Applying a blueprint is a single memberwise copy (tower.Data = asset.PerInstanceData), so a
    // new stat is added in exactly one place and cannot be forgotten. Live per-tower state (e.g.
    // FireCooldown) does NOT belong here — it lives on Tower.
    struct InstanceData {
        float Range = 180.0f;        // world units; at kHexSize=60 a range of 180 reaches ~3 tiles
        float FireInterval = 0.6f;   // seconds between shots; Tower::FireCooldown counts this down
        float Damage = 5.0f;         // per projectile; enemies start at 10 HP
        float ProjectileHitRadius = 8.0f;  // distance at which this tower's shots connect

        int Cost = 40;  // gold to place one

        SpriteSheetClipReference IdleClip = {};

        void Serialize(SerdeArchive* sa);
    };
    InstanceData PerInstanceData = {};

    // Writes the manifest for a new blueprint with default stats. Overwrites any existing.
    static bool Create(AssetId id);
    // Reads the manifest at |id|. nullopt if it isn't a tower or it was written by a newer build.
    static std::optional<TowerAsset> LoadFromDisk(AssetId id);
    bool SaveManifest();

    void Serialize(SerdeArchive* sa);

    bool ResolveReferences(AssetRegistry& registry);
};

}  // namespace kdk
