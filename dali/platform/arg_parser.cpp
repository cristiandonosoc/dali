#include <dali/platform/arg_parser.h>

#include <dali/core/defines.h>

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdlib>

namespace kdk {

namespace arg_parser_private {

void AddArgument(ArgParser* ap,
                 EArgType type,
                 StringView long_name,
                 char short_name,
                 bool required) {
    ASSERT(ap->ArgCount < ArgParser::kMaxArguments);

    // Check that the argument is not already set.
    for (u32 i = 0; i < ap->ArgCount; i++) {
        ArgParser::Argument& arg = ap->Arguments[i];
        if (arg.LongName == long_name) {
            SDL_Log("ERROR: argument %s already defined!\n", arg.LongName.Str());
            ASSERT(false);
            return;
        }

        if (short_name != NULL) {
            if (arg.ShortName == short_name) {
                SDL_Log("ERROR: setting argument %s: argument %s has same short name %c\n",
                        long_name.Str(),
                        arg.LongName.Str(),
                        short_name);
                ASSERT(false);
                return;
            }
        }
    }

    auto& arg = ap->Arguments[ap->ArgCount];
    ap->ArgCount++;

    arg = ArgParser::Argument{
        .Type = type,
        .LongName = long_name,
        .ShortName = short_name,
        .Required = required,
        // To shutup the compiler.
        .BoolValue = false,
    };
}

ArgParser::Argument* ParseArgument(ArgParser* ap, const char* argstr) {
    bool is_short_name = false;
    char short_name = NULL;

    constexpr u32 kMaxLongName = 128;
    char long_name_holder[kMaxLongName] = {};
    u32 long_name_count = 0;

    u32 ci = 0;
    char c = argstr[ci];
    while (c) {
        DEFER {
            ci++;
            c = argstr[ci];
        };

        if (c == '-') {
            is_short_name = ci == 0;
            if (ci > 1) {
                SDL_Log("ERROR: arg %s: flags must start with at most --\n", argstr);
                return nullptr;
            }
            continue;
        }

        if (c == '=') {
            SDL_Log("ERROR: no = sign supported for now\n");
            return nullptr;
        }

        if (is_short_name) {
            if (short_name != NULL) {
                SDL_Log("ERROR: arg %s: short name flags must be at most one character\n", argstr);
                return nullptr;
            }
            short_name = c;
            continue;
        }

        if (long_name_count == kMaxLongName - 1) {
            SDL_Log("ERROR: arg %s: long names can only be %d characters long\n",
                    argstr,
                    kMaxLongName);
            return nullptr;
        }
        long_name_holder[long_name_count++] = c;
    }

    // Now that we parsed the argument we try to find it.
    ArgParser::Argument* arg = nullptr;
    for (u32 i = 0; i < ap->ArgCount; i++) {
        auto& candidate = ap->Arguments[i];
        if (is_short_name) {
            if (candidate.ShortName == short_name) {
                arg = &candidate;
                break;
            }
        } else {
            if (strcmp(candidate.LongName.Str(), long_name_holder) == 0) {
                arg = &candidate;
                break;
            }
        }
    }

    if (!arg) {
        SDL_Log("ERROR: arg %s not found\n", argstr);
        return nullptr;
    }

    return arg;
}

const ArgParser::Argument* FindArgument(const ArgParser& ap, StringView long_name) {
    for (u32 i = 0; i < ap.ArgCount; i++) {
        auto& arg = ap.Arguments[i];
        if (arg.LongName == long_name) {
            return &arg;
        }
    }

    return nullptr;
}

template <typename T>
bool FindValue(const ArgParser& ap, EArgType type, StringView long_name, T* out) {
    auto* arg = FindArgument(ap, long_name);
    if (!arg) {
        return false;
    }

    if (arg->Type != type) {
        SDL_Log("ERROR: FindStringValue: flag %s: Wrong flag type: expected: %s, got: %s",
                long_name.Str(),
                ToString(type),
                ToString(arg->Type));
        ASSERT(false);
        return false;
    }

    if (!arg->HasValue) {
        return false;
    }

    if constexpr (std::is_same_v<T, bool>) {
        *out = arg->BoolValue;
        return true;
    } else if constexpr (std::is_same_v<T, StringView>) {
        *out = arg->StringValue;
        return true;
    } else if constexpr (std::is_same_v<T, i32>) {
        *out = arg->IntValue;
        return true;
    } else if constexpr (std::is_same_v<T, float>) {
        *out = arg->FloatValue;
        return true;
    } else {
        ASSERT(false);
        return false;
    }
}

}  // namespace arg_parser_private

const char* ToString(EArgType type) {
    // clang-format off
	switch (type) {
        case EArgType::Invalid: return "<INVALID>";
        case EArgType::Boolean: return "Boolean";
        case EArgType::String: return "String";
        case EArgType::Int: return "Int";
        case EArgType::Float: return "Float";
	}
    // clang-format on

    ASSERT(false);
    return "<UNKNOWN>";
}

void AddStringArgument(ArgParser* ap, StringView long_name, char short_name, bool required) {
    arg_parser_private::AddArgument(ap, EArgType::String, long_name, short_name, required);
}

void AddIntArgument(ArgParser* ap, StringView long_name, char short_name, bool required) {
    arg_parser_private::AddArgument(ap, EArgType::Int, long_name, short_name, required);
}

void AddFloatArgument(ArgParser* ap, StringView long_name, char short_name, bool required) {
    arg_parser_private::AddArgument(ap, EArgType::Float, long_name, short_name, required);
}

bool ParseArguments(ArgParser* ap, int argc, const char* argv[]) {
    using namespace arg_parser_private;

    ap->AppName = argv[0];

    ArgParser::Argument* current_arg = nullptr;
    for (int i = 1; i < argc; i++) {
        const char* argstr = argv[i];

        // If we haven't find a current argument, it means we're searching for one.
        if (!current_arg) {
            if (current_arg = ParseArgument(ap, argstr); !current_arg) {
                return false;
            }

            if (current_arg->HasValue) {
                SDL_Log("ERROR: flag %s (short: %c) already set\n",
                        current_arg->LongName.Str(),
                        current_arg->ShortName);
                return false;
            }

            // If it's boolean, we immediatelly put it to true and then clear it so we can parse
            // another argument rather than searching for a value.
            if (current_arg->Type == EArgType::Boolean) {
                current_arg->BoolValue = true;
                current_arg = nullptr;
            }

            continue;
        }

        // Means that we need to insert a value.
        switch (current_arg->Type) {
            case EArgType::Invalid: ASSERT(false); return false;
            case EArgType::Boolean:
                ASSERTF(false, "Boolean should be handled directly in the definition part");
                return false;
            case EArgType::String: current_arg->StringValue = StringView(argstr); break;
            case EArgType::Int: {
                char* end = nullptr;
                current_arg->IntValue = (u32)strtol(argstr, &end, 10);
                if (!end) {
                    SDL_Log("ERROR: parsing %s to int\n", argstr);
                    return false;
                }
                break;
            }
            case EArgType::Float: {
                char* end = nullptr;
                current_arg->FloatValue = strtof(argstr, &end);
                if (!end) {
                    SDL_Log("ERROR: parsing %s to float\n", argstr);
                    return false;
                }
                break;
            }
        }

        // Clear for next argument searching.
        current_arg->HasValue = true;
        current_arg = nullptr;
    }

    if (current_arg) {
        SDL_Log("ERROR: flag %s no value given\n", current_arg->LongName.Str());
        return false;
    }

    // Check for required flags.
    for (u32 i = 0; i < ap->ArgCount; i++) {
        auto& arg = ap->Arguments[i];

        if (arg.Required && !arg.HasValue) {
            SDL_Log("ERROR: flag %s is required but has no value", arg.LongName.Str());
            return false;
        }
    }

    return true;
}

bool FindBoolValue(const ArgParser& ap, StringView long_name, bool* out) {
    return arg_parser_private::FindValue(ap, EArgType::Boolean, long_name, out);
}

bool FindStringValue(const ArgParser& ap, StringView long_name, StringView* out) {
    return arg_parser_private::FindValue(ap, EArgType::String, long_name, out);
}

bool FindIntValue(const ArgParser& ap, StringView long_name, i32* out) {
    return arg_parser_private::FindValue(ap, EArgType::Int, long_name, out);
}

bool FindFloatValue(const ArgParser& ap, StringView long_name, float* out) {
    return arg_parser_private::FindValue(ap, EArgType::Float, long_name, out);
}

}  // namespace kdk
