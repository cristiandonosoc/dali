#include <dali/game/serde.h>

#include <dali/core/memory.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace kdk;

#define CREATE_ARENA()                                           \
    Arena arena = Arena::Allocate("TestArena"sv, 16 * MEGABYTE); \
    DEFER { Arena::Free(&arena); };

namespace kdk {
namespace serde_test_private {

enum class EColor : u8 {
    Red = 0,
    Green = 1,
    Blue = 2,
};

enum class ESize : i32 {
    Small = -100,
    Medium = 0,
    Large = 100,
};

struct Immediates {
    u8 U8 = 0;
    u16 U16 = 0;
    u32 U32 = 0;
    u64 U64 = 0;
    i8 I8 = 0;
    i16 I16 = 0;
    i32 I32 = 0;
    i64 I64 = 0;
    f32 F32 = 0;
    f64 F64 = 0;
    bool Bool = false;

    void Serialize(SerdeArchive* sa) {
        SERDE(sa, this, U8);
        SERDE(sa, this, U16);
        SERDE(sa, this, U32);
        SERDE(sa, this, U64);
        SERDE(sa, this, I8);
        SERDE(sa, this, I16);
        SERDE(sa, this, I32);
        SERDE(sa, this, I64);
        SERDE(sa, this, F32);
        SERDE(sa, this, F64);
        SERDE(sa, this, Bool);
    }
};

struct Leaf {
    FixedString<64> Name = {};
    i32 Value = 0;

    void Serialize(SerdeArchive* sa) {
        SERDE(sa, this, Name);
        SERDE(sa, this, Value);
    }
};

struct Nested {
    Leaf Inner = {};
    Vec2 Position = {};
    Color32 Tint = Color32::White;
    EColor Color = EColor::Red;
    ESize Size = ESize::Medium;
    StringView Text = {};

    FixedVector<i32, 8> Ints = {};
    FixedVector<Leaf, 8> Leaves = {};
    FixedVector<FixedString<32>, 8> Names = {};
    Array<i32, 4> Slots = {};

    void Serialize(SerdeArchive* sa) {
        SERDE(sa, this, Inner);
        SERDE(sa, this, Position);
        SERDE(sa, this, Tint);
        SERDE(sa, this, Color);
        SERDE(sa, this, Size);
        SERDE(sa, this, Text);
        SERDE(sa, this, Ints);
        SERDE(sa, this, Leaves);
        SERDE(sa, this, Names);
        SERDE(sa, this, Slots);
    }
};

// Round-trips |in| through a serialize pass and a deserialize pass, returning the result.
template <typename T>
T RoundTrip(Arena* arena, T in, StringView* out_yaml = nullptr) {
    SerdeArchive save = SerdeArchive::New(arena, arena, ESerdeBackend::YAML, ESerdeMode::Serialize);
    Serde(&save, "Root", &in);
    StringView yaml = save.GetSerializedString(arena);
    if (out_yaml) {
        *out_yaml = yaml;
    }

    SerdeArchive load =
        SerdeArchive::New(arena, arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());
    T out = {};
    Serde(&load, "Root", &out);
    return out;
}

}  // namespace serde_test_private
}  // namespace kdk

TEST_CASE("Serde immediates round-trip at their limits", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    Immediates in = {};
    in.U8 = std::numeric_limits<u8>::max();
    in.U16 = std::numeric_limits<u16>::max();
    in.U32 = std::numeric_limits<u32>::max();
    in.U64 = std::numeric_limits<u64>::max();
    in.I8 = std::numeric_limits<i8>::min();
    in.I16 = std::numeric_limits<i16>::min();
    in.I32 = std::numeric_limits<i32>::min();
    in.I64 = std::numeric_limits<i64>::min();
    in.F32 = 1.5f;
    in.F64 = -2.25;
    in.Bool = true;

    Immediates out = RoundTrip(&arena, in);

    CHECK(out.U8 == in.U8);
    CHECK(out.U16 == in.U16);
    CHECK(out.U32 == in.U32);
    CHECK(out.U64 == in.U64);
    CHECK(out.I8 == in.I8);
    CHECK(out.I16 == in.I16);
    CHECK(out.I32 == in.I32);
    CHECK(out.I64 == in.I64);
    CHECK(out.F32 == in.F32);
    CHECK(out.F64 == in.F64);
    CHECK(out.Bool == in.Bool);
}

TEST_CASE("Serde round-trips nested structs and containers", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    Nested in = {};
    in.Inner.Name.Set("inner"sv);
    in.Inner.Value = 42;
    in.Position = Vec2{1.5f, -2.5f};
    in.Tint = Color32::FromRGBA(0x11, 0x22, 0x33, 0x44);
    in.Color = EColor::Blue;
    in.Size = ESize::Small;
    in.Text = "some text"sv;
    in.Ints.Push(1);
    in.Ints.Push(2);
    in.Ints.Push(3);
    in.Leaves.Push(Leaf{FixedString<64>("a"sv), 10});
    in.Leaves.Push(Leaf{FixedString<64>("b"sv), 20});
    in.Names.Push(FixedString<32>("first"sv));
    in.Names.Push(FixedString<32>("second"sv));
    in.Slots = Array<i32, 4>{7, 8, 9, 10};

    Nested out = RoundTrip(&arena, in);

    CHECK(out.Inner.Name == in.Inner.Name);
    CHECK(out.Inner.Value == in.Inner.Value);
    CHECK(out.Position == in.Position);
    CHECK(out.Tint.Bits == in.Tint.Bits);
    CHECK(out.Color == in.Color);
    CHECK(out.Size == in.Size);
    CHECK(out.Text == in.Text);

    REQUIRE(out.Ints.Size == in.Ints.Size);
    for (i32 i = 0; i < in.Ints.Size; ++i) {
        CHECK(out.Ints[i] == in.Ints[i]);
    }

    REQUIRE(out.Leaves.Size == in.Leaves.Size);
    for (i32 i = 0; i < in.Leaves.Size; ++i) {
        CHECK(out.Leaves[i].Name == in.Leaves[i].Name);
        CHECK(out.Leaves[i].Value == in.Leaves[i].Value);
    }

    REQUIRE(out.Names.Size == in.Names.Size);
    for (i32 i = 0; i < in.Names.Size; ++i) {
        CHECK(out.Names[i] == in.Names[i]);
    }

    for (i32 i = 0; i < 4; ++i) {
        CHECK(out.Slots[i] == in.Slots[i]);
    }
}

TEST_CASE("Serde deserialized strings outlive the source buffer", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    // StringView deserializes by interning into TargetArena. Prove it doesn't alias the yaml text by
    // letting that text's arena scope die first.
    StringView text = {};
    {
        auto scratch = Arena::GetScratch(&arena);
        Arena* temp = scratch;

        Nested in = {};
        in.Text = "interned"sv;

        SerdeArchive save =
            SerdeArchive::New(temp, temp, ESerdeBackend::YAML, ESerdeMode::Serialize);
        Serde(&save, "Root", &in);
        StringView yaml = save.GetSerializedString(temp);

        SerdeArchive load =
            SerdeArchive::New(&arena, temp, ESerdeBackend::YAML, ESerdeMode::Deserialize);
        load.LoadData(yaml.ToSpan());
        Nested out = {};
        Serde(&load, "Root", &out);
        text = out.Text;
    }

    CHECK(text == "interned"sv);
}

TEST_CASE("Serde tolerates a missing key by keeping the default", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    // This is the version-tolerance property: an older file simply lacks the newer keys, and the
    // struct's defaults stand in. Nothing here is an error.
    StringView yaml = "Root:\n  Value: 7\n"sv;

    SerdeArchive load =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());

    Leaf out = {};
    out.Name.Set("untouched"sv);
    Serde(&load, "Root", &out);

    CHECK(out.Value == 7);
    CHECK(out.Name.IsEmpty());  // Absent key resets to the default, not left as-is.
    CHECK(!load.HasErrors());
}

TEST_CASE("Serde records an error for a malformed value instead of throwing", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    // We build without exceptions, so a mistyped scalar must degrade to a recorded error and a
    // default value, never a throw.
    StringView yaml = "Root:\n  Name: ok\n  Value: not_a_number\n"sv;

    SerdeArchive load =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());

    Leaf out = {};
    Serde(&load, "Root", &out);

    CHECK(out.Name == FixedString<64>("ok"sv));  // The good field still loaded.
    CHECK(out.Value == 0);
    CHECK(load.HasErrors());
    CHECK(load.Errors.Size == 1);
}

TEST_CASE("Serde reports an over-long sequence rather than overflowing", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    StringView yaml = "Root:\n  Ints: [1, 2, 3, 4, 5, 6, 7, 8, 9]\n"sv;  // FixedVector<i32, 8>

    SerdeArchive load =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());

    Nested out = {};
    Serde(&load, "Root", &out);

    CHECK(out.Ints.Size == 8);
    CHECK(load.HasErrors());
}

TEST_CASE("Serde reports a non-sequence where a sequence belongs", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    StringView yaml = "Root:\n  Ints: 5\n"sv;

    SerdeArchive load =
        SerdeArchive::New(&arena, &arena, ESerdeBackend::YAML, ESerdeMode::Deserialize);
    load.LoadData(yaml.ToSpan());

    Nested out = {};
    Serde(&load, "Root", &out);

    CHECK(out.Ints.IsEmpty());
    CHECK(load.HasErrors());
}

TEST_CASE("Serde keys are the member names verbatim", "[serde]") {
    using namespace kdk::serde_test_private;
    CREATE_ARENA();

    Leaf in = {};
    in.Name.Set("x"sv);
    in.Value = 1;

    StringView yaml = {};
    RoundTrip(&arena, in, &yaml);

    CHECK(yaml.Contains("Name:"sv));
    CHECK(yaml.Contains("Value:"sv));
}
