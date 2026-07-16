#include <dali/game/assets/asset.h>

#include <dali/core/memory.h>
#include <dali/game/assets/enemy_asset.h>
#include <dali/game/assets/spritesheet_asset.h>
#include <dali/game/assets/tower_asset.h>
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

// Both go through the "Manifest" KEY rather than calling Serialize at the document root, because
// that is the only shape a real file has: every asset writes its header via SERDE(sa, this,
// Manifest). Testing the root-level shape instead is self-consistent and proves nothing.
StringView SerializeToYaml(Arena* arena, AssetManifest* manifest) {
    SerdeArchive sa = SerdeArchive::New(arena, arena, ESerdeBackend::YAML, ESerdeMode::Serialize);
    Serde(&sa, "Manifest", manifest);
    return sa.GetSerializedString(arena);
}

AssetManifest DeserializeFromYaml(Arena* arena, StringView yaml) {
    SerdeArchive sa = SerdeArchive::New(arena, arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    sa.LoadData(yaml.ToSpan());
    AssetManifest manifest = {};
    Serde(&sa, "Manifest", &manifest);
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
    StringView yaml =
        "Manifest:\n  Type: texture\n  Version: 1\n  Id: Textures\\Goblin\\Walk.png\n"sv;

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

// The exact shape of a migrated sheet on disk (assets/spritesheets/towers/archer.yml). Kept verbatim
// so the manifest format is pinned by a real file, not by a hand-made approximation: if serde's key
// naming or nesting drifts, this fails instead of every asset silently loading empty.
constexpr StringView kMigratedSheetYaml =
    "Manifest:\n"
    "  Type: spritesheet\n"
    "  Version: 3\n"
    "  Id: spritesheets/towers/archer\n"
    "  Source: ''\n"
    "Clips:\n"
    "  - Name: idle_1\n"
    "    Texture: textures/towers/archer_tower/idle_1\n"
    "    Grid: {CellW: 70, CellH: 130, Margin: 0, Spacing: 0}\n"
    "    Pivot: {x: 0, y: 0.5}\n"
    "    Frames: [0]\n"
    "  - Name: idle_2\n"
    "    Texture: textures/towers/archer_tower/idle_2\n"
    "    Grid: {CellW: 70, CellH: 130, Margin: 0, Spacing: 0}\n"
    "    Pivot: {x: 0, y: 0}\n"
    "    Frames: [0, 1, 2, 3]\n"sv;

TEST_CASE("A migrated spritesheet loads with its data intact", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    SerdeArchive sa =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    sa.LoadData(kMigratedSheetYaml.ToSpan());

    SpriteSheetAsset sheet = {};
    sheet.Serialize(&sa);

    CHECK(!sa.HasErrors());
    CHECK(sheet.Manifest.Type == EAssetType::SpriteSheet);
    CHECK(sheet.Manifest.Version == 3);
    CHECK(sheet.Manifest.Id == AssetId::Normalize("spritesheets/towers/archer"sv));

    REQUIRE(sheet.Clips.Size == 2);

    const SpriteSheetClip& first = sheet.Clips[0];
    CHECK(first.Name == FixedString<64>("idle_1"sv));
    CHECK(first.Texture == AssetId::Normalize("textures/towers/archer_tower/idle_1"sv));
    CHECK(first.Grid.CellW == 70);
    CHECK(first.Grid.CellH == 130);
    CHECK(first.Grid.Margin == 0);
    CHECK(first.Grid.Spacing == 0);
    CHECK(first.Pivot == Vec2{0.0f, 0.5f});
    REQUIRE(first.Frames.Size == 1);
    CHECK(first.Frames[0] == 0);
    // The file predates FPS being serialized. Version tolerance means it takes the struct default
    // rather than 0, which would freeze the animation.
    CHECK(first.FPS == 8.0f);

    const SpriteSheetClip& second = sheet.Clips[1];
    CHECK(second.Name == FixedString<64>("idle_2"sv));
    CHECK(second.Pivot == Vec2{0.0f, 0.0f});
    REQUIRE(second.Frames.Size == 4);
    CHECK(second.Frames[3] == 3);
}

TEST_CASE("A spritesheet round-trips", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    SpriteSheetAsset in = {};
    in.Manifest.Type = EAssetType::SpriteSheet;
    in.Manifest.Version = SpriteSheetAsset::kVersion;
    in.Manifest.Id = AssetId::Normalize("spritesheets/goblin"sv);

    SpriteSheetClip clip = {};
    clip.Name.Set("d_walk"sv);
    clip.Texture = AssetId::Normalize("textures/goblin/d_walk"sv);
    clip.Grid = SpriteGrid{48, 48, 1, 2};
    clip.Pivot = Vec2{0.25f, -0.5f};
    clip.FPS = 12.0f;
    clip.Frames.Push(0);
    clip.Frames.Push(3);
    in.Clips.Push(clip);

    SerdeArchive save = SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Serialize);
    in.Serialize(&save);
    StringView yaml = save.GetSerializedString(&arena);

    SerdeArchive load =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());
    SpriteSheetAsset out = {};
    out.Serialize(&load);

    CHECK(!load.HasErrors());
    REQUIRE(out.Clips.Size == 1);
    CHECK(out.Clips[0].Name == clip.Name);
    CHECK(out.Clips[0].Texture == clip.Texture);
    CHECK(out.Clips[0].Grid.CellW == 48);
    CHECK(out.Clips[0].Grid.Margin == 1);
    CHECK(out.Clips[0].Grid.Spacing == 2);
    CHECK(out.Clips[0].Pivot == clip.Pivot);
    CHECK(out.Clips[0].FPS == 12.0f);
    REQUIRE(out.Clips[0].Frames.Size == 2);
    CHECK(out.Clips[0].Frames[1] == 3);
}

TEST_CASE("A clip reference carries FlipX through one shared encoding", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    // The regression this whole refactor is for: FlipX shipped in the enemy's hand-written walk-clip
    // encoding and was missing from the tower's hand-written idle-clip one. There is now a single
    // SpriteSheetClipReference::Serialize, so both compose it and neither can drift.
    SpriteSheetClipReference in = {};
    in.SpriteSheetId = AssetId::Normalize("spritesheets/goblin"sv);
    in.ClipName.Set("s_walk"sv);
    in.FlipX = true;

    SerdeArchive save = SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Serialize);
    Serde(&save, "Idle", &in);
    StringView yaml = save.GetSerializedString(&arena);

    SerdeArchive load =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());
    SpriteSheetClipReference out = {};
    Serde(&load, "Idle", &out);

    CHECK(out.SpriteSheetId == in.SpriteSheetId);
    CHECK(out.ClipName == in.ClipName);
    CHECK(out.FlipX == true);
}

// The exact shape of the migrated assets/enemies/goblin.yml and assets/towers/arrow.yml, verbatim.
// A migration mistake is invisible at runtime (a missing key just takes a default), so the real files
// are pinned here rather than trusted.
constexpr StringView kMigratedEnemyYaml =
    "Manifest:\n"
    "  Type: enemy\n"
    "  Version: 2\n"
    "  Id: enemies/goblin\n"
    "  Source: ''\n"
    "PerInstanceData:\n"
    "  Speed: 25\n"
    "  MaxHealth: 10\n"
    "  Damage: 5\n"
    "  Reward: 5\n"
    "  Color: 35ff00ff\n"
    "  Walk:\n"
    "    Down:\n"
    "      SpriteSheetId: spritesheets/goblin\n"
    "      ClipName: d_walk\n"
    "      FlipX: false\n"
    "    Up:\n"
    "      SpriteSheetId: spritesheets/goblin\n"
    "      ClipName: u_walk\n"
    "      FlipX: false\n"
    "    Left:\n"
    "      SpriteSheetId: spritesheets/goblin\n"
    "      ClipName: s_walk\n"
    "      FlipX: false\n"
    "    Right:\n"
    "      SpriteSheetId: spritesheets/goblin\n"
    "      ClipName: s_walk\n"
    "      FlipX: true\n"sv;

constexpr StringView kMigratedTowerYaml =
    "Manifest:\n"
    "  Type: tower\n"
    "  Version: 1\n"
    "  Id: towers/arrow\n"
    "  Source: ''\n"
    "PerInstanceData:\n"
    "  Range: 180\n"
    "  FireInterval: 0.6\n"
    "  Damage: 5\n"
    "  ProjectileHitRadius: 8\n"
    "  Cost: 40\n"
    "  IdleClip:\n"
    "    SpriteSheetId: spritesheets/towers/archer\n"
    "    ClipName: idle_4\n"
    "    FlipX: false\n"sv;

TEST_CASE("A migrated enemy loads with its data intact", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    SerdeArchive sa =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    sa.LoadData(kMigratedEnemyYaml.ToSpan());

    EnemyAsset enemy = {};
    enemy.Serialize(&sa);

    CHECK(!sa.HasErrors());
    CHECK(enemy.Manifest.Type == EAssetType::Enemy);
    CHECK(enemy.Manifest.Id == AssetId::Normalize("enemies/goblin"sv));

    const EnemyAsset::InstanceData& data = enemy.PerInstanceData;
    CHECK(data.Speed == 25.0f);
    CHECK(data.MaxHealth == 10.0f);
    CHECK(data.Damage == 5.0f);
    CHECK(data.Reward == 5);
    CHECK(data.Color.Bits == Color32::FromRGBA(0x35, 0xff, 0x00, 0xff).Bits);

    // Facings are keyed by name, so the file survives EFacing being reordered.
    CHECK(data.Walk.Resolve(EFacing::Down).ClipName == FixedString<64>("d_walk"sv));
    CHECK(data.Walk.Resolve(EFacing::Up).ClipName == FixedString<64>("u_walk"sv));
    CHECK(data.Walk.Resolve(EFacing::Left).ClipName == FixedString<64>("s_walk"sv));
    CHECK(data.Walk.Resolve(EFacing::Right).ClipName == FixedString<64>("s_walk"sv));
    CHECK(data.Walk.Resolve(EFacing::Down).SpriteSheetId ==
          AssetId::Normalize("spritesheets/goblin"sv));

    // One side-view clip serving both sides, mirrored on one of them.
    CHECK(data.Walk.Resolve(EFacing::Left).FlipX == false);
    CHECK(data.Walk.Resolve(EFacing::Right).FlipX == true);
}

TEST_CASE("A migrated tower loads with its data intact", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    SerdeArchive sa =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    sa.LoadData(kMigratedTowerYaml.ToSpan());

    TowerAsset tower = {};
    tower.Serialize(&sa);

    CHECK(!sa.HasErrors());
    CHECK(tower.Manifest.Type == EAssetType::Tower);
    CHECK(tower.Manifest.Id == AssetId::Normalize("towers/arrow"sv));

    const TowerAsset::InstanceData& data = tower.PerInstanceData;
    CHECK(data.Range == 180.0f);
    CHECK(data.FireInterval == 0.6f);  // The tuned value, not InstanceData's default.
    CHECK(data.Damage == 5.0f);
    CHECK(data.ProjectileHitRadius == 8.0f);
    CHECK(data.Cost == 40);
    CHECK(data.IdleClip.SpriteSheetId == AssetId::Normalize("spritesheets/towers/archer"sv));
    CHECK(data.IdleClip.ClipName == FixedString<64>("idle_4"sv));
    CHECK(data.IdleClip.FlipX == false);
}

TEST_CASE("A manifest header reads on its own, the way the crawl does", "[asset][serde]") {
    using namespace kdk::asset_serde_test_private;
    CREATE_ARENA();

    // What PeekManifest does: pull ONLY the header out of a file, to dispatch the crawl by type
    // before the concrete loader runs. It has to descend into the same "Manifest:" block the asset's
    // own Serialize writes — reading at the document root finds no keys, and because a missing key is
    // not an error, every asset silently comes back Invalid instead of failing loudly.
    struct Case {
        StringView Yaml;
        EAssetType Type;
        i32 Version;
        const char* Id;
    };
    Array cases = {
        Case{kMigratedSheetYaml, EAssetType::SpriteSheet, 3, "spritesheets/towers/archer"},
        Case{kMigratedEnemyYaml, EAssetType::Enemy, 2, "enemies/goblin"},
        Case{kMigratedTowerYaml, EAssetType::Tower, 1, "towers/arrow"},
    };

    for (const Case& c : cases) {
        SerdeArchive sa =
            SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
        sa.LoadData(c.Yaml.ToSpan());

        AssetManifest header = {};
        Serde(&sa, "Manifest", &header);

        INFO("peeking " << c.Id);
        CHECK(header.Type == c.Type);
        CHECK(header.Type != EAssetType::Invalid);
        CHECK(header.Version == c.Version);
        CHECK(header.Id == AssetId::Normalize(StringView(c.Id)));
    }
}
