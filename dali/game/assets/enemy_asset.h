#pragma once

#include <dali/core/array.h>
#include <dali/core/color.h>
#include <dali/game/assets/asset.h>
#include <dali/game/assets/spritesheet_asset.h>

#include <optional>

namespace kdk {

struct AssetRegistry;

// Movement facing, quantized to four directions on a 45 degree cutoff. Default 0 == Down, so a
// fresh or stationary enemy faces the camera. Values index WalkClips::ByFacing directly.
enum class EFacing : u8 {
    Down = 0,
    Up,
    Left,
    Right,
    COUNT
};

// Maps a movement vector to a facing. World Y grows downward (screen convention, see
// WorldToScreen), so a positive y is Down. The 45 degree cutoff is a magnitude compare; a tie at
// exactly 45 degrees resolves to the horizontal facing.
EFacing FacingFromDir(Vec2 dir);

// An enemy's walk animation, one clip reference per facing. A slot may be left unset; drawing a
// facing whose slot is unset asserts at the draw site (its clip reference resolves to null). No
// fallback by design. Each reference carries its own FlipX, so a single side-view clip can serve
// both Left and Right by pointing both slots at it and flipping one.
struct WalkClips {
    Array<SpriteSheetClipReference, (i32)EFacing::COUNT> ByFacing = {};

    const SpriteSheetClipReference& Resolve(EFacing facing) const { return ByFacing[(i32)facing]; }
};

// The design-time definition of one enemy type ("goblin", "wolf") — a lightweight CDO. Pure
// metadata (yml-only, no payload). SpawnEnemy stamps a runtime Enemy from Data; editing a blueprint
// never mutates enemies already spawned, because Data is snapshotted (copied) at spawn.
struct EnemyAsset {
    static constexpr EAssetType kAssetType = EAssetType::Enemy;
    static constexpr i32 kVersion = 2;
    static constexpr StringView kIdRoot = "enemies"sv;

    AssetManifest Manifest = {};

    // The stats copied verbatim from an EnemyAsset (the blueprint) onto a runtime Enemy at spawn.
    // This struct IS the spawn-snapshot boundary: every field here is snapshotted, so the walk-clip
    // reference travels with each spawned enemy (resolved live at draw, never a cached pointer).
    // Applying a blueprint is a single memberwise copy (enemy.Data = asset.Data), so a new stat is
    // added in exactly one place and cannot be forgotten.
    struct InstanceData {
        float Speed = 100.0f;     // px/s
        float MaxHealth = 10.0f;  // health at spawn; the live pool (Enemy::Health) starts here
        float Damage = 5.0f;      // HP drained from the base on breach
        i32 Reward = 5;           // gold granted when a tower kills it
        Color32 Color = Color32::OrangeRed;  // draw color / sprite tint
        WalkClips Walk = {};  // per-facing walk animation; resolved against the registry
    };
    InstanceData PerInstanceData = {};

    // Writes the manifest for a new blueprint with default stats. Overwrites any existing.
    static bool Create(AssetId id);
    // Reads the manifest at |id|. nullopt if it isn't an enemy or the version mismatches.
    static std::optional<EnemyAsset> LoadFromDisk(AssetId id);

    bool SaveManifest() const;

    bool ResolveReferences(AssetRegistry&) { return true; }  // no-op
};

}  // namespace kdk
