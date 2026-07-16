#pragma once

#include <dali/core/array.h>
#include <dali/core/color.h>
#include <dali/core/container.h>
#include <dali/core/defines.h>
#include <dali/core/math.h>
#include <dali/core/memory.h>
#include <dali/core/string.h>

#include <yaml-cpp/yaml.h>

#include <span>
#include <type_traits>

// Serialize + Deserialize in one pass. A type declares its fields ONCE, in a `Serialize` member that
// runs in both directions depending on the archive's mode:
//
//     struct SpriteGrid {
//         i32 CellW = 32;
//         i32 CellH = 32;
//         void Serialize(SerdeArchive* sa) {
//             SERDE(sa, this, CellW);
//             SERDE(sa, this, CellH);
//         }
//     };
//
// That single list is the whole point: an emit/parse pair written by hand drifts (a field lands in
// one and not the other and the data is silently lost), which is exactly the bug class this
// replaces.
//
// The key is the member name verbatim, so the yml is PascalCase and matches the struct.
//
// The dependency runs one way: serde knows about dali/core's types, dali/game's types know about
// serde. That is why core types (Color32, FixedString, the containers, Vec2) are handled by the
// SerdeYaml overloads below rather than by a `Serialize` member — dali/core sits underneath this
// header and must not know it exists.
namespace kdk {

// Serializes |owner->member| under the key "member". |owner| is a pointer.
#define SERDE(sa, owner, member) ::kdk::Serde(sa, #member, &owner->member)

enum class ESerdeBackend : u8 {
    Invalid = 0,
    YAML,
};

enum class ESerdeMode : u8 {
    Invalid = 0,
    Serialize,
    Deserialize,
};

// One (de)serialization pass. Deserializing is best-effort: a missing key leaves the field at its
// default (this is what makes an older file load against a newer struct), and a malformed value is
// recorded in Errors and skipped rather than aborting the whole load. Callers decide what a partial
// load means by checking HasErrors().
struct SerdeArchive {
    static constexpr i32 kMaxErrors = 128;

    ESerdeBackend Backend = ESerdeBackend::Invalid;
    ESerdeMode Mode = ESerdeMode::Invalid;

    // Where deserialized data that needs to outlive the pass is allocated (interned strings). When
    // serializing nothing lands here, so it can be the temp arena too.
    Arena* TargetArena = nullptr;
    Arena* TempArena = nullptr;

    // Error strings are interned into TempArena, so they are only readable while that arena's scope
    // is alive — i.e. check HasErrors() before the scratch scope that owns this archive closes.
    FixedVector<StringView, kMaxErrors> Errors = {};

    YAML::Node _BaseNode = {};
    // Null means "at the document root". Resolved through CurrentNode() rather than initialized in
    // New(), because New() returns by value and a pointer to the temporary's _BaseNode would dangle.
    YAML::Node* _CurrentNode = nullptr;

    static SerdeArchive New(Arena* target_arena,
                            Arena* temp_arena,
                            ESerdeBackend backend,
                            ESerdeMode mode);

    bool IsValid() const;
    bool HasErrors() const { return !Errors.IsEmpty(); }
    YAML::Node* CurrentNode() { return _CurrentNode ? _CurrentNode : &_BaseNode; }

    // Parses |data| into the archive, for a Deserialize pass.
    // NOTE: yaml-cpp offers no non-throwing parse, so syntactically malformed input terminates
    //       rather than returning an error. Matches what the asset loaders already did.
    void LoadData(std::span<const u8> data);

    // Records a malformed-input error. Always returns false, so a decode site can `return
    // sa->AddError(...)`.
    bool AddError(StringView error);

    // The serialized document as a string, interned into |arena|. For a Serialize pass.
    StringView GetSerializedString(Arena* arena) const;
};

namespace serde {

// Everything that is not handled by an overload below has to provide this. The concept exists to
// give a one-line diagnostic naming the offending type, instead of the compiler dumping every
// candidate in the overload set.
template <typename T>
concept HasSerdeMember = requires(T* t, SerdeArchive* sa) { t->Serialize(sa); };

// clang-format off
template <typename T>
concept IsImmediateType =
    std::is_same_v<T, u8>  || std::is_same_v<T, i8>  ||
    std::is_same_v<T, u16> || std::is_same_v<T, i16> ||
    std::is_same_v<T, u32> || std::is_same_v<T, i32> ||
    std::is_same_v<T, u64> || std::is_same_v<T, i64> ||
    std::is_same_v<T, f32> || std::is_same_v<T, f64> ||
    std::is_same_v<T, bool>;
// clang-format on

template <typename T>
struct IsFixedStringTrait : std::false_type {};
template <u64 CAPACITY>
struct IsFixedStringTrait<FixedString<CAPACITY>> : std::true_type {};

// Decodes |node| into |out| without throwing. yaml-cpp's plain as<T>() throws on a type mismatch and
// we build without exceptions, so every read goes through convert<T>::decode, which reports failure
// as a bool. Returns false and records an error when the value is present but not decodable (e.g.
// `FPS: fast`).
template <typename T>
bool DecodeNode(SerdeArchive* sa, const char* name, const YAML::Node& node, T* out) {
    if (YAML::convert<T>::decode(node, *out)) {
        return true;
    }
    return sa->AddError(Printf(sa->TempArena, "key '%s': value has the wrong type", name));
}

// STRUCTS -----------------------------------------------------------------------------------------

// The generic path: a nested struct, serialized as a sub-map under |name|.
template <typename T>
void SerdeYaml(SerdeArchive* sa, const char* name, T* value) {
    static_assert(HasSerdeMember<T>,
                  "serde: this type needs a `void Serialize(SerdeArchive* sa)` member. "
                  "(Core types get a SerdeYaml overload in serde.h instead - see the header comment.)");

    YAML::Node* prev = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        YAML::Node node;
        sa->_CurrentNode = &node;
        value->Serialize(sa);
        sa->_CurrentNode = prev;
        (*prev)[name] = std::move(node);
        return;
    }

    const YAML::Node& node = (*prev)[name];
    if (!node.IsDefined()) {
        *value = {};  // Missing key: the field keeps its default. This is the version tolerance.
        return;
    }
    sa->_CurrentNode = const_cast<YAML::Node*>(&node);
    value->Serialize(sa);
    sa->_CurrentNode = prev;
}

// IMMEDIATES --------------------------------------------------------------------------------------

template <typename T>
    requires IsImmediateType<T>
void SerdeYaml(SerdeArchive* sa, const char* name, T* value) {
    // u8/i8 are widened to u16/i16 on the wire: yaml-cpp would otherwise encode them as characters
    // and decode them back wrong.
    using WireType = std::conditional_t<std::is_same_v<T, u8>,
                                        u16,
                                        std::conditional_t<std::is_same_v<T, i8>, i16, T>>;

    YAML::Node* current = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        (*current)[name] = (WireType)(*value);
        return;
    }

    const YAML::Node& node = (*current)[name];
    if (!node.IsDefined()) {
        *value = {};
        return;
    }
    WireType wire = {};
    if (!DecodeNode(sa, name, node, &wire)) {
        *value = {};
        return;
    }
    *value = (T)wire;
}

// ENUMS -------------------------------------------------------------------------------------------

// Stored as the underlying integer, so REORDERING AN ENUM SILENTLY REINTERPRETS EXISTING FILES.
// Bump the asset's version when you do.
template <typename T>
    requires std::is_enum_v<T>
void SerdeYaml(SerdeArchive* sa, const char* name, T* value) {
    using UnderlyingType = std::underlying_type_t<T>;
    SerdeYaml(sa, name, reinterpret_cast<UnderlyingType*>(value));
}

// STRINGS -----------------------------------------------------------------------------------------

template <u64 CAPACITY>
void SerdeYaml(SerdeArchive* sa, const char* name, FixedString<CAPACITY>* value) {
    YAML::Node* current = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        (*current)[name] = value->Str();
        return;
    }

    const YAML::Node& node = (*current)[name];
    if (!node.IsDefined()) {
        *value = {};
        return;
    }
    std::string str;
    if (!DecodeNode(sa, name, node, &str)) {
        *value = {};
        return;
    }
    if (str.size() >= CAPACITY) {
        sa->AddError(Printf(sa->TempArena,
                            "key '%s': string of %llu does not fit FixedString<%llu> (truncated)",
                            name,
                            (u64)str.size(),
                            CAPACITY));
    }
    value->Set(StringView(str.c_str(), str.size()));
}

// Deserializes by interning into sa->TargetArena, so the result outlives the pass.
template <>
void SerdeYaml<StringView>(SerdeArchive* sa, const char* name, StringView* value);

// MATH / COLOR ------------------------------------------------------------------------------------

template <>
void SerdeYaml<Vec2>(SerdeArchive* sa, const char* name, Vec2* value);

// As one "rrggbbaa" hex scalar (channels left-to-right), rather than a {r,g,b,a} sub-map: compact,
// and the usual way a color is authored.
template <>
void SerdeYaml<Color32>(SerdeArchive* sa, const char* name, Color32* value);

// CONTAINERS --------------------------------------------------------------------------------------

// A sequence element carries no key, so it cannot reuse the keyed overloads above and dispatches
// here instead. The categories are deliberately narrower than the keyed path: anything else is a
// compile error rather than silent garbage.
template <typename T>
YAML::Node SerializeElement(SerdeArchive* sa, T* value) {
    YAML::Node node;
    if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        using Wire = std::conditional_t<std::is_same_v<U, u8>,
                                        u16,
                                        std::conditional_t<std::is_same_v<U, i8>, i16, U>>;
        node = (Wire)(*value);
    } else if constexpr (std::is_same_v<T, u8>) {
        node = (u16)(*value);
    } else if constexpr (std::is_same_v<T, i8>) {
        node = (i16)(*value);
    } else if constexpr (std::is_arithmetic_v<T>) {
        node = *value;
    } else if constexpr (IsFixedStringTrait<T>::value) {
        node = value->Str();
    } else if constexpr (std::is_same_v<T, StringView>) {
        node = value->Str();
    } else {
        static_assert(HasSerdeMember<T>,
                      "serde: sequence elements must be arithmetic, an enum, a string, or have a "
                      "`void Serialize(SerdeArchive* sa)` member.");
        YAML::Node* prev = sa->CurrentNode();
        sa->_CurrentNode = &node;
        value->Serialize(sa);
        sa->_CurrentNode = prev;
    }
    return node;
}

template <typename T>
bool DeserializeElement(SerdeArchive* sa, const char* name, const YAML::Node& node, T* out) {
    if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        using Wire = std::conditional_t<std::is_same_v<U, u8>,
                                        u16,
                                        std::conditional_t<std::is_same_v<U, i8>, i16, U>>;
        Wire wire = {};
        if (!DecodeNode(sa, name, node, &wire)) {
            return false;
        }
        *out = (T)wire;
        return true;
    } else if constexpr (std::is_same_v<T, u8> || std::is_same_v<T, i8>) {
        using Wire = std::conditional_t<std::is_same_v<T, u8>, u16, i16>;
        Wire wire = {};
        if (!DecodeNode(sa, name, node, &wire)) {
            return false;
        }
        *out = (T)wire;
        return true;
    } else if constexpr (std::is_arithmetic_v<T>) {
        return DecodeNode(sa, name, node, out);
    } else if constexpr (IsFixedStringTrait<T>::value) {
        std::string str;
        if (!DecodeNode(sa, name, node, &str)) {
            return false;
        }
        out->Set(StringView(str.c_str(), str.size()));
        return true;
    } else if constexpr (std::is_same_v<T, StringView>) {
        std::string str;
        if (!DecodeNode(sa, name, node, &str)) {
            return false;
        }
        *out = InternStringToArena(sa->TargetArena, str.c_str(), str.size());
        return true;
    } else {
        static_assert(HasSerdeMember<T>,
                      "serde: sequence elements must be arithmetic, an enum, a string, or have a "
                      "`void Serialize(SerdeArchive* sa)` member.");
        YAML::Node* prev = sa->CurrentNode();
        sa->_CurrentNode = const_cast<YAML::Node*>(&node);
        out->Serialize(sa);
        sa->_CurrentNode = prev;
        return true;
    }
}

// Array is always exactly N long, so a short sequence leaves the tail default-initialized and a long
// one is truncated with an error.
template <typename T, i32 N>
void SerdeYaml(SerdeArchive* sa, const char* name, Array<T, N>* values) {
    YAML::Node* current = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        YAML::Node array_node(YAML::NodeType::Sequence);
        for (i32 i = 0; i < N; i++) {
            array_node.push_back(SerializeElement(sa, &values->At(i)));
        }
        (*current)[name] = std::move(array_node);
        return;
    }

    *values = {};
    const YAML::Node& node = (*current)[name];
    if (!node.IsDefined()) {
        return;
    }
    if (!node.IsSequence()) {
        sa->AddError(Printf(sa->TempArena, "key '%s': expected a sequence", name));
        return;
    }

    i32 index = 0;
    for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
        if (index >= N) {
            sa->AddError(Printf(sa->TempArena, "key '%s': more than %d elements (truncated)", name, N));
            return;
        }
        DeserializeElement(sa, name, *it, &values->At(index));
        index++;
    }
}

template <typename T, i32 N>
void SerdeYaml(SerdeArchive* sa, const char* name, FixedVector<T, N>* values) {
    YAML::Node* current = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        YAML::Node array_node(YAML::NodeType::Sequence);
        for (i32 i = 0; i < values->Size; i++) {
            array_node.push_back(SerializeElement(sa, &values->At(i)));
        }
        (*current)[name] = std::move(array_node);
        return;
    }

    values->Clear();
    const YAML::Node& node = (*current)[name];
    if (!node.IsDefined()) {
        return;
    }
    if (!node.IsSequence()) {
        sa->AddError(Printf(sa->TempArena, "key '%s': expected a sequence", name));
        return;
    }

    for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
        if (values->IsFull()) {
            sa->AddError(Printf(sa->TempArena, "key '%s': more than %d elements (truncated)", name, N));
            return;
        }
        T element = {};
        if (!DeserializeElement(sa, name, *it, &element)) {
            continue;  // Error already recorded; drop this element and keep the rest.
        }
        values->Push(element);
    }
}

}  // namespace serde

template <typename T>
void Serde(SerdeArchive* sa, const char* name, T* value) {
    switch (sa->Backend) {
        case ESerdeBackend::Invalid: ASSERT(false); break;
        case ESerdeBackend::YAML: serde::SerdeYaml(sa, name, value); break;
    }
}

}  // namespace kdk
