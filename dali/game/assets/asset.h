#pragma once

#include <dali/core/memory.h>
#include <dali/core/string.h>

#include <optional>

namespace kdk {

struct Arena;
// Only ever passed around by pointer/reference here, so the yaml-cpp-heavy serde.h stays out of the
// asset headers (and therefore out of game.h, which reaches these through registry.h). Every
// `Serialize` member is DEFINED in a .cpp, which is where serde.h gets included.
struct SerdeArchive;

// FORMAT: ASSET_NAME, MAX_QUANTITY,
#define XASSET_TYPES(X) \
    X(Texture, 256)     \
    X(SpriteSheet, 256) \
    X(Enemy, 256)       \
    X(Tower, 256)

#define X(ASSET_NAME, ...) ASSET_NAME,

enum class EAssetType : u8 {
    Invalid = 0,
    XASSET_TYPES(X)

    COUNT,
};

#undef X

// The lowercase token used in a manifest's `type:` field ("texture").
StringView ToString(EAssetType type);
EAssetType AssetTypeFromString(StringView str);

// The id root for an asset type ("textures", "spritesheets", "enemies"), empty for Invalid/COUNT.
// Same value each type declares as its kIdRoot, but keyed by the enum so display/entry code can map
// a type to its root without the concrete struct. (Keep in sync with the structs' kIdRoot.)
StringView IdRootForType(EAssetType type);

// A canonical asset identifier: lowercase, no spaces, forward-slash separated, no extension, rooted
// at the assets/ directory (e.g. "textures/goblin/walk"). It is the reference string, the registry
// key, and the on-disk stem all at once. Stored inline (FixedString) so the registry is a
// self-contained value blob with no pointers to keep alive across reloads.
struct AssetId {
    FixedString<128> Value = {};

    // Coerces arbitrary input into canonical form: lowercases, '\'->'/', ' '->'_', strips a leading
    // "assets/", strips the extension, collapses repeated and leading/trailing slashes.
    static AssetId Normalize(StringView raw);

    // Whether Value is already canonical (see the rules above). A stored id that isn't is a bug in
    // whoever wrote it, meant to be caught loudly rather than silently normalized.
    bool IsValid() const;
    bool operator==(const AssetId& other) const = default;
    // As a plain scalar ("Id: textures/goblin/walk"), not a field list: an id IS its string, and a
    // manifest is read by humans. Re-normalizes on read, so a hand-edited file cannot introduce a
    // non-canonical id.
    void Serialize(SerdeArchive* sa);
};

// The id with its type root stripped, for display in type-contextual UI where the root is redundant
// ("textures/goblin/walk" -> "goblin/walk"). Returned as a view into |id|'s own storage (so it
// stays null-terminated); the full id, not this, remains the stored/keyed/referenced identity. If
// |id| is not under the type's root, it is returned unchanged.
StringView ShortId(EAssetType type, const AssetId& id);

// The inverse, for entry: builds a full canonical AssetId from a root-relative |short_id| typed in
// a type-contextual form, prepending the type root. Tolerant of an already-full input (an input
// that already starts with the root is not doubled), so pasting a full id still works.
AssetId AssetIdFromShort(EAssetType type, StringView short_id);

// The header every asset manifest (.yml) carries, regardless of type.
struct AssetManifest {
    EAssetType Type = EAssetType::Invalid;
    i32 Version = 0;
    AssetId Id = {};
    // The raw file this asset was imported from (e.g. "raw/sprites/goblin/walk.png"). Meaningful
    // only for source-importing assets; empty for composing ones. Re-import reads it back so the
    // human never re-hunts the source.
    FixedString<256> Source = {};
    // True when a .asset payload sits beside the .yml. Composing assets (later) are yml-only. NOT
    // serialized: it is a property of the asset TYPE (T::kHasPayload), not of the file, so a manifest
    // could otherwise lie about it. The load envelope stamps it.
    bool HasPayload = false;

    void Serialize(SerdeArchive* sa);
};

// Absolute path to the assets/ directory (GetBaseDir()/assets), interned into |arena|.
StringView GetAssetsRoot(Arena* arena);
// The on-disk paths of an asset's manifest and payload: <assets>/<id>.yml and <assets>/<id>.asset.
StringView AssetYmlPath(Arena* arena, AssetId id);
StringView AssetPayloadPath(Arena* arena, AssetId id);

// Reads just the common header (type, version, id) of the manifest at |id|. Enough to dispatch the
// crawl by type before handing off to the type's own loader. Returns false if the .yml is
// unreadable.
bool PeekManifest(AssetId id, AssetManifest* out);

// The manifest load/save envelope ------------------------------------------------------------------
//
// Every asset type's on-disk flow is identical apart from its field list: read the .yml, check the
// header, hand the body to the type, stamp the identity. These templates are that flow, written
// once; a type supplies only `Serialize` plus its kAssetType/kVersion/kHasPayload constants.
//
// The non-template halves below live in asset.cpp so the platform IO surface and serde stay out of
// this header.

// Opens <assets>/<id>.yml for reading, returning an archive allocated in |arena| to deserialize
// from, or nullptr when the file is unreadable. The archive uses |arena| for BOTH target and temp,
// so nothing it produces outlives |arena|'s scope. That is fine precisely because assets are
// self-contained value blobs (FixedString, never StringView) - if that ever stops being true, this
// is the line that has to change.
SerdeArchive* OpenManifestForRead(Arena* arena, AssetId id);
// A fresh archive (in |arena|) to serialize a manifest into.
SerdeArchive* OpenManifestForWrite(Arena* arena);
// Writes |sa|'s document to <assets>/<id>.yml. false on an IO failure (already logged).
bool CommitManifestWrite(Arena* arena, AssetId id, const SerdeArchive& sa);

// Checks a just-deserialized header against what this build expects, and logs any decode errors |sa|
// collected. false means the asset must be discarded.
//
// VERSION POLICY: an OLDER file loads - its missing keys take the struct's defaults, which is the
// whole point of only bumping on a breaking change. A NEWER file is rejected, because this build
// cannot know what its fields mean. Bump kVersion only when a field is renamed, retyped, or changes
// meaning; that then needs a real migration or a re-author.
bool ValidateManifest(EAssetType expected_type,
                      i32 expected_version,
                      AssetId id,
                      const AssetManifest& manifest,
                      const SerdeArchive& sa);
// Guards the id of an asset about to be created. Logs and returns false when non-canonical.
bool ValidateNewAssetId(EAssetType type, AssetId id);

template <typename T>
std::optional<T> LoadAssetFromDisk(AssetId id) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    SerdeArchive* sa = OpenManifestForRead(arena, id);
    if (!sa) {
        return std::nullopt;
    }

    T asset = {};
    asset.Serialize(sa);
    if (!ValidateManifest(T::kAssetType, T::kVersion, id, asset.Manifest, *sa)) {
        return std::nullopt;
    }

    // Where the file lives is the identity; a manifest that disagrees was logged by ValidateManifest
    // and loses. HasPayload is a type property, never read from the file.
    asset.Manifest.Id = id;
    asset.Manifest.HasPayload = T::kHasPayload;
    return asset;
}

template <typename T>
bool SaveAssetManifest(T* asset) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    SerdeArchive* sa = OpenManifestForWrite(arena);
    asset->Serialize(sa);
    return CommitManifestWrite(arena, asset->Manifest.Id, *sa);
}

// Writes the manifest for a new blueprint with default field values. Overwrites any existing.
template <typename T>
bool CreateAsset(AssetId id) {
    if (!ValidateNewAssetId(T::kAssetType, id)) {
        return false;
    }

    T asset = {};
    asset.Manifest.Type = T::kAssetType;
    asset.Manifest.Version = T::kVersion;
    asset.Manifest.Id = id;
    asset.Manifest.HasPayload = T::kHasPayload;
    return SaveAssetManifest(&asset);
}

}  // namespace kdk
