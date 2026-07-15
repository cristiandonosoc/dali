#include <dali/core/string.h>

#include <dali/core/container.h>
#include <dali/core/memory.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;

#define CREATE_ARENA()                                           \
    Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE); \
    DEFER { Arena::Free(&arena); };

// StringView --------------------------------------------------------------------------------------

TEST_CASE("StringView") {
    SECTION("Empty") {
        StringView empty(nullptr, 1000);
        CHECK(!empty.Equals(nullptr));
        CHECK(!empty.Equals(""));

        empty = StringView("");
        CHECK(!empty.Equals(nullptr));
        CHECK(empty.Equals(""));
    }

    SECTION("Comparison") {
        StringView some("0123456789");
        CHECK(some.Equals("0123456789"));
        CHECK(!some.Equals("012345678"));
        CHECK(some.Equals(some));

        StringView other = some;
        CHECK(other.Equals(some));

        other = StringView(some.Str(), 5);
        CHECK(!other.Equals(some));
        CHECK(other.Equals("01234"));
    }
}

// Tests begin here
TEST_CASE("StringView construction and basic properties", "[string]") {
    SECTION("Default constructor creates empty string") {
        StringView s;
        INFO("Default constructed StringView should have _Str equal to kEmptyStrPtr");
        CHECK(s._Str == StringView::kEmptyStrPtr);
        INFO("Default constructed StringView should have Size 0");
        CHECK(s.Size == 0);
        INFO("Default constructed StringView should be empty");
        CHECK(s.IsEmpty());
        INFO("Default constructed StringView should be valid");
        CHECK(s.IsValid());
        INFO("Default constructed StringView's Str() should return empty string");
        CHECK(std::strcmp(s.Str(), "") == 0);
    }

    SECTION("Construction from C-string") {
        const char* test_str = "Hello, World!";
        StringView s(test_str);

        INFO("StringView constructed from C-string should store the pointer");
        CHECK(s._Str == test_str);
        INFO("Size should be set to the length of the string");
        CHECK(s.Size == std::strlen(test_str));
        INFO("IsEmpty should return false for non-empty string");
        CHECK_FALSE(s.IsEmpty());
        INFO("IsValid should return true for non-null string");
        CHECK(s.IsValid());
        INFO("Str() should return the original string");
        CHECK(s.Str() == test_str);
    }

    SECTION("Construction with explicit size") {
        const char* test_str = "Hello, World!";
        u64 test_size = 5;  // Only "Hello"
        StringView s(test_str, test_size);

        INFO("StringView constructed with explicit size should store the pointer");
        CHECK(s._Str == test_str);
        INFO("Size should match the provided value");
        CHECK(s.Size == test_size);
        INFO("IsEmpty should return false when size > 0");
        CHECK_FALSE(s.IsEmpty());
        INFO("IsValid should return true for non-null string");
        CHECK(s.IsValid());
    }

    SECTION("Construction with nullptr") {
        StringView s(nullptr, 0);

        INFO("StringView constructed with nullptr should have null _Str");
        CHECK(s._Str == nullptr);
        INFO("StringView constructed with nullptr should have Size 0");
        CHECK(s.Size == 0);
        INFO("StringView constructed with nullptr should be empty");
        CHECK(s.IsEmpty());
        INFO("StringView constructed with nullptr should not be valid");
        CHECK_FALSE(s.IsValid());
        INFO("StringView constructed with nullptr's Str() should return empty string");
        CHECK(std::strcmp(s.Str(), "") == 0);
    }
}

TEST_CASE("StringView's Str() method", "[string]") {
    SECTION("Str() should return the string when _Str is not null") {
        const char* test_str = "Test StringView";
        StringView s(test_str);

        INFO("Str() should return the original string pointer");
        CHECK(s.Str() == test_str);
    }

    SECTION("Str() should return kEmptyStrPtr when _Str is null") {
        StringView s(nullptr, 0);

        INFO("Str() should return kEmptyStrPtr for null _Str");
        CHECK(s.Str() == StringView::kEmptyStrPtr);
        INFO("Str() should never return nullptr");
        CHECK(s.Str() != nullptr);
    }
}

TEST_CASE("StringView's IsEmpty() and IsValid() methods", "[string]") {
    SECTION("IsEmpty() should return true for empty strings") {
        StringView s1;
        StringView s2("", 0);
        StringView s3(nullptr, 0);

        INFO("Default constructed string should be empty");
        CHECK(s1.IsEmpty());
        INFO("StringView with empty C-string should be empty");
        CHECK(s2.IsEmpty());
        INFO("StringView with nullptr should be empty");
        CHECK(s3.IsEmpty());
    }

    SECTION("IsEmpty() should return false for non-empty strings") {
        StringView s1("Hello");
        StringView s2("", 5);  // This is a bit strange but Size is non-zero

        INFO("StringView with content should not be empty");
        CHECK_FALSE(s1.IsEmpty());
        INFO("StringView with non-zero size should not be empty, even if _Str is empty");
        CHECK_FALSE(s2.IsEmpty());
    }

    SECTION("IsValid() should return true for non-null _Str") {
        StringView s1;
        StringView s2("Hello");
        StringView s3("", 0);

        INFO("Default constructed string should be valid");
        CHECK(s1.IsValid());
        INFO("StringView with content should be valid");
        CHECK(s2.IsValid());
        INFO("StringView with empty C-string should be valid");
        CHECK(s3.IsValid());
    }

    SECTION("IsValid() should return false for null _Str") {
        StringView s(nullptr, 0);

        INFO("StringView with nullptr should not be valid");
        CHECK_FALSE(s.IsValid());
    }
}

TEST_CASE("StringView's Equals(const char*) method", "[string]") {
    SECTION("Equals should compare content for equality") {
        const char* test_str = "Test StringView";
        StringView s(test_str);

        INFO("Equals should return true for identical C-strings");
        CHECK(s.Equals(test_str));
        INFO("Equals should return true for C-strings with same content");
        CHECK(s.Equals("Test StringView"));
        INFO("Equals should return false for C-strings with different content");
        CHECK_FALSE(s.Equals("Different StringView"));
        INFO("Equals should return false for nullptr");
        CHECK_FALSE(s.Equals(nullptr));
    }

    SECTION("Equals should handle empty strings") {
        StringView empty1;
        StringView empty2("", 0);

        INFO("Empty string should equal empty C-string");
        CHECK(empty1.Equals(""));
        CHECK(empty2.Equals(""));
        INFO("Empty string should not equal non-empty C-string");
        CHECK_FALSE(empty1.Equals("Not Empty"));
        CHECK_FALSE(empty2.Equals("Not Empty"));
    }

    SECTION("Equals should respect Size limit") {
        const char* test_str = "Test StringView Long";
        StringView s(test_str, 4);  // Only "Test"

        INFO("Equals should only compare up to Size characters");
        CHECK(s.Equals("Test"));
        CHECK_FALSE(s.Equals("Tes"));
        CHECK_FALSE(s.Equals("Testing"));
        CHECK_FALSE(s.Equals("Test StringView Long"));
    }

    SECTION("Equals should handle invalid strings") {
        StringView invalid(nullptr, 0);

        INFO("Invalid string should not equal any C-string");
        CHECK_FALSE(invalid.Equals(""));
        CHECK_FALSE(invalid.Equals("Test"));
        CHECK_FALSE(invalid.Equals(nullptr));
    }
}

TEST_CASE("StringView's Equals(const StringView&) method", "[string]") {
    SECTION("Equals should compare content for equality") {
        const char* test_str = "Test StringView";
        StringView s1(test_str);
        StringView s2("Test StringView");
        StringView s3("Different StringView");

        INFO("Equals should return true for strings with same content");
        CHECK(s1.Equals(s2));
        CHECK(s2.Equals(s1));
        INFO("Equals should return false for strings with different content");
        CHECK_FALSE(s1.Equals(s3));
        CHECK_FALSE(s3.Equals(s1));
    }

    SECTION("Equals should handle empty strings") {
        StringView empty1;
        StringView empty2("", 0);
        StringView nonEmpty("Not Empty");

        INFO("Empty strings should equal each other");
        CHECK(empty1.Equals(empty2));
        CHECK(empty2.Equals(empty1));
        INFO("Empty string should not equal non-empty string");
        CHECK_FALSE(empty1.Equals(nonEmpty));
        CHECK_FALSE(nonEmpty.Equals(empty1));
    }

    SECTION("Equals should respect Size limit") {
        const char* test_str = "Test StringView Long";
        StringView s1(test_str, 4);  // Only "Test"
        StringView s2("Test");
        StringView s3("Test StringView Long");

        INFO("Equals should only compare up to Size characters");
        CHECK(s1.Equals(s2));
        CHECK(s2.Equals(s1));
        CHECK_FALSE(s1.Equals(s3));
        CHECK_FALSE(s3.Equals(s1));
    }

    SECTION("Equals should optimize for same pointer") {
        const char* test_str = "Test StringView";
        StringView s1(test_str);
        StringView s2(test_str);

        INFO("Strings with same pointer should be equal without character comparison");
        CHECK(s1.Equals(s2));
    }

    SECTION("Equals should handle invalid strings") {
        StringView valid("Test");
        StringView invalid(nullptr, 0);
        StringView invalid2(nullptr, 0);

        INFO("Valid string should not equal invalid string");
        CHECK_FALSE(valid.Equals(invalid));

        INFO("Two invalid strings with same size should be equal");
        CHECK(invalid.Equals(invalid2));
    }
}

TEST_CASE("StringView edge cases and corner cases", "[string]") {
    SECTION("Strings with size but empty content") {
        StringView s("", 5);

        INFO("StringView should not be empty if Size > 0 regardless of content");
        CHECK_FALSE(s.IsEmpty());
        INFO("StringView should be valid if _Str is not nullptr");
        CHECK(s.IsValid());

        INFO("StringView with empty content but Size > 0 should not equal empty string");
        CHECK_FALSE(s.Equals(""));
        CHECK_FALSE(s.Equals(StringView()));
    }

    SECTION("StringView with null terminator in the middle") {
        const char str[] = {'H', 'e', 'l', 'l', 'o', '\0', 'W', 'o', 'r', 'l', 'd'};
        StringView s(str, sizeof(str));

        INFO("Size should include characters after null terminator");
        CHECK(s.Size == sizeof(str));

        char comparison[sizeof(str)];
        std::memcpy(comparison, str, sizeof(str));
        StringView s2(comparison, sizeof(comparison));

        INFO("Equals should compare all characters including after null terminator");
        CHECK(s.Equals(s2));

        // Change a character after the null terminator
        comparison[7] = 'X';  // Change 'o' to 'X'
        StringView s3(comparison, sizeof(comparison));

        INFO("Equals should detect differences after null terminator");
        CHECK_FALSE(s.Equals(s3));
    }

    SECTION("StringView equality with different pointers but same content") {
        char str1[] = "Test StringView";
        char str2[] = "Test StringView";

        StringView s1(str1);
        StringView s2(str2);

        INFO("Different pointers with same content should be equal");
        CHECK(s1._Str != s2._Str);  // Confirm different pointers
        CHECK(s1.Equals(s2));
        CHECK(s2.Equals(s1));
    }
}

TEST_CASE("StringView equality with different lengths", "[string]") {
    SECTION("Different lengths should not be equal") {
        StringView s1("Test");
        StringView s2("Testing");

        INFO("Strings with different lengths should not be equal");
        CHECK_FALSE(s1.Equals(s2));
        CHECK_FALSE(s2.Equals(s1));

        // Test with C-string comparison too
        CHECK_FALSE(s1.Equals("Testing"));
        CHECK_FALSE(s2.Equals("Test"));
    }

    SECTION("Prefix match should not imply equality") {
        StringView s1("Test");
        StringView s2("Testing");

        INFO("StringView should not equal its prefix or extension");
        CHECK_FALSE(s1.Equals(s2));
        CHECK_FALSE(s2.Equals(s1));
    }
}

// Contains / ContainsCi ---------------------------------------------------------------------------

TEST_CASE("StringView::Contains", "[string][contains]") {
    SECTION("Empty needle is contained by everything") {
        CHECK(StringView("hello").Contains(StringView()));
        CHECK(StringView("hello").Contains(StringView("")));
        CHECK(StringView().Contains(StringView()));       // empty in empty
        CHECK(StringView("").Contains(StringView("")));
    }

    SECTION("Non-empty needle is never in an empty haystack") {
        CHECK_FALSE(StringView().Contains(StringView("a")));
        CHECK_FALSE(StringView("").Contains(StringView("a")));
    }

    SECTION("Substring found at start, middle, and end") {
        StringView s("textures/goblin/walk");
        CHECK(s.Contains(StringView("textures")));  // start
        CHECK(s.Contains(StringView("goblin")));    // middle
        CHECK(s.Contains(StringView("walk")));      // end
        CHECK(s.Contains(StringView("/goblin/")));  // spanning separators
    }

    SECTION("Whole-string and single-char matches") {
        StringView s("abc");
        CHECK(s.Contains(StringView("abc")));  // equal to haystack
        CHECK(s.Contains(StringView("a")));
        CHECK(s.Contains(StringView("b")));
        CHECK(s.Contains(StringView("c")));
    }

    SECTION("Absent substrings are not found") {
        StringView s("abcdef");
        CHECK_FALSE(s.Contains(StringView("xyz")));
        CHECK_FALSE(s.Contains(StringView("acf")));   // right chars, wrong order
        CHECK_FALSE(s.Contains(StringView("abcdefg")));  // needle longer than haystack
    }

    SECTION("Is case-sensitive") {
        StringView s("Goblin");
        CHECK(s.Contains(StringView("Gob")));
        CHECK_FALSE(s.Contains(StringView("gob")));
        CHECK_FALSE(s.Contains(StringView("GOBLIN")));
    }

    SECTION("Near-miss with a partial repeated prefix") {
        // The scan must keep sliding after a partial match at index 0.
        StringView s("abab_abc");
        CHECK(s.Contains(StringView("abc")));
        CHECK_FALSE(StringView("ababab").Contains(StringView("abc")));
    }

    SECTION("Respects Size, not null terminator") {
        StringView haystack("hello world", 5);  // only "hello"
        CHECK(haystack.Contains(StringView("hello")));
        CHECK_FALSE(haystack.Contains(StringView("world")));
        CHECK_FALSE(haystack.Contains(StringView("hello world")));

        StringView needle("worldwide", 5);  // only "world"
        CHECK(StringView("say world now").Contains(needle));
    }

    SECTION("Handles embedded null bytes") {
        // hay is {'a','\0','b','c'} — the search must not stop at the interior null.
        const char hay[] = {'a', '\0', 'b', 'c'};
        const char need[] = {'\0', 'b'};
        StringView h(hay, sizeof(hay));
        StringView n(need, sizeof(need));
        CHECK(h.Contains(n));                     // "\0b" at indices 1,2
        CHECK(h.Contains(StringView("bc", 2)));   // "bc" at indices 2,3
        CHECK_FALSE(h.Contains(StringView("ab", 2)));  // 'a' and 'b' straddle the null, not adjacent
    }
}

TEST_CASE("StringView::ContainsCi", "[string][contains]") {
    SECTION("Empty needle is contained by everything") {
        CHECK(StringView("Hello").ContainsCi(StringView()));
        CHECK(StringView().ContainsCi(StringView()));
    }

    SECTION("Non-empty needle is never in an empty haystack") {
        CHECK_FALSE(StringView().ContainsCi(StringView("a")));
    }

    SECTION("Matches regardless of case on either side") {
        StringView s("Textures/Goblin/Walk");
        CHECK(s.ContainsCi(StringView("goblin")));
        CHECK(s.ContainsCi(StringView("GOBLIN")));
        CHECK(s.ContainsCi(StringView("GoBlIn")));
        CHECK(StringView("goblin").ContainsCi(StringView("GOB")));
        CHECK(StringView("GOBLIN").ContainsCi(StringView("gob")));
    }

    SECTION("Still fails when the letters genuinely differ") {
        StringView s("goblin");
        CHECK_FALSE(s.ContainsCi(StringView("wolf")));
        CHECK_FALSE(s.ContainsCi(StringView("goblins")));  // longer than haystack
    }

    SECTION("Only ASCII letters are folded; digits and symbols compare literally") {
        StringView s("Clip_42");
        CHECK(s.ContainsCi(StringView("clip_42")));
        CHECK(s.ContainsCi(StringView("_42")));
        CHECK_FALSE(s.ContainsCi(StringView("clip-42")));  // '_' vs '-'
    }

    SECTION("Respects Size, not null terminator") {
        StringView haystack("HELLO world", 5);  // only "HELLO"
        CHECK(haystack.ContainsCi(StringView("hello")));
        CHECK_FALSE(haystack.ContainsCi(StringView("world")));
    }
}

TEST_CASE("Concat tests", "[string][concat]") {
    SECTION("Empty strings") {
        CREATE_ARENA();
        StringView a;
        StringView b;
        StringView result = Concat(&arena, a, b);
        CHECK(result.IsEmpty());
        CHECK(result.Size == 0);
    }

    SECTION("First string empty, second non-empty") {
        CREATE_ARENA();
        StringView a;
        StringView b("hello");
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 5);
        CHECK(std::strcmp(result.Str(), "hello") == 0);
    }

    SECTION("First string non-empty, second empty") {
        CREATE_ARENA();
        StringView a("world");
        StringView b;
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 5);
        CHECK(std::strcmp(result.Str(), "world") == 0);
    }

    SECTION("Both strings non-empty") {
        CREATE_ARENA();
        StringView a("hello");
        StringView b("world");
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 10);
        CHECK(std::strcmp(result.Str(), "helloworld") == 0);
    }

    SECTION("Concatenate with space") {
        CREATE_ARENA();
        StringView a("hello ");
        StringView b("world");
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 11);
        CHECK(std::strcmp(result.Str(), "hello world") == 0);
    }

    SECTION("Single character strings") {
        CREATE_ARENA();
        StringView a("a");
        StringView b("b");
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 2);
        CHECK(std::strcmp(result.Str(), "ab") == 0);
    }

    SECTION("Concatenate longer strings") {
        CREATE_ARENA();
        StringView a("The quick brown fox ");
        StringView b("jumps over the lazy dog");
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 43);
        CHECK(std::strcmp(result.Str(), "The quick brown fox jumps over the lazy dog") == 0);
    }

    SECTION("Concatenate strings with null bytes") {
        CREATE_ARENA();
        StringView a("hello\0world", 11);
        StringView b("test", 4);
        StringView result = Concat(&arena, a, b);
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Size == 15);
        // Note: strcmp won't work here due to null byte, so check manually
        CHECK(std::memcmp(result.Str(), "hello\0worldtest", 15) == 0);
    }

    SECTION("Result string properties") {
        CREATE_ARENA();
        StringView a("foo");
        StringView b("bar");
        StringView result = Concat(&arena, a, b);

        CHECK(result.IsValid());
        CHECK_FALSE(result.IsEmpty());
        CHECK(result.Str() != nullptr);
        CHECK(result.Str() != a.Str());  // Should be a new allocation
        CHECK(result.Str() != b.Str());  // Should be a new allocation
    }

    SECTION("Equality checks after concatenation") {
        CREATE_ARENA();
        StringView a("test");
        StringView b("ing");
        StringView result = Concat(&arena, a, b);
        StringView expected("testing");

        CHECK(result.Equals(expected));
        CHECK(result == expected);
        CHECK(result.Equals("testing"));
    }
}

// Subscript operator Tests ------------------------------------------------------------------------

TEST_CASE("StringView subscript operator - Basic access", "[string][subscript]") {
    SECTION("Access characters by index") {
        const char* test_str = "Hello";
        StringView s(test_str);

        INFO("Should access characters at valid indices");
        CHECK(s[0] == 'H');
        CHECK(s[1] == 'e');
        CHECK(s[2] == 'l');
        CHECK(s[3] == 'l');
        CHECK(s[4] == 'o');
    }

    SECTION("Access first and last characters") {
        const char* test_str = "Test";
        StringView s(test_str);

        INFO("Should access first character");
        CHECK(s[0] == 'T');
        INFO("Should access last character");
        CHECK(s[3] == 't');
    }

    SECTION("Access single character string") {
        const char* test_str = "X";
        StringView s(test_str);

        INFO("Should access single character");
        CHECK(s[0] == 'X');
    }
}

TEST_CASE("StringView subscript operator - With explicit size", "[string][subscript]") {
    SECTION("Access within explicit size limit") {
        const char* test_str = "Hello, World!";
        StringView s(test_str, 5);  // Only "Hello"

        INFO("Should only access characters within Size");
        CHECK(s[0] == 'H');
        CHECK(s[1] == 'e');
        CHECK(s[2] == 'l');
        CHECK(s[3] == 'l');
        CHECK(s[4] == 'o');
    }

    SECTION("Size limit respected") {
        const char* test_str = "0123456789";
        StringView s(test_str, 3);  // Only "012"

        INFO("Should access first three characters");
        CHECK(s[0] == '0');
        CHECK(s[1] == '1');
        CHECK(s[2] == '2');
    }
}

TEST_CASE("StringView subscript operator - Special characters", "[string][subscript]") {
    SECTION("Access string with spaces") {
        const char* test_str = "A B C";
        StringView s(test_str);

        INFO("Should handle spaces correctly");
        CHECK(s[0] == 'A');
        CHECK(s[1] == ' ');
        CHECK(s[2] == 'B');
        CHECK(s[3] == ' ');
        CHECK(s[4] == 'C');
    }

    SECTION("Access string with special characters") {
        const char* test_str = "!@#$%";
        StringView s(test_str);

        INFO("Should handle special characters");
        CHECK(s[0] == '!');
        CHECK(s[1] == '@');
        CHECK(s[2] == '#');
        CHECK(s[3] == '$');
        CHECK(s[4] == '%');
    }

    SECTION("Access string with null byte in middle") {
        const char str[] = {'A', 'B', '\0', 'C', 'D'};
        StringView s(str, 5);

        INFO("Should access characters including null byte");
        CHECK(s[0] == 'A');
        CHECK(s[1] == 'B');
        CHECK(s[2] == '\0');
        CHECK(s[3] == 'C');
        CHECK(s[4] == 'D');
    }
}

TEST_CASE("StringView subscript operator - Use in expressions", "[string][subscript]") {
    SECTION("Use subscript in comparisons") {
        StringView s("Hello");

        INFO("Should work in comparison expressions");
        CHECK(s[0] == 'H');
        CHECK(s[0] != 'h');
        CHECK(s[0] < 'Z');
        CHECK(s[4] > 'a');
    }

    SECTION("Use subscript to build new string") {
        StringView s("Hello");
        std::string result;

        INFO("Should be usable to construct new strings");
        for (u64 i = 0; i < s.Size; i++) {
            result += s[i];
        }
        CHECK(result == "Hello");
    }

    SECTION("Compare characters from different strings") {
        StringView s1("ABC");
        StringView s2("ABZ");

        INFO("Should compare characters from different StringView objects");
        CHECK(s1[0] == s2[0]);
        CHECK(s1[1] == s2[1]);
        CHECK(s1[2] != s2[2]);
    }
}

TEST_CASE("StringView subscript operator - Edge cases", "[string][subscript]") {
    SECTION("Sequential access matches iterator") {
        const char* test_str = "Test123";
        StringView s(test_str);

        INFO("Subscript access should match iterator access");
        u64 index = 0;
        for (char c : s) {
            CHECK(c == s[index]);
            index++;
        }
        CHECK(index == s.Size);
    }

    SECTION("Access with numbers and letters") {
        StringView s("a1b2c3");

        INFO("Should handle mixed alphanumeric content");
        CHECK(s[0] == 'a');
        CHECK(s[1] == '1');
        CHECK(s[2] == 'b');
        CHECK(s[3] == '2');
        CHECK(s[4] == 'c');
        CHECK(s[5] == '3');
    }

    SECTION("Returned reference is const") {
        const char* test_str = "Test";
        StringView s(test_str);

        INFO("Should return const reference");
        const char& ref = s[0];
        CHECK(ref == 'T');
        // Note: Cannot modify through ref since it's const
    }
}

// Iterator API Tests ------------------------------------------------------------------------------

TEST_CASE("StringView iterator API - Basic iteration", "[string][iterator]") {
    SECTION("Iterate over non-empty string") {
        const char* test_str = "Hello";
        StringView s(test_str);

        INFO("Should be able to iterate over characters");
        std::string result;
        for (char c : s) {
            result += c;
        }
        CHECK(result == "Hello");
    }

    SECTION("Iterate over empty string") {
        StringView s;

        INFO("Should handle iteration over empty string");
        int count = 0;
        for (char c : s) {
            (void)c;  // Suppress unused variable warning
            count++;
        }
        CHECK(count == 0);
    }

    SECTION("Iterate over string with explicit size") {
        const char* test_str = "Hello, World!";
        StringView s(test_str, 5);  // Only "Hello"

        INFO("Should only iterate over Size characters");
        std::string result;
        for (char c : s) {
            result += c;
        }
        CHECK(result == "Hello");
        CHECK(result.length() == 5);
    }
}

TEST_CASE("StringView iterator API - begin() and end()", "[string][iterator]") {
    SECTION("begin() returns pointer to first character") {
        const char* test_str = "Test";
        StringView s(test_str);

        INFO("begin() should return pointer to first character");
        CHECK(s.begin() == test_str);
        CHECK(*s.begin() == 'T');
    }

    SECTION("end() returns pointer past last character") {
        const char* test_str = "Test";
        StringView s(test_str, 4);

        INFO("end() should return pointer past last character");
        CHECK(s.end() == test_str + 4);
        CHECK(s.end() - s.begin() == 4);
    }

    SECTION("begin() and end() for empty string") {
        StringView s;

        INFO("For empty string, begin() and end() should point to same location");
        CHECK(s.begin() == s.end());
        CHECK(s.end() - s.begin() == 0);
    }

    SECTION("begin() and end() for null string") {
        StringView s;

        INFO("For null string, end() should be kEmtpyStrPtr");
        CHECK(s.begin() == StringView::kEmptyStrPtr);
        CHECK(s.end() == StringView::kEmptyStrPtr);
    }
}

TEST_CASE("StringView iterator API - Manual iteration", "[string][iterator]") {
    SECTION("Manual iteration with pointers") {
        const char* test_str = "ABC";
        StringView s(test_str);

        INFO("Should be able to manually iterate");
        std::string result;
        for (const char* it = s.begin(); it != s.end(); ++it) {
            result += *it;
        }
        CHECK(result == "ABC");
    }

    SECTION("Count characters using iterators") {
        const char* test_str = "Count Me!";
        StringView s(test_str);

        INFO("Should be able to count characters");
        size_t count = 0;
        for (const char* it = s.begin(); it != s.end(); ++it) {
            count++;
        }
        CHECK(count == 9);
        CHECK(count == s.Size);
    }

    SECTION("Find character using iterators") {
        const char* test_str = "Find the space";
        StringView s(test_str);

        INFO("Should be able to find specific character");
        const char* space_ptr = nullptr;
        for (const char* it = s.begin(); it != s.end(); ++it) {
            if (*it == ' ') {
                space_ptr = it;
                break;
            }
        }
        CHECK(space_ptr != nullptr);
        CHECK(*space_ptr == ' ');
        CHECK(space_ptr - s.begin() == 4);  // Space is at index 4
    }
}

TEST_CASE("StringView iterator API - Special cases", "[string][iterator]") {
    SECTION("StringView with null bytes") {
        const char str[] = {'A', '\0', 'B', 'C'};
        StringView s(str, 4);

        INFO("Should iterate over all bytes including null");
        int count = 0;
        char expected[] = {'A', '\0', 'B', 'C'};
        int index = 0;
        for (char c : s) {
            CHECK(c == expected[index]);
            index++;
            count++;
        }
        CHECK(count == 4);
    }

    SECTION("Single character string") {
        const char* test_str = "X";
        StringView s(test_str);

        INFO("Should handle single character");
        int count = 0;
        char result = '\0';
        for (char c : s) {
            result = c;
            count++;
        }
        CHECK(count == 1);
        CHECK(result == 'X');
    }

    SECTION("StringView with only whitespace") {
        const char* test_str = "   ";
        StringView s(test_str);

        INFO("Should iterate over whitespace characters");
        int count = 0;
        for (char c : s) {
            CHECK(c == ' ');
            count++;
        }
        CHECK(count == 3);
    }
}

TEST_CASE("StringView iterator API - STL algorithm compatibility", "[string][iterator]") {
    SECTION("std::find with iterators") {
        const char* test_str = "Hello World";
        StringView s(test_str);

        INFO("Should work with std::find");
        auto it = std::find(s.begin(), s.end(), 'W');
        CHECK(it != s.end());
        CHECK(*it == 'W');
        CHECK(it - s.begin() == 6);
    }

    SECTION("std::count with iterators") {
        const char* test_str = "Mississippi";
        StringView s(test_str);

        INFO("Should work with std::count");
        auto count = std::count(s.begin(), s.end(), 's');
        CHECK(count == 4);
    }

    SECTION("std::all_of with iterators") {
        const char* test_str = "12345";
        StringView s(test_str);

        INFO("Should work with std::all_of");
        bool all_digits =
            std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        CHECK(all_digits);
    }

    SECTION("std::any_of with iterators") {
        const char* test_str = "abc123";
        StringView s(test_str);

        INFO("Should work with std::any_of");
        bool has_digit =
            std::any_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        CHECK(has_digit);
    }

    SECTION("std::copy with iterators") {
        const char* test_str = "Copy Me";
        StringView s(test_str);

        INFO("Should work with std::copy");
        FixedVector<char, 100> buffer;
        for (char c : s) {
            buffer.Push(c);
        }
        CHECK((u64)buffer.Size == s.Size);
        CHECK(std::equal(buffer.begin(), buffer.end(), s.begin()));
    }
}

TEST_CASE("StringView iterator API - Distance and advancement", "[string][iterator]") {
    SECTION("Calculate distance between iterators") {
        const char* test_str = "Distance";
        StringView s(test_str);

        INFO("Should be able to calculate distance");
        auto distance = std::distance(s.begin(), s.end());
        CHECK(distance == 8);
        CHECK(distance == s.Size);
    }

    SECTION("Advance iterator") {
        const char* test_str = "Advance";
        StringView s(test_str);

        INFO("Should be able to advance iterator");
        auto it = s.begin();
        std::advance(it, 3);
        CHECK(*it == 'a');
        CHECK(it - s.begin() == 3);
    }

    SECTION("Iterator arithmetic") {
        const char* test_str = "Math";
        StringView s(test_str);

        INFO("Should support pointer arithmetic");
        auto it = s.begin();
        CHECK(*(it + 0) == 'M');
        CHECK(*(it + 1) == 'a');
        CHECK(*(it + 2) == 't');
        CHECK(*(it + 3) == 'h');
        CHECK(it + 4 == s.end());
    }
}


