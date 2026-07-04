#include <dali/core/container.h>

#include <dali/core/memory.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;

// Tests start here
TEST_CASE("DynArray basic operations", "[dynarray]") {
    SECTION("Default initialized DynArray") {
        DynArray<int> array = {};
        REQUIRE(array._Data == nullptr);
        REQUIRE(array.Size == 0);
        REQUIRE(array.Cap == 0);
        REQUIRE(!array.IsValid());

        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };
    }

    SECTION("Creating a new DynArray") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);
        REQUIRE(array._Data != nullptr);
        REQUIRE(array.Size == 0);
        REQUIRE(array.Cap == kDynArrayInitialCap);
    }

    SECTION("Pushing elements") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);

        auto& ref1 = array.Push(42);
        REQUIRE(array.Size == 1);
        REQUIRE(array.Cap == kDynArrayInitialCap);
        REQUIRE(ref1 == 42);

        auto& ref2 = array.Push(100);
        REQUIRE(array.Size == 2);
        REQUIRE(ref2 == 100);

        REQUIRE(array[0] == 42);
        REQUIRE(array[1] == 100);
    }

    SECTION("Popping elements") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);
        array.Push(10);
        array.Push(20);
        array.Push(30);

        REQUIRE(array.Size == 3);

        int val = array.Pop();
        REQUIRE(val == 30);
        REQUIRE(array.Size == 2);

        val = array.Pop();
        REQUIRE(val == 20);
        REQUIRE(array.Size == 1);

        val = array.Pop();
        REQUIRE(val == 10);
        REQUIRE(array.Size == 0);
    }

    SECTION("Popping from empty array") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);

        int val = array.Pop();
        REQUIRE(val == 0);  // Default-constructed int
    }

    SECTION("Subscript operator access") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);

        array.Push(5);
        array.Push(10);

        REQUIRE(array[0] == 5);
        REQUIRE(array[1] == 10);

        // Modify through reference
        array[0] = 50;
        REQUIRE(array[0] == 50);
    }
}

TEST_CASE("DynArray with non-trivial types", "[dynarray]") {
    SECTION("Using strings") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<std::string>(&arena);

        array.Push("Hello");
        array.Push("World");

        REQUIRE(array.Size == 2);
        REQUIRE(array[0] == "Hello");
        REQUIRE(array[1] == "World");

        std::string popped = array.Pop();
        REQUIRE(popped == "World");
        REQUIRE(array.Size == 1);
    }

    SECTION("Capacity expansion with non-trivial type") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<std::string>(&arena);

        // Fill beyond initial capacity to test reallocation
        for (int i = 0; i < 10; i++) {
            array.Push("String " + std::to_string(i));
        }

        REQUIRE(array.Size == 10);
        REQUIRE(array.Cap >= 10);

        // Verify all strings are intact after reallocation
        for (i32 i = 0; i < array.Size; i++) {
            REQUIRE(array[i] == "String " + std::to_string(i));
        }
    }

    SECTION("Move semantics") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        struct MoveOnly {
            std::string data;
            bool moved = false;

            MoveOnly() = default;
            MoveOnly(const std::string& s) : data(s) {}
            MoveOnly(const MoveOnly& other) = default;
            MoveOnly& operator=(const MoveOnly&) = delete;

            MoveOnly(MoveOnly&& other) noexcept : data(std::move(other.data)) {
                other.moved = true;
            }

            MoveOnly& operator=(MoveOnly&& other) noexcept {
                data = std::move(other.data);
                other.moved = true;
                return *this;
            }
        };

        auto array = NewDynArray<MoveOnly>(&arena);

        array.Push(MoveOnly("test1"));
        array.Push(MoveOnly("test2"));

        REQUIRE(array.Size == 2);
        REQUIRE(array[0].data == "test1");
        REQUIRE(array[1].data == "test2");

        MoveOnly popped = array.Pop();
        REQUIRE(popped.data == "test2");
        REQUIRE(array.Size == 1);
    }
}

TEST_CASE("DynArray capacity expansion", "[dynarray]") {
    SECTION("Initial capacity") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);

        auto array = NewDynArray<int>(&arena);

        REQUIRE(array.Cap == kDynArrayInitialCap);  // Should allocate initial capacity
        REQUIRE(arena.Offset == 4 * sizeof(int));

        // Fill in just until capacity.
        u32 cap = array.Cap;
        for (u32 i = 0; i < cap; i++) {
            array.Push(i);
            REQUIRE(array.Size == i + 1);
            REQUIRE(array.Cap == cap);
        }

        // When we hit capacity, it should double.
        array.Push(1001);
        REQUIRE(array.Cap == 2 * kDynArrayInitialCap);
        REQUIRE(array.Size == cap + 1);
        REQUIRE(array.Last() == 1001);

        u32 size = array.Size;
        cap = array.Cap;
        for (u32 i = size; i < cap; i++) {
            array.Push(i);
            REQUIRE(array.Cap == 2 * kDynArrayInitialCap);
            REQUIRE(array.Size == i + 1);
        }

        // Adding one more should double.
        array.Push(2001);
        REQUIRE(array.Cap == 4 * kDynArrayInitialCap);
        REQUIRE(array.Size == cap + 1);
        REQUIRE(array.Last() == 2001);
    }
}

TEST_CASE("DynArray Reserve operations", "[dynarray]") {
    SECTION("Reserve with empty array") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);
        REQUIRE(array.Cap == kDynArrayInitialCap);

        array.Reserve(16);
        REQUIRE(array.Cap == 16);
        REQUIRE(array.Size == 0);
        REQUIRE(array._Data != nullptr);
    }

    SECTION("Reserve with existing elements") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena);

        // Add some elements
        array.Push(1);
        array.Push(2);
        array.Push(3);

        u32 originalSize = array.Size;
        array.Reserve(20);

        REQUIRE(array.Cap == 20);
        REQUIRE(array.Size == originalSize);
        // Verify elements are preserved
        REQUIRE(array[0] == 1);
        REQUIRE(array[1] == 2);
        REQUIRE(array[2] == 3);
    }

    SECTION("Reserve with non-trivial type") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<std::string>(&arena);

        array.Push("test1");
        array.Push("test2");

        array.Reserve(10);
        REQUIRE(array.Cap == 10);
        REQUIRE(array.Size == 2);
        REQUIRE(array[0] == "test1");
        REQUIRE(array[1] == "test2");
    }

    SECTION("Reserve smaller than current capacity") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        auto array = NewDynArray<int>(&arena, 8);
        u32 originalCap = array.Cap;

        array.Reserve(4);                   // Try to reserve less than current capacity
        REQUIRE(array.Cap == originalCap);  // Should not shrink
    }
}

TEST_CASE("Array basic operations", "[Array]") {
    SECTION("Default initialization") {
        Array<int, 5> arr;
        arr.DefaultInitialize();
        REQUIRE(arr.Size == 5);
        for (i32 i = 0; i < 5; i++) {
            REQUIRE(arr[i] == 0);
        }
    }

    SECTION("Aggregate initialization") {
        Array<int, 4> arr = {1, 2, 3, 4};
        REQUIRE(arr[0] == 1);
        REQUIRE(arr[1] == 2);
        REQUIRE(arr[2] == 3);
        REQUIRE(arr[3] == 4);
    }

    SECTION("Partial initialization") {
        Array<int, 5> arr = {10, 20};
        REQUIRE(arr[0] == 10);
        REQUIRE(arr[1] == 20);
        REQUIRE(arr[2] == 0);
        REQUIRE(arr[3] == 0);
        REQUIRE(arr[4] == 0);
    }
}

TEST_CASE("Array element access", "[Array]") {
    Array<int, 5> arr = {10, 20, 30, 40, 50};

    SECTION("operator[] access") {
        REQUIRE(arr[0] == 10);
        REQUIRE(arr[2] == 30);
        REQUIRE(arr[4] == 50);

        arr[1] = 99;
        REQUIRE(arr[1] == 99);
    }

    SECTION("At() method") {
        REQUIRE(arr.At(0) == 10);
        REQUIRE(arr.At(3) == 40);

        arr.At(2) = 77;
        REQUIRE(arr.At(2) == 77);
    }

    SECTION("First() method") {
        REQUIRE(arr.First() == 10);

        arr.First() = 5;
        REQUIRE(arr.First() == 5);
        REQUIRE(arr[0] == 5);
    }

    SECTION("Last() method") {
        REQUIRE(arr.Last() == 50);

        arr.Last() = 100;
        REQUIRE(arr.Last() == 100);
        REQUIRE(arr[4] == 100);
    }

    SECTION("Const access") {
        const Array<int, 3> constArr = {1, 2, 3};
        REQUIRE(constArr[0] == 1);
        REQUIRE(constArr.At(1) == 2);
        REQUIRE(constArr.First() == 1);
        REQUIRE(constArr.Last() == 3);
    }
}

TEST_CASE("Array ToSpan", "[Array]") {
    SECTION("Non-const ToSpan") {
        Array<int, 4> arr = {5, 10, 15, 20};
        auto span = arr.ToSpan();

        REQUIRE(span.size() == 4);
        REQUIRE(span[0] == 5);
        REQUIRE(span[3] == 20);

        span[1] = 99;
        REQUIRE(arr[1] == 99);
    }

    SECTION("Const ToSpan") {
        const Array<int, 3> arr = {1, 2, 3};
        auto span = arr.ToSpan();

        REQUIRE(span.size() == 3);
        REQUIRE(span[0] == 1);
        REQUIRE(span[2] == 3);
    }
}

TEST_CASE("Array Find operations", "[Array]") {
    Array<int, 6> arr = {10, 20, 30, 20, 40, 50};

    SECTION("Find existing element") {
        auto [index, ptr] = arr.Find(30);
        REQUIRE(index == 2);
        REQUIRE(ptr != nullptr);
        REQUIRE(*ptr == 30);
    }

    SECTION("Find first occurrence") {
        auto [index, ptr] = arr.Find(20);
        REQUIRE(index == 1);
        REQUIRE(ptr != nullptr);
        REQUIRE(*ptr == 20);
    }

    SECTION("Find non-existing element") {
        auto [index, ptr] = arr.Find(99);
        REQUIRE(index == NONE);
        REQUIRE(ptr == nullptr);
    }

    SECTION("Find on const array") {
        const Array<int, 3> constArr = {5, 10, 15};
        auto [index, ptr] = constArr.Find(10);
        REQUIRE(index == 1);
        REQUIRE(ptr != nullptr);
        REQUIRE(*ptr == 10);
    }
}

TEST_CASE("Array FindPred operations", "[Array]") {
    Array<int, 5> arr = {5, 12, 8, 20, 15};

    SECTION("FindPred with lambda") {
        auto [index, ptr] = arr.FindPred([](int x) { return x > 10; });
        REQUIRE(index == 1);
        REQUIRE(ptr != nullptr);
        REQUIRE(*ptr == 12);
    }

    SECTION("FindPred even number") {
        auto [index, ptr] = arr.FindPred([](int x) { return x % 2 == 0; });
        REQUIRE(index == 1);
        REQUIRE(*ptr == 12);
    }

    SECTION("FindPred not found") {
        auto [index, ptr] = arr.FindPred([](int x) { return x > 100; });
        REQUIRE(index == NONE);
        REQUIRE(ptr == nullptr);
    }

    SECTION("FindPred on const array") {
        const Array<int, 4> constArr = {2, 4, 6, 8};
        auto [index, ptr] = constArr.FindPred([](int x) { return x == 6; });
        REQUIRE(index == 2);
        REQUIRE(*ptr == 6);
    }
}

TEST_CASE("Array Contains", "[Array]") {
    Array<int, 5> arr = {3, 7, 11, 15, 19};

    SECTION("Contains existing element") {
        REQUIRE(arr.Contains(7) == true);
        REQUIRE(arr.Contains(11) == true);
        REQUIRE(arr.Contains(3) == true);
        REQUIRE(arr.Contains(19) == true);
    }

    SECTION("Contains non-existing element") {
        REQUIRE(arr.Contains(0) == false);
        REQUIRE(arr.Contains(100) == false);
        REQUIRE(arr.Contains(8) == false);
    }
}

TEST_CASE("Array Sort operations", "[Array]") {
    SECTION("Sort integers ascending") {
        Array<int, 6> arr = {30, 10, 50, 20, 40, 5};
        arr.Sort();

        REQUIRE(arr[0] == 5);
        REQUIRE(arr[1] == 10);
        REQUIRE(arr[2] == 20);
        REQUIRE(arr[3] == 30);
        REQUIRE(arr[4] == 40);
        REQUIRE(arr[5] == 50);
    }

    SECTION("Sort already sorted array") {
        Array<int, 4> arr = {1, 2, 3, 4};
        arr.Sort();

        REQUIRE(arr[0] == 1);
        REQUIRE(arr[1] == 2);
        REQUIRE(arr[2] == 3);
        REQUIRE(arr[3] == 4);
    }

    SECTION("Sort reverse sorted array") {
        Array<int, 5> arr = {50, 40, 30, 20, 10};
        arr.Sort();

        REQUIRE(arr[0] == 10);
        REQUIRE(arr[1] == 20);
        REQUIRE(arr[2] == 30);
        REQUIRE(arr[3] == 40);
        REQUIRE(arr[4] == 50);
    }
}

TEST_CASE("Array SortPred operations", "[Array]") {
    SECTION("Sort descending with predicate") {
        Array<int, 5> arr = {20, 50, 10, 40, 30};
        arr.SortPred([](int a, int b) { return a > b; });

        REQUIRE(arr[0] == 50);
        REQUIRE(arr[1] == 40);
        REQUIRE(arr[2] == 30);
        REQUIRE(arr[3] == 20);
        REQUIRE(arr[4] == 10);
    }

    SECTION("Sort by absolute value") {
        Array<int, 5> arr = {-30, 10, -50, 20, -5};
        arr.SortPred([](int a, int b) { return std::abs(a) < std::abs(b); });

        REQUIRE(arr[0] == -5);
        REQUIRE(arr[1] == 10);
        REQUIRE(arr[2] == 20);
        REQUIRE(arr[3] == -30);
        REQUIRE(arr[4] == -50);
    }
}

TEST_CASE("Array iterators", "[Array]") {
    Array<int, 5> arr = {1, 2, 3, 4, 5};

    SECTION("Range-based for loop") {
        i32 sum = 0;
        for (int val : arr) {
            sum += val;
        }
        REQUIRE(sum == 15);
    }

    SECTION("Iterator modification") {
        for (int& val : arr) {
            val *= 2;
        }
        REQUIRE(arr[0] == 2);
        REQUIRE(arr[1] == 4);
        REQUIRE(arr[2] == 6);
        REQUIRE(arr[3] == 8);
        REQUIRE(arr[4] == 10);
    }

    SECTION("Const iterator") {
        const Array<int, 3> constArr = {10, 20, 30};
        i32 count = 0;
        for (int val : constArr) {
            count++;
            REQUIRE(val == count * 10);
        }
        REQUIRE(count == 3);
    }

    SECTION("Manual iterator usage") {
        auto it = arr.begin();
        REQUIRE(*it == 1);

        ++it;
        REQUIRE(*it == 2);

        REQUIRE(arr.end() - arr.begin() == 5);
    }
}

TEST_CASE("Array with custom types", "[Array]") {
    struct Point {
        int x, y;
        bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    };

    SECTION("Array of structs") {
        Array<Point, 3> points = {
            {{1, 2}, {3, 4}, {5, 6}}
        };

        REQUIRE(points[0].x == 1);
        REQUIRE(points[0].y == 2);
        REQUIRE(points[2].x == 5);
        REQUIRE(points[2].y == 6);
    }

    SECTION("Find with custom type") {
        Array<Point, 3> points = {
            {{1, 2}, {3, 4}, {5, 6}}
        };
        Point target = {3, 4};

        auto [index, ptr] = points.Find(target);
        REQUIRE(index == 1);
        REQUIRE(ptr->x == 3);
        REQUIRE(ptr->y == 4);
    }

    SECTION("FindPred with custom type") {
        Array<Point, 4> points = {
            {{1, 2}, {3, 8}, {5, 6}, {7, 10}}
        };

        auto [index, ptr] = points.FindPred([](const Point& p) { return p.y > 7; });

        REQUIRE(index == 1);
        REQUIRE(ptr->y == 8);
    }
}

TEST_CASE("Array Slice operations", "[Array]") {
    SECTION("Slice middle portion") {
        Array<int, 6> arr = {10, 20, 30, 40, 50, 60};
        auto slice = arr.Slice(1, 4);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 20);
        REQUIRE(slice[1] == 30);
        REQUIRE(slice[2] == 40);
    }

    SECTION("Slice from beginning") {
        Array<int, 5> arr = {1, 2, 3, 4, 5};
        auto slice = arr.Slice(0, 3);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 1);
        REQUIRE(slice[1] == 2);
        REQUIRE(slice[2] == 3);
    }

    SECTION("Slice to end") {
        Array<int, 5> arr = {1, 2, 3, 4, 5};
        auto slice = arr.Slice(2, 5);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 3);
        REQUIRE(slice[1] == 4);
        REQUIRE(slice[2] == 5);
    }

    SECTION("Slice entire array") {
        Array<int, 4> arr = {100, 200, 300, 400};
        auto slice = arr.Slice(0, 4);

        REQUIRE(slice.size() == 4);
        REQUIRE(slice[0] == 100);
        REQUIRE(slice[1] == 200);
        REQUIRE(slice[2] == 300);
        REQUIRE(slice[3] == 400);
    }

    SECTION("Empty slice") {
        Array<int, 5> arr = {1, 2, 3, 4, 5};
        auto slice = arr.Slice(2, 2);

        REQUIRE(slice.size() == 0);
        REQUIRE(slice.empty());
    }

    SECTION("Slice with single element") {
        Array<int, 5> arr = {10, 20, 30, 40, 50};
        auto slice = arr.Slice(2, 3);

        REQUIRE(slice.size() == 1);
        REQUIRE(slice[0] == 30);
    }

    SECTION("Modify through slice") {
        Array<int, 5> arr = {1, 2, 3, 4, 5};
        auto slice = arr.Slice(1, 4);

        slice[0] = 99;
        slice[2] = 88;

        REQUIRE(arr[1] == 99);
        REQUIRE(arr[3] == 88);
    }

    SECTION("Const slice") {
        const Array<int, 5> arr = {10, 20, 30, 40, 50};
        auto slice = arr.Slice(1, 4);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 20);
        REQUIRE(slice[1] == 30);
        REQUIRE(slice[2] == 40);
    }

    SECTION("Slice with custom type") {
        struct Point {
            int x, y;
        };

        Array<Point, 4> points = {
            {{1, 2}, {3, 4}, {5, 6}, {7, 8}}
        };
        auto slice = points.Slice(1, 3);

        REQUIRE(slice.size() == 2);
        REQUIRE(slice[0].x == 3);
        REQUIRE(slice[0].y == 4);
        REQUIRE(slice[1].x == 5);
        REQUIRE(slice[1].y == 6);
    }
}

TEST_CASE("FixedVector Remove operations", "[FixedVector]") {
    SECTION("Remove single element") {
        FixedVector<int, 8> vector;
        vector.Push(1);
        vector.Push(2);
        vector.Push(2);
        vector.Push(3);
        vector.Push(2);
        vector.Push(4);
        REQUIRE(vector.Size == 6);

        // Remove first occurrence of 2
        i32 removed = vector.Remove(2);
        REQUIRE(removed == 1);
        REQUIRE(vector.Size == 5);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 2);  // Second 2 remains
        REQUIRE(vector[2] == 3);
        REQUIRE(vector[3] == 2);  // Third 2 remains
        REQUIRE(vector[4] == 4);
    }

    SECTION("Remove multiple elements") {
        FixedVector<int, 8> vector;
        vector.Push(1);
        vector.Push(2);
        vector.Push(2);
        vector.Push(3);
        vector.Push(2);
        vector.Push(4);

        // Remove two occurrences of 2
        i32 removed = vector.Remove(2, 2);
        REQUIRE(removed == 2);
        REQUIRE(vector.Size == 4);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 3);
        REQUIRE(vector[2] == 2);  // Last 2 remains
        REQUIRE(vector[3] == 4);
    }

    SECTION("RemoveAll elements") {
        FixedVector<int, 8> vector;
        vector.Push(1);
        vector.Push(2);
        vector.Push(2);
        vector.Push(3);
        vector.Push(2);
        vector.Push(4);

        // Remove all occurrences of 2
        i32 removed = vector.RemoveAll(2);
        REQUIRE(removed == 3);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 3);
        REQUIRE(vector[2] == 4);
    }

    SECTION("RemoveAll elements, which empties vector.") {
        FixedVector<int, 8> vector;
        vector.Push(2);
        vector.Push(2);
        vector.Push(2);
        vector.Push(2);
        vector.Push(2);
        vector.Push(2);
        REQUIRE(vector.Size == 6);

        // Remove all occurrences of 2
        i32 removed = vector.RemoveAll(2);
        REQUIRE(removed == 6);
        REQUIRE(vector.Size == 0);
    }

    SECTION("Remove from empty vector") {
        FixedVector<int, 4> vector;
        i32 removed = vector.Remove(1);
        REQUIRE(removed == 0);
        REQUIRE(vector.Size == 0);
    }

    SECTION("Remove non-existent element") {
        FixedVector<int, 4> vector;
        vector.Push(1);
        vector.Push(2);
        vector.Push(3);

        i32 removed = vector.Remove(4);
        REQUIRE(removed == 0);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 2);
        REQUIRE(vector[2] == 3);
    }

    SECTION("Remove with non-POD type") {
        FixedVector<std::string, 8> vector;
        vector.Push("hello");
        vector.Push("world");
        vector.Push("hello");
        vector.Push("test");
        vector.Push("hello");

        // Remove two occurrences of "hello"
        i32 removed = vector.Remove("hello", 2);
        REQUIRE(removed == 2);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0] == "world");
        REQUIRE(vector[1] == "test");
        REQUIRE(vector[2] == "hello");  // Last "hello" remains

        // Remove remaining "hello"
        removed = vector.Remove("hello");
        REQUIRE(removed == 1);
        REQUIRE(vector.Size == 2);
        REQUIRE(vector[0] == "world");
        REQUIRE(vector[1] == "test");
    }

    SECTION("Remove with move-only type") {
        struct MoveOnly {
            std::string data;
            bool moved = false;

            MoveOnly() = default;
            MoveOnly(const std::string& s) : data(s) {}
            MoveOnly(const MoveOnly&) = default;
            MoveOnly& operator=(const MoveOnly&) = delete;

            MoveOnly(MoveOnly&& other) noexcept : data(std::move(other.data)) {
                other.moved = true;
            }

            MoveOnly& operator=(MoveOnly&& other) noexcept {
                data = std::move(other.data);
                other.moved = true;
                return *this;
            }

            bool operator==(const MoveOnly& other) const { return data == other.data; }
        };

        FixedVector<MoveOnly, 8> vector;
        vector.Push(MoveOnly("one"));
        vector.Push(MoveOnly("two"));
        vector.Push(MoveOnly("two"));
        vector.Push(MoveOnly("three"));

        // Remove one occurrence of "two"
        i32 removed = vector.Remove(MoveOnly("two"));
        REQUIRE(removed == 1);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0].data == "one");
        REQUIRE(vector[1].data == "two");
        REQUIRE(vector[2].data == "three");
    }

    SECTION("RemovePred with basic predicate") {
        FixedVector<int, 8> vector;
        vector.Push(1);
        vector.Push(2);
        vector.Push(3);
        vector.Push(4);
        vector.Push(5);
        vector.Push(6);

        // Remove even numbers (up to 2)
        i32 removed = vector.RemovePred([](const int& x) { return x % 2 == 0; }, 2);
        REQUIRE(removed == 2);
        REQUIRE(vector.Size == 4);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 3);
        REQUIRE(vector[2] == 5);
        REQUIRE(vector[3] == 6);  // Last even number remains

        // Remove remaining even numbers
        removed = vector.RemovePred([](const int& x) { return x % 2 == 0; });
        REQUIRE(removed == 1);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 3);
        REQUIRE(vector[2] == 5);
    }

    SECTION("RemovePred with string predicate") {
        FixedVector<std::string, 8> vector;
        vector.Push("hello");
        vector.Push("world");
        vector.Push("test123");
        vector.Push("hello123");
        vector.Push("testing");

        // Remove strings containing "123"
        i32 removed = vector.RemovePred(
            [](const std::string& s) { return s.find("123") != std::string::npos; },
            2);
        REQUIRE(removed == 2);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0] == "hello");
        REQUIRE(vector[1] == "world");
        REQUIRE(vector[2] == "testing");
    }

    SECTION("RemovePred with empty vector") {
        FixedVector<int, 4> vector;
        i32 removed = vector.RemovePred([](const int& x) { return x > 0; });
        REQUIRE(removed == 0);
        REQUIRE(vector.Size == 0);
    }

    SECTION("RemovePred with non-matching predicate") {
        FixedVector<int, 4> vector;
        vector.Push(1);
        vector.Push(2);
        vector.Push(3);

        i32 removed = vector.RemovePred([](const int& x) { return x > 10; });
        REQUIRE(removed == 0);
        REQUIRE(vector.Size == 3);
        REQUIRE(vector[0] == 1);
        REQUIRE(vector[1] == 2);
        REQUIRE(vector[2] == 3);
    }

    SECTION("RemovePred with move-only type") {
        struct MoveOnly {
            int value = 0;
            bool moved = false;

            MoveOnly() = default;
            MoveOnly(int v) : value(v) {}
            MoveOnly(const MoveOnly&) = default;
            MoveOnly& operator=(const MoveOnly&) = delete;

            MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.moved = true; }

            MoveOnly& operator=(MoveOnly&& other) noexcept {
                value = other.value;
                other.moved = true;
                return *this;
            }
        };

        FixedVector<MoveOnly, 8> vector;
        vector.Push(MoveOnly(1));
        vector.Push(MoveOnly(2));
        vector.Push(MoveOnly(3));
        vector.Push(MoveOnly(4));

        // Remove even numbers
        i32 removed = vector.RemoveAllPred([](const MoveOnly& x) { return x.value % 2 == 0; });
        REQUIRE(removed == 2);
        REQUIRE(vector.Size == 2);
        REQUIRE(vector[0].value == 1);
        REQUIRE(vector[1].value == 3);
    }
}

TEST_CASE("FixedVector RemoveUnordered operations", "[FixedVector]") {
    SECTION("Remove existing element from middle") {
        FixedVector<int, 10> arr;
        arr.Push(1);
        arr.Push(2);
        arr.Push(3);
        arr.Push(4);

        bool result = arr.RemoveUnordered(2);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 3);

        // Element 2 should be gone, but order may have changed
        // The last element (4) should have moved to where 2 was
        bool found_2 = false;
        for (int i = 0; i < arr.Size; ++i) {
            if (arr[i] == 2) {
                found_2 = true;
                break;
            }
        }
        REQUIRE_FALSE(found_2);

        // Should still contain 1, 3, and 4
        REQUIRE(arr.Contains(1));
        REQUIRE(arr.Contains(3));
        REQUIRE(arr.Contains(4));
    }

    SECTION("Remove first element") {
        FixedVector<int, 10> arr;
        arr.Push(10);
        arr.Push(20);
        arr.Push(30);

        bool result = arr.RemoveUnordered(10);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 2);
        REQUIRE_FALSE(arr.Contains(10));
        REQUIRE(arr.Contains(20));
        REQUIRE(arr.Contains(30));
    }

    SECTION("Remove last element") {
        FixedVector<int, 10> arr;
        arr.Push(5);
        arr.Push(15);
        arr.Push(25);

        bool result = arr.RemoveUnordered(25);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 2);
        REQUIRE(arr.Contains(5));
        REQUIRE(arr.Contains(15));
        REQUIRE_FALSE(arr.Contains(25));
    }

    SECTION("Remove non-existent element") {
        FixedVector<int, 10> arr;
        arr.Push(1);
        arr.Push(2);
        arr.Push(3);

        bool result = arr.RemoveUnordered(99);

        REQUIRE(result == false);
        REQUIRE(arr.Size == 3);
        REQUIRE(arr.Contains(1));
        REQUIRE(arr.Contains(2));
        REQUIRE(arr.Contains(3));
    }

    SECTION("Remove from single element array") {
        FixedVector<int, 10> arr;
        arr.Push(42);

        bool result = arr.RemoveUnordered(42);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 0);
        REQUIRE(arr.IsEmpty());
    }

    SECTION("Remove from empty array") {
        FixedVector<int, 10> arr;

        bool result = arr.RemoveUnordered(1);

        REQUIRE(result == false);
        REQUIRE(arr.Size == 0);
        REQUIRE(arr.IsEmpty());
    }

    SECTION("Remove duplicate elements - only removes first found") {
        FixedVector<int, 10> arr;
        arr.Push(1);
        arr.Push(2);
        arr.Push(2);
        arr.Push(3);

        bool result = arr.RemoveUnordered(2);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 3);

        // Should still contain one instance of 2
        int count_2 = 0;
        for (int i = 0; i < arr.Size; ++i) {
            if (arr[i] == 2) {
                count_2++;
            }
        }
        REQUIRE(count_2 == 1);
    }
}

TEST_CASE("FixedVector::RemoveUnorderedPred", "[FixedVector]") {
    SECTION("Remove element matching predicate") {
        FixedVector<int, 10> arr;
        arr.Push(1);
        arr.Push(2);
        arr.Push(3);
        arr.Push(4);
        arr.Push(5);

        // Remove first even number
        auto isEven = [](const int& x) {
            return x % 2 == 0;
        };
        bool result = arr.RemoveUnorderedPred(isEven);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 4);

        // Should have removed either 2 or 4 (whichever was found first)
        bool has_2 = arr.Contains(2);
        bool has_4 = arr.Contains(4);
        REQUIRE((has_2 ^ has_4));  // Exactly one should be true (XOR)

        // Odd numbers should still be present
        REQUIRE(arr.Contains(1));
        REQUIRE(arr.Contains(3));
        REQUIRE(arr.Contains(5));
    }

    SECTION("Remove with predicate that matches no elements") {
        FixedVector<int, 10> arr;
        arr.Push(1);
        arr.Push(3);
        arr.Push(5);

        auto isEven = [](const int& x) {
            return x % 2 == 0;
        };
        bool result = arr.RemoveUnorderedPred(isEven);

        REQUIRE(result == false);
        REQUIRE(arr.Size == 3);
        REQUIRE(arr.Contains(1));
        REQUIRE(arr.Contains(3));
        REQUIRE(arr.Contains(5));
    }

    SECTION("Remove with predicate that matches all elements") {
        FixedVector<int, 10> arr;
        arr.Push(2);
        arr.Push(4);
        arr.Push(6);

        auto isEven = [](const int& x) {
            return x % 2 == 0;
        };
        bool result = arr.RemoveUnorderedPred(isEven);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 2);  // Only removes first match

        // Should still have two even numbers
        int evenCount = 0;
        for (int i = 0; i < arr.Size; ++i) {
            if (arr[i] % 2 == 0) {
                evenCount++;
            }
        }
        REQUIRE(evenCount == 2);
    }

    SECTION("Remove from single element array with matching predicate") {
        FixedVector<int, 10> arr;
        arr.Push(10);

        auto isPositive = [](const int& x) {
            return x > 0;
        };
        bool result = arr.RemoveUnorderedPred(isPositive);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 0);
        REQUIRE(arr.IsEmpty());
    }

    SECTION("Remove from empty array") {
        FixedVector<int, 10> arr;

        auto always_true = [](const int&) {
            return true;
        };
        bool result = arr.RemoveUnorderedPred(always_true);

        REQUIRE(result == false);
        REQUIRE(arr.Size == 0);
        REQUIRE(arr.IsEmpty());
    }

    SECTION("Complex predicate test") {
        FixedVector<std::string, 10> arr;
        arr.Push("hello");
        arr.Push("world");
        arr.Push("test");
        arr.Push("case");

        // Remove first string with length > 4
        auto long_string = [](const std::string& s) {
            return s.length() > 4;
        };
        bool result = arr.RemoveUnorderedPred(long_string);

        REQUIRE(result == true);
        REQUIRE(arr.Size == 3);

        // Either "hello" or "world" should be removed (both have length > 4)
        bool hasHello = arr.Contains("hello");
        bool hasWorld = arr.Contains("world");
        REQUIRE((hasHello ^ hasWorld));  // Exactly one should remain

        REQUIRE(arr.Contains("test"));
        REQUIRE(arr.Contains("case"));
    }
}

TEST_CASE("FixedVector::RemoveUnorderedAt", "[FixedVector]") {
    SECTION("Remove element at valid index") {
        FixedVector<int, 10> arr;
        arr.Push(10);
        arr.Push(20);
        arr.Push(30);
        arr.Push(40);

        arr.RemoveUnorderedAt(1);  // Remove element at index 1 (value 20)

        REQUIRE(arr.Size == 3);

        // Element at index 1 should no longer be 20
        // The last element (40) should have moved to index 1
        REQUIRE_FALSE(arr.Contains(20));
        REQUIRE(arr.Contains(10));
        REQUIRE(arr.Contains(30));
        REQUIRE(arr.Contains(40));
    }

    SECTION("Remove first element") {
        FixedVector<int, 10> arr;
        arr.Push(100);
        arr.Push(200);
        arr.Push(300);

        arr.RemoveUnorderedAt(0);

        REQUIRE(arr.Size == 2);
        REQUIRE_FALSE(arr.Contains(100));
        REQUIRE(arr.Contains(200));
        REQUIRE(arr.Contains(300));
    }

    SECTION("Remove last element") {
        FixedVector<int, 10> arr;
        arr.Push(1);
        arr.Push(2);
        arr.Push(3);

        arr.RemoveUnorderedAt(2);  // Last index

        REQUIRE(arr.Size == 2);
        REQUIRE(arr.Contains(1));
        REQUIRE(arr.Contains(2));
        REQUIRE_FALSE(arr.Contains(3));
    }

    SECTION("Remove from single element array") {
        FixedVector<int, 10> arr;
        arr.Push(999);

        arr.RemoveUnorderedAt(0);

        REQUIRE(arr.Size == 0);
        REQUIRE(arr.IsEmpty());
    }

    SECTION("Verify unordered nature - element order after removal") {
        FixedVector<char, 10> arr;
        arr.Push('A');
        arr.Push('B');
        arr.Push('C');
        arr.Push('D');
        arr.Push('E');

        // Remove element at index 1 ('B')
        arr.RemoveUnorderedAt(1);

        REQUIRE(arr.Size == 4);

        // The last element 'E' should have moved to index 1
        REQUIRE(arr[0] == 'A');
        REQUIRE(arr[1] == 'E');  // Last element moved here
        REQUIRE(arr[2] == 'C');
        REQUIRE(arr[3] == 'D');

        REQUIRE_FALSE(arr.Contains('B'));
    }
}

TEST_CASE("DynArray const operations", "[dynarray]") {
    SECTION("Const array access") {
        Arena arena = Arena::Allocate("TestArena"sv, 64 * KILOBYTE);
        auto array = NewDynArray<int>(&arena);

        array.Push(10);
        array.Push(20);

        const DynArray<int>& constArray = array;

        REQUIRE(constArray[0] == 10);
        REQUIRE(constArray[1] == 20);

        // Cannot modify through const reference
        // constArray.Push(30);  // Should not compile
    }
}

TEST_CASE("FixedVector Slice operations", "[FixedVector]") {
    SECTION("Slice middle portion") {
        FixedVector<int, 10> vec;
        vec.Push(10);
        vec.Push(20);
        vec.Push(30);
        vec.Push(40);
        vec.Push(50);
        vec.Push(60);

        auto slice = vec.Slice(1, 4);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 20);
        REQUIRE(slice[1] == 30);
        REQUIRE(slice[2] == 40);
    }

    SECTION("Slice from beginning") {
        FixedVector<int, 10> vec;
        vec.Push(1);
        vec.Push(2);
        vec.Push(3);
        vec.Push(4);
        vec.Push(5);

        auto slice = vec.Slice(0, 3);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 1);
        REQUIRE(slice[1] == 2);
        REQUIRE(slice[2] == 3);
    }

    SECTION("Slice to end") {
        FixedVector<int, 10> vec;
        vec.Push(1);
        vec.Push(2);
        vec.Push(3);
        vec.Push(4);
        vec.Push(5);

        auto slice = vec.Slice(2, 5);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 3);
        REQUIRE(slice[1] == 4);
        REQUIRE(slice[2] == 5);
    }

    SECTION("Slice entire vector") {
        FixedVector<int, 10> vec;
        vec.Push(100);
        vec.Push(200);
        vec.Push(300);
        vec.Push(400);

        auto slice = vec.Slice(0, 4);

        REQUIRE(slice.size() == 4);
        REQUIRE(slice[0] == 100);
        REQUIRE(slice[1] == 200);
        REQUIRE(slice[2] == 300);
        REQUIRE(slice[3] == 400);
    }

    SECTION("Empty slice") {
        FixedVector<int, 10> vec;
        vec.Push(1);
        vec.Push(2);
        vec.Push(3);

        auto slice = vec.Slice(2, 2);

        REQUIRE(slice.size() == 0);
        REQUIRE(slice.empty());
    }

    SECTION("Slice with single element") {
        FixedVector<int, 10> vec;
        vec.Push(10);
        vec.Push(20);
        vec.Push(30);

        auto slice = vec.Slice(1, 2);

        REQUIRE(slice.size() == 1);
        REQUIRE(slice[0] == 20);
    }

    SECTION("Modify through slice") {
        FixedVector<int, 10> vec;
        vec.Push(1);
        vec.Push(2);
        vec.Push(3);
        vec.Push(4);
        vec.Push(5);

        auto slice = vec.Slice(1, 4);

        slice[0] = 99;
        slice[2] = 88;

        REQUIRE(vec[1] == 99);
        REQUIRE(vec[3] == 88);
    }

    SECTION("Const slice") {
        FixedVector<int, 10> vec;
        vec.Push(10);
        vec.Push(20);
        vec.Push(30);
        vec.Push(40);
        vec.Push(50);

        const FixedVector<int, 10>& constVec = vec;
        auto slice = constVec.Slice(1, 4);

        REQUIRE(slice.size() == 3);
        REQUIRE(slice[0] == 20);
        REQUIRE(slice[1] == 30);
        REQUIRE(slice[2] == 40);
    }

    SECTION("Slice with non-POD type") {
        FixedVector<std::string, 10> vec;
        vec.Push("hello");
        vec.Push("world");
        vec.Push("test");
        vec.Push("slice");

        auto slice = vec.Slice(1, 3);

        REQUIRE(slice.size() == 2);
        REQUIRE(slice[0] == "world");
        REQUIRE(slice[1] == "test");
    }

    SECTION("Slice of empty vector") {
        FixedVector<int, 10> vec;

        auto slice = vec.Slice(0, 0);

        REQUIRE(slice.size() == 0);
        REQUIRE(slice.empty());
    }

    SECTION("Slice after modifications") {
        FixedVector<int, 10> vec;
        vec.Push(1);
        vec.Push(2);
        vec.Push(3);
        vec.Push(4);

        vec.RemoveUnorderedAt(1);  // Remove element at index 1

        // Now vec might be [1, 4, 3] depending on implementation
        REQUIRE(vec.Size == 3);

        auto slice = vec.Slice(0, vec.Size);
        REQUIRE(slice.size() == 3);
    }
}

TEST_CASE("Queue basic operations", "[Queue]") {
    SECTION("Default initialization") {
        Queue<int, 5> queue;
        REQUIRE(queue.IsEmpty());
        REQUIRE(queue.Size == 0);
        REQUIRE(queue.Capacity() == 5);
        REQUIRE_FALSE(queue.IsFull());
    }

    SECTION("Push single element") {
        Queue<int, 5> queue;
        queue.Push(42);

        REQUIRE(queue.Size == 1);
        REQUIRE_FALSE(queue.IsEmpty());
        REQUIRE(queue.Front() == 42);
        REQUIRE(queue.Back() == 42);
    }

    SECTION("Push multiple elements") {
        Queue<int, 5> queue;
        queue.Push(10);
        queue.Push(20);
        queue.Push(30);

        REQUIRE(queue.Size == 3);
        REQUIRE(queue.Front() == 10);
        REQUIRE(queue.Back() == 30);
    }

    SECTION("Pop single element") {
        Queue<int, 5> queue;
        queue.Push(100);

        int val = queue.Pop();
        REQUIRE(val == 100);
        REQUIRE(queue.Size == 0);
        REQUIRE(queue.IsEmpty());
    }

    SECTION("Pop maintains FIFO order") {
        Queue<int, 5> queue;
        queue.Push(1);
        queue.Push(2);
        queue.Push(3);

        REQUIRE(queue.Pop() == 1);
        REQUIRE(queue.Pop() == 2);
        REQUIRE(queue.Pop() == 3);
        REQUIRE(queue.IsEmpty());
    }

    SECTION("Pop from empty queue") {
        Queue<int, 5> queue;
        int val = queue.Pop();
        REQUIRE(val == 0);  // Default-constructed int
        REQUIRE(queue.IsEmpty());
    }
}

TEST_CASE("Queue circular buffer behavior", "[Queue]") {
    SECTION("Fill to capacity") {
        Queue<int, 4> queue;
        queue.Push(1);
        queue.Push(2);
        queue.Push(3);
        queue.Push(4);

        REQUIRE(queue.IsFull());
        REQUIRE(queue.Size == 4);
        REQUIRE(queue.Front() == 1);
        REQUIRE(queue.Back() == 4);
    }

    SECTION("Wrap around behavior") {
        Queue<int, 4> queue;

        // Fill the queue
        queue.Push(1);
        queue.Push(2);
        queue.Push(3);
        queue.Push(4);
        REQUIRE(queue.IsFull());

        // Pop two elements
        REQUIRE(queue.Pop() == 1);
        REQUIRE(queue.Pop() == 2);
        REQUIRE(queue.Size == 2);

        // Push two more (should wrap around)
        queue.Push(5);
        queue.Push(6);
        REQUIRE(queue.IsFull());
        REQUIRE(queue.Size == 4);

        // Verify FIFO order is maintained
        REQUIRE(queue.Pop() == 3);
        REQUIRE(queue.Pop() == 4);
        REQUIRE(queue.Pop() == 5);
        REQUIRE(queue.Pop() == 6);
        REQUIRE(queue.IsEmpty());
    }

    SECTION("Multiple wrap cycles") {
        Queue<int, 3> queue;

        // First cycle
        queue.Push(1);
        queue.Push(2);
        queue.Push(3);
        REQUIRE(queue.Pop() == 1);

        // Second cycle
        queue.Push(4);
        REQUIRE(queue.Pop() == 2);
        queue.Push(5);
        REQUIRE(queue.Pop() == 3);

        // Third cycle
        queue.Push(6);
        REQUIRE(queue.Pop() == 4);
        REQUIRE(queue.Pop() == 5);
        REQUIRE(queue.Pop() == 6);
        REQUIRE(queue.IsEmpty());
    }
}

TEST_CASE("Queue Front and Back access", "[Queue]") {
    SECTION("Front returns first element") {
        Queue<int, 5> queue;
        queue.Push(10);
        queue.Push(20);
        queue.Push(30);

        REQUIRE(queue.Front() == 10);

        queue.Pop();
        REQUIRE(queue.Front() == 20);
    }

    SECTION("Back returns last element") {
        Queue<int, 5> queue;
        queue.Push(10);
        queue.Push(20);
        queue.Push(30);

        REQUIRE(queue.Back() == 30);

        queue.Push(40);
        REQUIRE(queue.Back() == 40);
    }

    SECTION("Front and Back same for single element") {
        Queue<int, 5> queue;
        queue.Push(99);

        REQUIRE(queue.Front() == 99);
        REQUIRE(queue.Back() == 99);
    }

    SECTION("Modify through Front reference") {
        Queue<int, 5> queue;
        queue.Push(1);
        queue.Push(2);

        queue.Front() = 100;
        REQUIRE(queue.Front() == 100);
        REQUIRE(queue.Pop() == 100);
    }

    SECTION("Modify through Back reference") {
        Queue<int, 5> queue;
        queue.Push(1);
        queue.Push(2);

        queue.Back() = 200;
        REQUIRE(queue.Back() == 200);
        queue.Pop();  // Remove 1
        REQUIRE(queue.Pop() == 200);
    }

    SECTION("Const Front and Back access") {
        Queue<int, 5> queue;
        queue.Push(10);
        queue.Push(20);

        const Queue<int, 5>& constQueue = queue;
        REQUIRE(constQueue.Front() == 10);
        REQUIRE(constQueue.Back() == 20);
    }
}

TEST_CASE("Queue Clear operation", "[Queue]") {
    SECTION("Clear non-empty queue") {
        Queue<int, 5> queue;
        queue.Push(1);
        queue.Push(2);
        queue.Push(3);

        queue.Clear();
        REQUIRE(queue.IsEmpty());
        REQUIRE(queue.Size == 0);
    }

    SECTION("Clear resets indices") {
        Queue<int, 4> queue;
        queue.Push(1);
        queue.Push(2);
        queue.Pop();
        queue.Push(3);

        queue.Clear();

        // Should be able to use the queue normally after clear
        queue.Push(10);
        queue.Push(20);
        REQUIRE(queue.Front() == 10);
        REQUIRE(queue.Back() == 20);
    }

    SECTION("Clear empty queue") {
        Queue<int, 5> queue;
        queue.Clear();
        REQUIRE(queue.IsEmpty());
    }
}

TEST_CASE("Queue with non-trivial types", "[Queue]") {
    SECTION("Queue of strings") {
        Queue<std::string, 5> queue;

        queue.Push("Hello");
        queue.Push("World");
        queue.Push("Queue");

        REQUIRE(queue.Size == 3);
        REQUIRE(queue.Front() == "Hello");
        REQUIRE(queue.Back() == "Queue");

        std::string popped = queue.Pop();
        REQUIRE(popped == "Hello");
        REQUIRE(queue.Front() == "World");
    }

    SECTION("Queue with wrap around for strings") {
        Queue<std::string, 3> queue;

        queue.Push("A");
        queue.Push("B");
        queue.Push("C");

        REQUIRE(queue.Pop() == "A");

        queue.Push("D");
        REQUIRE(queue.Front() == "B");
        REQUIRE(queue.Back() == "D");

        REQUIRE(queue.Pop() == "B");
        REQUIRE(queue.Pop() == "C");
        REQUIRE(queue.Pop() == "D");
        REQUIRE(queue.IsEmpty());
    }

    SECTION("Clear with non-trivial type") {
        Queue<std::string, 5> queue;
        queue.Push("test1");
        queue.Push("test2");
        queue.Push("test3");

        queue.Clear();
        REQUIRE(queue.IsEmpty());

        // Should work normally after clear
        queue.Push("new");
        REQUIRE(queue.Front() == "new");
    }
}

TEST_CASE("Queue with custom types", "[Queue]") {
    struct Point {
        int x, y;
        Point() : x(0), y(0) {}
        Point(int x_, int y_) : x(x_), y(y_) {}
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y;
        }
    };

    SECTION("Queue of custom structs") {
        Queue<Point, 4> queue;

        queue.Push(Point(1, 2));
        queue.Push(Point(3, 4));
        queue.Push(Point(5, 6));

        REQUIRE(queue.Size == 3);
        REQUIRE(queue.Front().x == 1);
        REQUIRE(queue.Front().y == 2);
        REQUIRE(queue.Back().x == 5);
        REQUIRE(queue.Back().y == 6);

        Point p = queue.Pop();
        REQUIRE(p.x == 1);
        REQUIRE(p.y == 2);
    }

    SECTION("Queue with wrap around for custom type") {
        Queue<Point, 3> queue;

        queue.Push(Point(10, 20));
        queue.Push(Point(30, 40));
        queue.Push(Point(50, 60));

        queue.Pop();
        queue.Push(Point(70, 80));

        REQUIRE(queue.Front().x == 30);
        REQUIRE(queue.Back().x == 70);
    }
}

TEST_CASE("Queue capacity limits", "[Queue]") {
    SECTION("Cannot push to full queue without assertion") {
        Queue<int, 3> queue;
        queue.Push(1);
        queue.Push(2);
        queue.Push(3);

        REQUIRE(queue.IsFull());
        // Pushing to a full queue would trigger ASSERT in debug builds
    }

    SECTION("Check IsFull predicate") {
        Queue<int, 2> queue;
        REQUIRE_FALSE(queue.IsFull());

        queue.Push(1);
        REQUIRE_FALSE(queue.IsFull());

        queue.Push(2);
        REQUIRE(queue.IsFull());

        queue.Pop();
        REQUIRE_FALSE(queue.IsFull());
    }

    SECTION("Capacity remains constant") {
        Queue<int, 10> queue;
        REQUIRE(queue.Capacity() == 10);

        queue.Push(1);
        queue.Push(2);
        REQUIRE(queue.Capacity() == 10);

        queue.Pop();
        REQUIRE(queue.Capacity() == 10);
    }
}

TEST_CASE("Queue stress test", "[Queue]") {
    SECTION("Alternating push and pop") {
        Queue<int, 5> queue;

        for (int i = 0; i < 20; i++) {
            queue.Push(i);
            REQUIRE(queue.Front() == i);
            REQUIRE(queue.Pop() == i);
            REQUIRE(queue.IsEmpty());
        }
    }

    SECTION("Fill, empty, refill pattern") {
        Queue<int, 4> queue;

        // First fill
        for (int i = 0; i < 4; i++) {
            queue.Push(i);
        }
        REQUIRE(queue.IsFull());

        // Empty
        for (int i = 0; i < 4; i++) {
            REQUIRE(queue.Pop() == i);
        }
        REQUIRE(queue.IsEmpty());

        // Refill
        for (int i = 10; i < 14; i++) {
            queue.Push(i);
        }
        REQUIRE(queue.IsFull());

        // Verify
        for (int i = 10; i < 14; i++) {
            REQUIRE(queue.Pop() == i);
        }
        REQUIRE(queue.IsEmpty());
    }

    SECTION("Multiple partial fills and drains") {
        Queue<int, 6> queue;
        int value = 0;

        for (int cycle = 0; cycle < 5; cycle++) {
            // Push 3 elements
            for (int i = 0; i < 3; i++) {
                queue.Push(value++);
            }

            // Pop 2 elements
            queue.Pop();
            queue.Pop();
        }

        // Should still have correct elements
        REQUIRE_FALSE(queue.IsEmpty());
        REQUIRE(queue.Size > 0);
    }
}
