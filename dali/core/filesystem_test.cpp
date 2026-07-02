#include <dali/core/filesystem.h>

#include <dali/core/memory.h>
#include <dali/core/string.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;
using namespace kdk::paths;

#define CREATE_ARENA()                                           \
    Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE); \
    DEFER { Arena::Free(&arena); };

// Paths -------------------------------------------------------------------------------------------

TEST_CASE("GetDirname tests", "[path]") {
    SECTION("Empty path") {
        CREATE_ARENA();
        StringView path;
        StringView result = GetDirname(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with no directory component") {
        CREATE_ARENA();
        StringView path("filename.txt");
        StringView result = GetDirname(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with directory component") {
        CREATE_ARENA();
        StringView path("/path/to/file.txt");
        StringView result = GetDirname(&arena, path);
        const char* want = "/path/to/";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with trailing slash") {
        CREATE_ARENA();
        StringView path("/path/to/");
        StringView result = GetDirname(&arena, path);
        const char* want = "/path/";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Root directory") {
        CREATE_ARENA();
        StringView path("/file.txt");
        StringView result = GetDirname(&arena, path);
        const char* want = "/";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Windows-style paths") {
        CREATE_ARENA();
        StringView path("C:\\path\\to\\file.txt");
        StringView result = GetDirname(&arena, path);
        const char* want = "C:\\path\\to\\";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Windows-style paths: Root") {
        CREATE_ARENA();
        StringView path("C:\\");
        StringView result = GetDirname(&arena, path);
        const char* want = "";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with multiple slashes") {
        CREATE_ARENA();
        StringView path("/path//to/file.txt");
        StringView result = GetDirname(&arena, path);
        const char* want = "/path//to/";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("GetBasename tests", "[path]") {
    SECTION("Empty path") {
        CREATE_ARENA();
        StringView path;
        StringView result = GetBasename(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with no directory component") {
        CREATE_ARENA();
        StringView path("filename.txt");
        StringView result = GetBasename(&arena, path);
        const char* want = "filename.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with directory component") {
        CREATE_ARENA();
        StringView path("/path/to/file.txt");
        StringView result = GetBasename(&arena, path);
        const char* want = "file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with trailing slash") {
        CREATE_ARENA();
        StringView path("/path/to/");
        StringView result = GetBasename(&arena, path);
        const char* want = "to";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Root directory") {
        CREATE_ARENA();
        StringView path("/");
        StringView result = GetBasename(&arena, path);
        const char* want = "";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Windows-style paths") {
        CREATE_ARENA();
        StringView path("C:\\path\\to\\file.txt");
        StringView result = GetBasename(&arena, path);
        const char* want = "file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Windows-style paths: Dir") {
        CREATE_ARENA();
        StringView path("C:\\path\\to\\");
        StringView result = GetBasename(&arena, path);
        const char* want = "to";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with multiple extensions") {
        CREATE_ARENA();
        StringView path("/path/to/file.tar.gz");
        StringView result = GetBasename(&arena, path);
        const char* want = "file.tar.gz";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("GetExtension tests", "[path]") {
    SECTION("Empty path") {
        CREATE_ARENA();
        StringView path;
        StringView result = GetExtension(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with no extension") {
        CREATE_ARENA();
        StringView path("filename");
        StringView result = GetExtension(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with extension") {
        CREATE_ARENA();
        StringView path("filename.txt");
        StringView result = GetExtension(&arena, path);
        const char* want = ".txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with multiple extensions") {
        CREATE_ARENA();
        StringView path("filename.tar.gz");
        StringView result = GetExtension(&arena, path);
        const char* want = ".gz";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with directory and extension") {
        CREATE_ARENA();
        StringView path("/path/to/file.txt");
        StringView result = GetExtension(&arena, path);
        const char* want = ".txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with dot in directory name") {
        CREATE_ARENA();
        StringView path("/path.with.dots/filename");
        StringView result = GetExtension(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with hidden file (dot at start)") {
        CREATE_ARENA();
        StringView path(".hidden");
        StringView result = GetExtension(&arena, path);
        const char* want = ".hidden";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with hidden file with extension") {
        CREATE_ARENA();
        StringView path(".hidden.txt");
        StringView result = GetExtension(&arena, path);
        const char* want = ".txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("RemoveExtension tests", "[path]") {
    SECTION("Empty path") {
        CREATE_ARENA();
        StringView path;
        StringView result = RemoveExtension(&arena, path);
        CHECK(result.IsEmpty());
    }

    SECTION("Path with no extension") {
        CREATE_ARENA();
        StringView path("filename");
        StringView result = RemoveExtension(&arena, path);
        const char* want = "filename";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with extension") {
        CREATE_ARENA();
        StringView path("filename.txt");
        StringView result = RemoveExtension(&arena, path);
        const char* want = "filename";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with multiple extensions") {
        CREATE_ARENA();
        StringView path("filename.tar.gz");
        StringView result = RemoveExtension(&arena, path);
        const char* want = "filename.tar";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with directory and extension") {
        CREATE_ARENA();
        StringView path("/path/to/file.txt");
        StringView result = RemoveExtension(&arena, path);
        const char* want = "/path/to/file";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with dot in directory name") {
        CREATE_ARENA();
        StringView path("/path.with.dots/filename.txt");
        StringView result = RemoveExtension(&arena, path);
        const char* want = "/path.with.dots/filename";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with hidden file (dot at start)") {
        CREATE_ARENA();
        StringView path(".hidden");
        StringView result = RemoveExtension(&arena, path);
        const char* want = ".hidden";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Path with hidden file with extension") {
        CREATE_ARENA();
        StringView path(".hidden.txt");
        StringView result = RemoveExtension(&arena, path);
        const char* want = ".hidden";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("ChangeExtension - Basic functionality", "[ChangeExtension]") {
    SECTION("Simple file with extension") {
        CREATE_ARENA();
        StringView original = StringView("file.txt");
        StringView new_ext = StringView(".cpp");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "file.cpp"sv);
    }

    SECTION("File with multiple dots") {
        CREATE_ARENA();
        StringView original = StringView("my.config.json");
        StringView new_ext = StringView(".xml");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "my.config.xml"sv);
    }

    SECTION("File without extension") {
        CREATE_ARENA();
        StringView original = StringView("README");
        StringView new_ext = StringView(".md");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "README.md"sv);
    }
}

TEST_CASE("ChangeExtension - Edge cases", "[ChangeExtension]") {
    SECTION("Empty new extension returns original") {
        CREATE_ARENA();
        StringView original = StringView("file.txt");
        StringView new_ext = StringView("");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "file.txt"sv);
    }

    SECTION("Empty original with extension") {
        CREATE_ARENA();
        StringView original = StringView("");
        StringView new_ext = StringView(".txt");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == ".txt"sv);
    }

    SECTION("Hidden file (starts with dot)") {
        CREATE_ARENA();
        StringView original = StringView(".gitignore");
        StringView new_ext = StringView(".bak");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == ".gitignore.bak"sv);
    }

    SECTION("Hidden file with extension") {
        CREATE_ARENA();
        StringView original = StringView(".config.json");
        StringView new_ext = StringView(".xml");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == ".config.xml"sv);
    }
}

TEST_CASE("ChangeExtension - Path handling", "[ChangeExtension]") {
    SECTION("Full path with extension") {
        CREATE_ARENA();
        StringView original = StringView("/home/user/documents/file.txt");
        StringView new_ext = StringView(".cpp");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "/home/user/documents/file.cpp"sv);
    }

    SECTION("Relative path with extension") {
        CREATE_ARENA();
        StringView original = StringView("../src/main.c");
        StringView new_ext = StringView(".o");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "../src/main.o"sv);
    }

    SECTION("Windows path with extension") {
        CREATE_ARENA();
        StringView original = StringView("C:\\Users\\Name\\file.txt");
        StringView new_ext = StringView(".bak");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "C:\\Users\\Name\\file.bak"sv);
    }
}

TEST_CASE("ChangeExtension - Extension variations", "[ChangeExtension]") {
    SECTION("File ending with dot") {
        CREATE_ARENA();
        StringView original = StringView("file.");
        StringView new_ext = StringView(".txt");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "file.txt"sv);
    }

    SECTION("Multiple consecutive dots in filename") {
        CREATE_ARENA();
        StringView original = StringView("file...old");
        StringView new_ext = StringView(".new");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "file...new"sv);
    }

    SECTION("Very long filename") {
        CREATE_ARENA();
        StringView original = StringView("very_long_filename_with_many_characters.txt");
        StringView new_ext = StringView(".backup");
        StringView result = ChangeExtension(&arena, original, new_ext);
        REQUIRE(result == "very_long_filename_with_many_characters.backup"sv);
    }
}

TEST_CASE("PathJoin basic functionality", "[pathjoin]") {
    SECTION("Joining two non-empty paths") {
        CREATE_ARENA();
        StringView a("dir");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should join paths with a separator");
        const char* want = "dir\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining path with trailing slash") {
        CREATE_ARENA();
        StringView a("dir/");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should preserve trailing slash and not add another");
        const char* want = "dir\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining with absolute path as second parameter") {
        CREATE_ARENA();
        StringView a("dir");
        StringView b("/file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should handle absolute path as second parameter");
        const char* want = "dir\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

// TODO(cdc): For the PathJoin tests we use windows \\ as separators, but we should do something
//            Platform independent. These tests will fail on Linux, even though the logic is most
//            likely correct.

TEST_CASE("PathJoin with empty inputs", "[pathjoin]") {
    SECTION("First path is empty") {
        CREATE_ARENA();
        StringView a;
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should return second path when first is empty");
        CHECK(result._Str == b._Str);  // Should return the exact same string object

        const char* want = "file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Second path is empty") {
        CREATE_ARENA();
        StringView a("dir");
        StringView b;
        StringView result = PathJoin(&arena, a, b);

        INFO("Should return first path when second is empty");
        CHECK(result._Str == a._Str);  // Should return the exact same string object

        const char* want = "dir";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Both paths are empty") {
        CREATE_ARENA();
        StringView a;
        StringView b;
        StringView result = PathJoin(&arena, a, b);

        INFO("Should return empty string when both inputs are empty");
        CHECK(result.IsEmpty());
    }
}

TEST_CASE("PathJoin with nested paths", "[pathjoin]") {
    SECTION("Joining multiple directory levels") {
        CREATE_ARENA();
        StringView a("dir1/dir2");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should correctly join nested directories");
        const char* want = "dir1\\dir2\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining path with filename and extension") {
        CREATE_ARENA();
        StringView a("dir");
        StringView b("subdir/file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should join path with subdirectory and filename");
        const char* want = "dir\\subdir\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("PathJoin with different slashes", "[pathjoin]") {
    SECTION("Joining paths with forward slashes") {
        CREATE_ARENA();
        StringView a("dir/");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should handle forward slashes correctly");
        const char* want = "dir\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining paths with backslashes") {
        CREATE_ARENA();
        StringView a("dir\\");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should handle backslashes correctly");
        const char* want = "dir\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining paths with mixed slashes") {
        CREATE_ARENA();
        StringView a("dir1/dir2\\");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should handle mixed slashes according to implementation");
        const char* want = "dir1\\dir2\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("PathJoin special cases", "[pathjoin]") {
    SECTION("Joining root directory") {
        CREATE_ARENA();
        StringView a("/");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should correctly join root directory");
        const char* want = "\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining with current directory") {
        CREATE_ARENA();
        StringView a(".");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should handle current directory reference");
        const char* want = "file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }

    SECTION("Joining with parent directory") {
        CREATE_ARENA();
        StringView a("..");
        StringView b("file.txt");
        StringView result = PathJoin(&arena, a, b);

        INFO("Should handle parent directory reference");
        const char* want = "..\\file.txt";
        INFO("Want: " << want << ", Got: " << result.Str());
        CHECK(result.Equals(want));
    }
}

TEST_CASE("RemovePrefix - Basic functionality", "[paths][RemovePrefix]") {
    SECTION("Remove Windows-style prefix") {
        CREATE_ARENA();
        StringView path = StringView("C:\\some\\path\\that\\I\\want\\to\\cut.asset");
        StringView prefix = StringView("C:\\some\\path");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "that\\I\\want\\to\\cut.asset") == 0);
    }

    SECTION("Remove Unix-style prefix") {
        CREATE_ARENA();
        StringView path = StringView("/home/user/documents/file.txt");
        StringView prefix = StringView("/home/user");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "documents/file.txt") == 0);
    }

    SECTION("Remove prefix with trailing separator") {
        CREATE_ARENA();
        StringView path = StringView("C:\\projects\\myapp\\src\\main.cpp");
        StringView prefix = StringView("C:\\projects\\myapp\\");  // Note trailing separator
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "src\\main.cpp") == 0);
    }
}

TEST_CASE("RemovePrefix - Edge cases", "[paths][RemovePrefix]") {
    SECTION("Empty path") {
        CREATE_ARENA();
        StringView path = StringView();
        StringView prefix = StringView("C:\\some\\path");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.IsEmpty());
    }

    SECTION("Empty prefix") {
        CREATE_ARENA();
        StringView path = StringView("C:\\some\\path\\file.txt");
        StringView prefix = StringView();
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "C:\\some\\path\\file.txt") == 0);
    }

    SECTION("Both empty") {
        CREATE_ARENA();
        StringView path = StringView();
        StringView prefix = StringView();
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.IsEmpty());
    }

    SECTION("Prefix longer than path") {
        CREATE_ARENA();
        StringView path = StringView("short");
        StringView prefix = StringView("this/is/a/much/longer/prefix");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "short") == 0);
    }

    SECTION("Prefix equals path exactly") {
        CREATE_ARENA();
        StringView path = StringView("C:\\exact\\match");
        StringView prefix = StringView("C:\\exact\\match");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.IsEmpty());
    }

    SECTION("Prefix with only separator remaining") {
        CREATE_ARENA();
        StringView path = StringView("C:\\root\\");
        StringView prefix = StringView("C:\\root");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.IsEmpty());
    }
}

TEST_CASE("RemovePrefix - No match cases", "[paths][RemovePrefix]") {
    SECTION("Prefix not at start of path") {
        CREATE_ARENA();
        StringView path = StringView("C:\\some\\path\\that\\contains\\some\\path\\again");
        StringView prefix = StringView("some\\path");  // This appears in middle, not start
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "C:\\some\\path\\that\\contains\\some\\path\\again") == 0);
    }

    SECTION("Partial prefix match") {
        CREATE_ARENA();
        StringView path = StringView("C:\\some\\pathological\\case");
        StringView prefix =
            StringView("C:\\some\\path");  // Partial match: "path" vs "pathological"
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "ological\\case") == 0);
    }

    SECTION("Completely different paths") {
        CREATE_ARENA();
        StringView path = StringView("/usr/local/bin/app");
        StringView prefix = StringView("C:\\Windows\\System32");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "/usr/local/bin/app") == 0);
    }

    SECTION("Case sensitivity test") {
        CREATE_ARENA();
        StringView path = StringView("C:\\Some\\Path\\File.txt");
        StringView prefix = StringView("c:\\some\\path");  // Different case
        StringView result = RemovePrefix(&arena, path, prefix);

        // Should not match due to case sensitivity (adjust if your implementation is
        // case-insensitive)
        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "C:\\Some\\Path\\File.txt") == 0);
    }
}

TEST_CASE("RemovePrefix - Mixed separator styles", "[paths][RemovePrefix]") {
    SECTION("Mixed separators in path") {
        CREATE_ARENA();
        StringView path = StringView("C:\\some/mixed\\separators/file.txt");
        StringView prefix = StringView("C:\\some");
        StringView result = RemovePrefix(&arena, path, prefix);

        // Result depends on how PathJoin normalizes separators
        REQUIRE(result.Str() != nullptr);
        REQUIRE(result.Size > 0);
        // Note: Exact result depends on your path normalization implementation
    }

    SECTION("Mixed separators in prefix") {
        CREATE_ARENA();
        StringView path = StringView("C:\\projects\\myapp\\src");
        StringView prefix = StringView("C:/projects/myapp");  // Unix-style separators
        StringView result = RemovePrefix(&arena, path, prefix);

        // Should work if PathJoin normalizes both paths consistently
        REQUIRE(result.Str() != nullptr);
        // Result depends on normalization - might be "src" or something similar
    }
}

TEST_CASE("RemovePrefix - Special characters", "[paths][RemovePrefix]") {
    SECTION("Paths with spaces") {
        CREATE_ARENA();
        StringView path = StringView("C:\\Program Files\\My App\\data\\file.dat");
        StringView prefix = StringView("C:\\Program Files\\My App");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "data\\file.dat") == 0);
    }

    SECTION("Paths with Unicode characters") {
        CREATE_ARENA();
        StringView path = StringView("C:\\Users\\José\\Documents\\файл.txt");
        StringView prefix = StringView("C:\\Users\\José");
        StringView result = RemovePrefix(&arena, path, prefix);

        REQUIRE(result.Str() != nullptr);
        REQUIRE(strcmp(result.Str(), "Documents\\файл.txt") == 0);
    }
}
