#pragma once

#include <dali/core/color.h>
#include <dali/game/assets/asset.h>

#include <optional>

namespace kdk {

// The stats copied verbatim from an EnemyAsset (the blueprint) onto a runtime Enemy at spawn. This
// struct IS the spawn-snapshot boundary: a field here is snapshotted; anything on EnemyAsset outside
// it (references, resolved pointers) is not. Applying a blueprint is a single memberwise copy
// (enemy.Data = asset.Data), so a new stat is added in exactly one place and cannot be forgotten.
struct InstanceData {
    float Speed = 100.0f;      // px/s
    float MaxHealth = 10.0f;   // health at spawn; the live pool (Enemy::Health) starts here
    float Damage = 5.0f;       // HP drained from the base on breach
    i32 Reward = 5;            // gold granted when a tower kills it
    Color32 Color = Color32::OrangeRed;  // draw color (until a sprite exists)
};

// The design-time definition of one enemy type ("goblin", "wolf") — a lightweight CDO. Pure metadata
// (yml-only, no payload). SpawnEnemy stamps a runtime Enemy from Data; editing a blueprint never
// mutates enemies already spawned, because Data is snapshotted (copied) at spawn.
struct EnemyAsset {
    static constexpr EAssetType kAssetType = EAssetType::Enemy;
    static constexpr i32 kVersion = 1;
    static constexpr StringView kIdRoot = "enemies"sv;

    AssetManifest Manifest = {};
    InstanceData Data = {};

    // Writes the manifest for a new blueprint with default stats. Overwrites any existing.
    static bool Create(AssetId id);
    // Reads the manifest at |id|. nullopt if it isn't an enemy or the version mismatches.
    static std::optional<EnemyAsset> LoadFromDisk(AssetId id);
    bool SaveManifest() const;
};

}  // namespace kdk
