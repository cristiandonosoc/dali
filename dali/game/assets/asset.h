#pragma once

#include <dali/core/string.h>

namespace kdk {

struct Arena;

enum class EAssetType : u8 {
    Invalid = 0,
    Texture,
    SpriteSheet,
    Enemy,
    COUNT,
};
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
};

// The id with its type root stripped, for display in type-contextual UI where the root is redundant
// ("textures/goblin/walk" -> "goblin/walk"). Returned as a view into |id|'s own storage (so it stays
// null-terminated); the full id, not this, remains the stored/keyed/referenced identity. If |id| is
// not under the type's root, it is returned unchanged.
StringView ShortId(EAssetType type, const AssetId& id);

// The inverse, for entry: builds a full canonical AssetId from a root-relative |short_id| typed in a
// type-contextual form, prepending the type root. Tolerant of an already-full input (an input that
// already starts with the root is not doubled), so pasting a full id still works.
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
    // True when a .asset payload sits beside the .yml. Composing assets (later) are yml-only.
    bool HasPayload = false;
};

// Absolute path to the assets/ directory (GetBaseDir()/assets), interned into |arena|.
StringView GetAssetsRoot(Arena* arena);
// The on-disk paths of an asset's manifest and payload: <assets>/<id>.yml and <assets>/<id>.asset.
StringView AssetYmlPath(Arena* arena, AssetId id);
StringView AssetPayloadPath(Arena* arena, AssetId id);

// Reads just the common header (type, version, id) of the manifest at |id|. Enough to dispatch the
// crawl by type before handing off to the type's own loader. Returns false if the .yml is unreadable.
bool PeekManifest(AssetId id, AssetManifest* out);

}  // namespace kdk
