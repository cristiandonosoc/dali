#include <dali/game/assets/tower_asset.h>

#include <dali/core/memory.h>
#include <dali/game/platform.h>
#include <dali/game/serde.h>

namespace kdk {

void TowerAsset::InstanceData::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Range);
    SERDE(sa, this, FireInterval);
    SERDE(sa, this, Damage);
    SERDE(sa, this, ProjectileHitRadius);
    SERDE(sa, this, Cost);
    // Composes SpriteSheetClipReference's own encoding, so FlipX (and every future per-use knob)
    // comes along without this file mentioning it.
    SERDE(sa, this, IdleClip);
}

void TowerAsset::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Manifest);
    SERDE(sa, this, PerInstanceData);
}

bool TowerAsset::Create(AssetId id) { return CreateAsset<TowerAsset>(id); }

std::optional<TowerAsset> TowerAsset::LoadFromDisk(AssetId id) {
    return LoadAssetFromDisk<TowerAsset>(id);
}

bool TowerAsset::SaveManifest() { return SaveAssetManifest(this); }

bool TowerAsset::ResolveReferences(AssetRegistry&) {
    // Nothing to link: IdleClip is a SpriteSheetClipReference resolved live at draw time, not a
    // cached pointer. Present because the registry calls ResolveReferences on every asset type
    // uniformly (X-macro), so each type must provide it even when it is a no-op.
    return true;
}

}  // namespace kdk
