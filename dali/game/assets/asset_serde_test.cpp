#include <dali/game/assets/asset.h>

#include <dali/core/memory.h>
#include <dali/game/platform.h>
#include <dali/game/serde.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;

#define CREATE_ARENA()                                           \
    Arena arena = Arena::Allocate("TestArena"sv, 16 * MEGABYTE); \
    DEFER { Arena::Free(&arena); };

namespace kdk {
namespace asset_serde_test_private {

// ValidateManifest reports through LogError, which asserts without a platform. Swallow the output;
// the tests assert on return values, not on what was logged.
struct SilentPlatform {
    PlatformState State = {};

    SilentPlatform() {
        State.API.Log = [](ELogSeverity, StringView) {};
        SetGlobalPlatformState(&State);
    }
    ~SilentPlatform() { SetGlobalPlatformState(nullptr); }
};

StringView SerializeToYaml(Arena* arena, AssetManifest* manifest) {
    SerdeArchive sa = SerdeArchive::New(arena, arena, ESerdeBackend::YAML, ESerdeMode::Serialize);
    manifest->Serialize(&sa);
    return sa.GetSerializedString(arena);
}

AssetManifest DeserializeFromYaml(Arena* arena, StringView yaml) {
    SerdeArchive sa = SerdeArchive::New(arena, arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    sa.LoadData(yaml.ToSpan());
    AssetManifest manifest = {};
    manifest.Serialize(&sa);
    return manifest;
}

}  // namespace asset_serde_test_private
}  // namespace kdk

TEST_CASE("AssetId serializes as a scalar, not a sub-map", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    AssetManifest in = {};
    in.Type = EAssetType::SpriteSheet;
    in.Id = AssetId::Normalize("spritesheets/goblin"sv);

    StringView yaml = SerializeToYaml(&arena, &in);

    CHECK(yaml.Contains("Id: spritesheets/goblin"sv));
    CHECK(!yaml.Contains("Value:"sv));  // The {Value: ...} sub-map a Serialize member would produce.
}

TEST_CASE("AssetManifest writes its type as a name, not an enum integer", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    AssetManifest in = {};
    in.Type = EAssetType::Tower;
    in.Id = AssetId::Normalize("towers/arrow"sv);

    StringView yaml = SerializeToYaml(&arena, &in);

    // Reordering XASSET_TYPES must not silently reinterpret every manifest on disk.
    CHECK(yaml.Contains("Type: tower"sv));
}

TEST_CASE("AssetManifest round-trips", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    AssetManifest in = {};
    in.Type = EAssetType::Texture;
    in.Version = 1;
    in.Id = AssetId::Normalize("textures/goblin/walk"sv);
    in.Source.Set("raw/sprites/goblin/D_Walk.png"sv);
    in.HasPayload = true;

    AssetManifest out = DeserializeFromYaml(&arena, SerializeToYaml(&arena, &in));

    CHECK(out.Type == in.Type);
    CHECK(out.Version == in.Version);
    CHECK(out.Id == in.Id);
    CHECK(out.Source == in.Source);
    // Not serialized: it is a property of the asset type. The load envelope stamps it.
    CHECK(out.HasPayload == false);
}

TEST_CASE("AssetId is re-canonicalized on read", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    // A hand-edited manifest must not be able to smuggle a non-canonical id into the registry.
    StringView yaml = "Type: texture\nVersion: 1\nId: Textures\\Goblin\\Walk.png\n"sv;

    AssetManifest out = DeserializeFromYaml(&arena, yaml);

    CHECK(out.Id == AssetId::Normalize("textures/goblin/walk"sv));
    CHECK(out.Id.IsValid());
}

TEST_CASE("Manifest version policy: older loads, newer is rejected", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    SilentPlatform platform;
    AssetId id = AssetId::Normalize("towers/arrow"sv);
    SerdeArchive sa = SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Serialize);

    AssetManifest manifest = {};
    manifest.Type = EAssetType::Tower;
    manifest.Id = id;

    SECTION("an older file loads - its missing keys just took defaults") {
        manifest.Version = 2;
        CHECK(ValidateManifest(EAssetType::Tower, 3, id, manifest, sa));
    }

    SECTION("the current version loads") {
        manifest.Version = 3;
        CHECK(ValidateManifest(EAssetType::Tower, 3, id, manifest, sa));
    }

    SECTION("a newer file is rejected - this build cannot know what its fields mean") {
        manifest.Version = 4;
        CHECK(!ValidateManifest(EAssetType::Tower, 3, id, manifest, sa));
    }

    SECTION("a manifest of another type is rejected") {
        manifest.Type = EAssetType::Enemy;
        manifest.Version = 3;
        CHECK(!ValidateManifest(EAssetType::Tower, 3, id, manifest, sa));
    }

    SECTION("an id mismatch is logged but still loads - the path wins") {
        manifest.Version = 3;
        manifest.Id = AssetId::Normalize("towers/wrong"sv);
        CHECK(ValidateManifest(EAssetType::Tower, 3, id, manifest, sa));
    }
}
