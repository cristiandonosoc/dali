#include <dali/platform/arg_parser.h>

#include <dali/core/container.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;

TEST_CASE("ArgParser", "[arg_parser]") {
    SECTION("String") {
        ArgParser ap = {};
        AddStringArgument(&ap, "long_flag"sv, NULL, false);
        AddStringArgument(&ap, "another_long_flag"sv, 'a', false);
        AddStringArgument(&ap, "yet_another"sv, 'y', false);

        // clang-format off
		Array argv {
			"kandinsky",
			"--long_flag", "long_flag value",
			"--another_long_flag", "another_long_flag value",
			"-y", "yet_another value",
		};
        // clang-format on

        bool ok = ParseArguments(&ap, argv.Size, argv.DataPtr());
        REQUIRE(ok);

        {
            StringView value;
            REQUIRE(FindStringValue(ap, "long_flag"sv, &value));
            REQUIRE(value == "long_flag value"sv);
        }

        {
            StringView value;
            REQUIRE(FindStringValue(ap, "another_long_flag"sv, &value));
            REQUIRE(value == "another_long_flag value"sv);
        }

        {
            StringView value;
            REQUIRE(FindStringValue(ap, "yet_another"sv, &value));
            REQUIRE(value == "yet_another value"sv);
        }
    }

    SECTION("Required") {
        ArgParser ap = {};
        AddStringArgument(&ap, "long_flag"sv, NULL, false);
        AddStringArgument(&ap, "another_long_flag"sv, 'a', false);
        AddStringArgument(&ap, "yet_another"sv, 'y', false);
        AddStringArgument(&ap, "required"sv, NULL, true);

        // clang-format off
		Array argv {
			"kandinsky",
			"--long_flag", "long_flag value",
			"--another_long_flag", "another_long_flag value",
			"-y", "yet_another value",
		};
        // clang-format on

        bool ok = ParseArguments(&ap, argv.Size, argv.DataPtr());
        REQUIRE(!ok);
    }
}
