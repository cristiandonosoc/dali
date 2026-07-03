#pragma once

#include <dali/core/defines.h>
#include <dali/core/string.h>

namespace kdk {

enum class EArgType : u8 {
    Invalid,
    Boolean,
    String,
    Int,
    Float,
};
const char* ToString(EArgType type);

struct ArgParser {
    static constexpr u32 kMaxArguments = 32;

    struct Argument {
        EArgType Type = EArgType::Invalid;
        StringView LongName = {};
        char ShortName = NULL;

        bool Required = false;

        bool HasValue = 0;
        union {
            bool BoolValue;
            StringView StringValue;
            i32 IntValue;
            float FloatValue;
        };
    };

    const char* AppName = nullptr;
    Argument Arguments[kMaxArguments] = {};
    u32 ArgCount = 0;
};

void AddStringArgument(ArgParser* ap, StringView long_name, char short_name, bool required);
void AddIntArgument(ArgParser* ap, StringView long_name, char short_name, bool required);
void AddFloatArgument(ArgParser* ap, StringView long_name, char short_name, bool required);

bool ParseArguments(ArgParser* ap, int argc, const char* argv[]);

bool FindBoolValue(const ArgParser& ap, StringView long_name, bool* out);
bool FindStringValue(const ArgParser& ap, StringView long_name, StringView* out);
bool FindIntValue(const ArgParser& ap, StringView long_name, i32* out);
bool FindFloatValue(const ArgParser& ap, StringView long_name, float* out);

}  // namespace kdk
