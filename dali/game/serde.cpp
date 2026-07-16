#include <dali/game/serde.h>

#include <dali/core/memory.h>
#include <dali/core/string.h>

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <sstream>

namespace kdk {

namespace serde_private {

i32 HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Parses "rrggbbaa" (an optional '#' or "0x" prefix is tolerated) into |out|. Leaves |out| untouched
// and returns false unless the string is exactly 8 hex digits. Byte order is written out explicitly
// so it does not depend on Color32's union endianness.
bool ParseColorHex(StringView s, Color32* out) {
    u64 start = 0;
    if (s.Size >= 1) {
        if (s[0] == '#') {
            start = 1;
        }
    }
    if (s.Size >= 2) {
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                start = 2;
            }
        }
    }
    if (s.Size - start != 8) {
        return false;
    }

    u32 value = 0;
    for (u64 i = start; i < s.Size; ++i) {
        i32 digit = HexDigit(s[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (u32)digit;
    }
    out->R = (u8)((value >> 24) & 0xffu);
    out->G = (u8)((value >> 16) & 0xffu);
    out->B = (u8)((value >> 8) & 0xffu);
    out->A = (u8)(value & 0xffu);
    return true;
}

}  // namespace serde_private

SerdeArchive SerdeArchive::New(Arena* target_arena,
                               Arena* temp_arena,
                               ESerdeBackend backend,
                               ESerdeMode mode) {
    SerdeArchive sa = {};
    sa.Backend = backend;
    sa.Mode = mode;
    sa.TargetArena = target_arena;
    sa.TempArena = temp_arena;
    sa._BaseNode = YAML::Node();
    return sa;
}

bool SerdeArchive::IsValid() const {
    if (Backend == ESerdeBackend::Invalid) {
        return false;
    }
    if (Mode == ESerdeMode::Invalid) {
        return false;
    }
    if (TargetArena == nullptr) {
        return false;
    }
    if (TempArena == nullptr) {
        return false;
    }
    return true;
}

void SerdeArchive::LoadData(std::span<const u8> data) {
    ASSERT(Mode == ESerdeMode::Deserialize);
    ASSERT(Backend == ESerdeBackend::YAML);
    _BaseNode = YAML::Load(std::string((const char*)data.data(), data.size()));
    _CurrentNode = nullptr;
}

bool SerdeArchive::AddError(StringView error) {
    if (Errors.IsFull()) {
        return false;
    }
    Errors.Push(error);
    return false;
}

StringView SerdeArchive::GetSerializedString(Arena* arena) const {
    ASSERT(Mode == ESerdeMode::Serialize);
    std::stringstream ss;
    ss << _BaseNode;
    std::string str = ss.str();
    return InternStringToArena(arena, str.c_str(), str.size());
}

namespace serde {

template <>
void SerdeYaml<StringView>(SerdeArchive* sa, const char* name, StringView* value) {
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
    *value = InternStringToArena(sa->TargetArena, str.c_str(), str.size());
}

template <>
void SerdeYaml<Vec2>(SerdeArchive* sa, const char* name, Vec2* value) {
    YAML::Node* current = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        YAML::Node node;
        node.SetStyle(YAML::EmitterStyle::Flow);
        node["x"] = value->x;
        node["y"] = value->y;
        (*current)[name] = std::move(node);
        return;
    }

    const YAML::Node& node = (*current)[name];
    if (!node.IsDefined()) {
        *value = {};
        return;
    }
    DecodeNode(sa, name, node["x"], &value->x);
    DecodeNode(sa, name, node["y"], &value->y);
}

template <>
void SerdeYaml<Color32>(SerdeArchive* sa, const char* name, Color32* value) {
    using namespace serde_private;

    YAML::Node* current = sa->CurrentNode();
    if (sa->Mode == ESerdeMode::Serialize) {
        char hex[9] = {};
        snprintf(hex, sizeof(hex), "%02x%02x%02x%02x", value->R, value->G, value->B, value->A);
        // Emits unquoted, which is fine even for an all-digit hex: the read side decodes the raw
        // scalar text, so whatever type YAML infers for it never comes into play.
        (*current)[name] = std::string(hex);
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
    if (!ParseColorHex(StringView(str.c_str(), str.size()), value)) {
        sa->AddError(Printf(sa->TempArena, "key '%s': not an rrggbbaa color", name));
        *value = {};
    }
}

}  // namespace serde

}  // namespace kdk
