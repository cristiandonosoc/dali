#include <dali/game/assets/enemy_asset.h>

#include <dali/core/memory.h>
#include <dali/game/assets/registry.h>
#include <dali/game/platform.h>
#include <dali/game/serde.h>

namespace kdk {

namespace enemy_asset_private {

// The yml key for each facing's slot in the "Walk" map. Parallel to EFacing; index with (i32)facing.
constexpr const char* kFacingKeys[(i32)EFacing::COUNT] = {
    "Down",
    "Up",
    "Left",
    "Right",
};

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

void WalkClips::Serialize(SerdeArchive* sa) {
    using namespace enemy_asset_private;

    for (i32 i = 0; i < (i32)EFacing::COUNT; ++i) {
        Serde(sa, kFacingKeys[i], &ByFacing[i]);
    }
}

void EnemyAsset::InstanceData::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Speed);
    SERDE(sa, this, MaxHealth);
    SERDE(sa, this, Damage);
    SERDE(sa, this, Reward);
    SERDE(sa, this, Color);
    SERDE(sa, this, Walk);
}

void EnemyAsset::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Manifest);
    SERDE(sa, this, PerInstanceData);
}

bool EnemyAsset::Create(AssetId id) { return CreateAsset<EnemyAsset>(id); }

std::optional<EnemyAsset> EnemyAsset::LoadFromDisk(AssetId id) {
    return LoadAssetFromDisk<EnemyAsset>(id);
}

bool EnemyAsset::SaveManifest() { return SaveAssetManifest(this); }

}  // namespace kdk
