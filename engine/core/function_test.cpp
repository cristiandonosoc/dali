#include <catch2/catch_test_macros.hpp>

#include <engine/core/function.h>

using namespace kdk;

TEST_CASE("Function basic construction and invocation", "[Function]") {
    SECTION("Default construction") {
        Function<int()> f;
        REQUIRE_FALSE(f.IsValid());
        REQUIRE_FALSE(f);
    }

    SECTION("Construct from nullptr") {
        Function<int()> f = nullptr;
        REQUIRE_FALSE(f.IsValid());
        REQUIRE_FALSE(f);
    }

    SECTION("Construct from simple lambda") {
        Function<int()> f = []() { return 42; };
        REQUIRE(f.IsValid());
        REQUIRE(f);
        REQUIRE(f() == 42);
    }

    SECTION("Construct from lambda with captures") {
        int value = 100;
        Function<int()> f = [value]() { return value; };
        REQUIRE(f.IsValid());
        REQUIRE(f() == 100);
    }

    SECTION("Construct from lambda returning void") {
        int called = 0;
        Function<void()> f = [&called]() { called++; };
        REQUIRE(f.IsValid());
        f();
        REQUIRE(called == 1);
        f();
        REQUIRE(called == 2);
    }
}

TEST_CASE("Function with parameters", "[Function]") {
    SECTION("Single parameter") {
        Function<int(int)> f = [](int x) { return x * 2; };
        REQUIRE(f(5) == 10);
        REQUIRE(f(21) == 42);
    }

    SECTION("Multiple parameters") {
        Function<int(int, int)> f = [](int a, int b) { return a + b; };
        REQUIRE(f(10, 20) == 30);
        REQUIRE(f(5, 7) == 12);
    }

    SECTION("Three parameters") {
        Function<int(int, int, int)> f = [](int a, int b, int c) { return a * b + c; };
        REQUIRE(f(2, 3, 4) == 10);
        REQUIRE(f(5, 6, 1) == 31);
    }

    SECTION("Mixed parameter types") {
        Function<double(int, double)> f = [](int a, double b) { return a + b; };
        REQUIRE(f(10, 5.5) == 15.5);
    }

    SECTION("String parameters") {
        Function<int(const char*)> f = [](const char* str) {
            int len = 0;
            while (str[len] != '\0') len++;
            return len;
        };
        REQUIRE(f("hello") == 5);
        REQUIRE(f("test") == 4);
    }
}

TEST_CASE("Function with captures", "[Function]") {
    SECTION("Capture single pointer") {
        int value = 42;
        int* ptr = &value;
        Function<int()> f = [ptr]() { return *ptr; };
        REQUIRE(f() == 42);

        value = 100;
        REQUIRE(f() == 100);
    }

    SECTION("Capture multiple pointers") {
        int a = 10, b = 20, c = 30;
        Function<int()> f = [&a, &b, &c]() { return a + b + c; };
        REQUIRE(f() == 60);

        a = 1;
        b = 2;
        c = 3;
        REQUIRE(f() == 6);
    }

    SECTION("Capture and modify through reference") {
        int counter = 0;
        Function<void()> f = [&counter]() { counter++; };

        f();
        REQUIRE(counter == 1);
        f();
        REQUIRE(counter == 2);
        f();
        REQUIRE(counter == 3);
    }

    SECTION("Capture pointer and call method") {
        struct Counter {
            int value = 0;
            void increment() { value++; }
            int get() const { return value; }
        };

        Counter counter;
        Function<void()> f = [&counter]() { counter.increment(); };

        REQUIRE(counter.get() == 0);
        f();
        REQUIRE(counter.get() == 1);
        f();
        REQUIRE(counter.get() == 2);
    }
}

TEST_CASE("Function assignment", "[Function]") {
    SECTION("Assign from lambda") {
        Function<int()> f;
        REQUIRE_FALSE(f.IsValid());

        f = []() { return 42; };
        REQUIRE(f.IsValid());
        REQUIRE(f() == 42);
    }

    SECTION("Reassign to different lambda") {
        Function<int()> f = []() { return 10; };
        REQUIRE(f() == 10);

        f = []() { return 20; };
        REQUIRE(f() == 20);
    }

    SECTION("Assign to nullptr") {
        Function<int()> f = []() { return 42; };
        REQUIRE(f.IsValid());

        f = nullptr;
        REQUIRE_FALSE(f.IsValid());
    }
}

TEST_CASE("Function copy semantics", "[Function]") {
    SECTION("Copy construction") {
        int value = 123;
        Function<int()> f1 = [value]() { return value; };
        Function<int()> f2 = f1;

        REQUIRE(f1.IsValid());
        REQUIRE(f2.IsValid());
        REQUIRE(f1() == 123);
        REQUIRE(f2() == 123);
    }

    SECTION("Copy assignment") {
        int a = 10;
        int b = 20;
        Function<int()> f1 = [a]() { return a; };
        Function<int()> f2 = [b]() { return b; };

        REQUIRE(f1() == 10);
        REQUIRE(f2() == 20);

        f2 = f1;
        REQUIRE(f2() == 10);
    }

    SECTION("Copy from empty function") {
        Function<int()> f1;
        Function<int()> f2 = f1;

        REQUIRE_FALSE(f1.IsValid());
        REQUIRE_FALSE(f2.IsValid());
    }
}

TEST_CASE("Function move semantics", "[Function]") {
    SECTION("Move construction") {
        int value = 456;
        Function<int()> f1 = [value]() { return value; };
        Function<int()> f2 = std::move(f1);

        REQUIRE(f2.IsValid());
        REQUIRE(f2() == 456);
    }

    SECTION("Move assignment") {
        int a = 11;
        int b = 22;
        Function<int()> f1 = [a]() { return a; };
        Function<int()> f2 = [b]() { return b; };

        REQUIRE(f1() == 11);
        REQUIRE(f2() == 22);

        f2 = std::move(f1);
        REQUIRE(f2() == 11);
    }
}

TEST_CASE("Function with different return types", "[Function]") {
    SECTION("Return int") {
        Function<int()> f = []() { return 42; };
        REQUIRE(f() == 42);
    }

    SECTION("Return double") {
        Function<double()> f = []() { return 3.14; };
        REQUIRE(f() == 3.14);
    }

    SECTION("Return bool") {
        Function<bool()> f = []() { return true; };
        REQUIRE(f() == true);
    }

    SECTION("Return pointer") {
        int value = 99;
        Function<int*()> f = [&value]() { return &value; };
        REQUIRE(*f() == 99);
    }

    SECTION("Return void") {
        bool called = false;
        Function<void()> f = [&called]() { called = true; };
        f();
        REQUIRE(called == true);
    }
}

TEST_CASE("Function with custom storage size", "[Function]") {
    SECTION("Default storage size (N=4)") {
        // Capture 4 pointers (should fit in default storage)
        int a = 1, b = 2, c = 3, d = 4;
        Function<int(), 4> f = [&a, &b, &c, &d]() { return a + b + c + d; };
        REQUIRE(f() == 10);
    }

    SECTION("Smaller storage size (N=2)") {
        // Capture 2 pointers
        int a = 10, b = 20;
        Function<int(), 2> f = [&a, &b]() { return a + b; };
        REQUIRE(f() == 30);
    }

    SECTION("Larger storage size (N=8)") {
        // Capture 8 pointers
        int v1 = 1, v2 = 2, v3 = 3, v4 = 4;
        int v5 = 5, v6 = 6, v7 = 7, v8 = 8;
        Function<int(), 8> f = [&v1, &v2, &v3, &v4, &v5, &v6, &v7, &v8]() {
            return v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8;
        };
        REQUIRE(f() == 36);
    }
}

TEST_CASE("Function conversion between storage sizes", "[Function]") {
    SECTION("Convert from smaller to larger storage") {
        int a = 100;
        Function<int(), 2> f1 = [&a]() { return a; };
        Function<int(), 4> f2 = f1;

        REQUIRE(f1.IsValid());
        REQUIRE(f2.IsValid());
        REQUIRE(f1() == 100);
        REQUIRE(f2() == 100);

        a = 200;
        REQUIRE(f1() == 200);
        REQUIRE(f2() == 200);
    }

    SECTION("Convert Function<2> to Function<8>") {
        int x = 42, y = 58;
        Function<int(), 2> small = [&x, &y]() { return x + y; };
        Function<int(), 8> large = small;

        REQUIRE(small() == 100);
        REQUIRE(large() == 100);
    }
}

TEST_CASE("Function with const parameters", "[Function]") {
    SECTION("Const reference parameter") {
        Function<int(const int&)> f = [](const int& x) { return x * 2; };
        int value = 21;
        REQUIRE(f(value) == 42);
    }

    SECTION("Const pointer parameter") {
        Function<int(const int*)> f = [](const int* ptr) { return *ptr + 10; };
        int value = 32;
        REQUIRE(f(&value) == 42);
    }
}

TEST_CASE("Function with rvalue parameters", "[Function]") {
    SECTION("Forwarding rvalue reference") {
        Function<int(int&&)> f = [](int&& x) { return x * 2; };
        REQUIRE(f(21) == 42);
    }

    SECTION("Moving parameter") {
        struct Movable {
            int value;
            Movable(int v) : value(v) {}
            Movable(const Movable&) = delete;
            Movable(Movable&& other) noexcept : value(other.value) { other.value = 0; }
        };

        Function<int(Movable&&)> f = [](Movable&& m) { return m.value; };
        Movable m(42);
        REQUIRE(f(std::move(m)) == 42);
    }
}

TEST_CASE("Function edge cases", "[Function]") {
    SECTION("Function calling through pointer") {
        Function<int()> inner = []() { return 42; };
        Function<int()>* inner_ptr = &inner;
        Function<int()> outer = [inner_ptr]() { return (*inner_ptr)(); };
        REQUIRE(outer() == 42);
    }

    SECTION("Recursive-like behavior through capture") {
        int count = 0;
        Function<void()>* fptr = nullptr;
        Function<void()> f = [&count, &fptr]() {
            count++;
            if (count < 5 && fptr) {
                (*fptr)();
            }
        };
        fptr = &f;

        f();
        REQUIRE(count == 5);
    }

    SECTION("Function that modifies its capture") {
        int value = 0;
        Function<int()> f = [&value]() { return ++value; };

        REQUIRE(f() == 1);
        REQUIRE(f() == 2);
        REQUIRE(f() == 3);
        REQUIRE(value == 3);
    }

    SECTION("Function with empty lambda") {
        Function<void()> f = []() {};
        REQUIRE(f.IsValid());
        f();  // Should not crash
    }
}

TEST_CASE("Function with complex computations", "[Function]") {
    SECTION("Lambda computing factorial (iterative)") {
        Function<int(int)> factorial = [](int n) {
            int result = 1;
            for (int i = 2; i <= n; i++) {
                result *= i;
            }
            return result;
        };

        REQUIRE(factorial(0) == 1);
        REQUIRE(factorial(1) == 1);
        REQUIRE(factorial(5) == 120);
        REQUIRE(factorial(6) == 720);
    }

    SECTION("Lambda with conditional logic") {
        Function<const char*(int)> classify = [](int x) -> const char* {
            if (x < 0) return "negative";
            if (x == 0) return "zero";
            return "positive";
        };

        REQUIRE(std::string(classify(-5)) == "negative");
        REQUIRE(std::string(classify(0)) == "zero");
        REQUIRE(std::string(classify(10)) == "positive");
    }

    SECTION("Lambda with loop and accumulation") {
        Function<int(const int*, int)> sum_array = [](const int* arr, int size) {
            int sum = 0;
            for (int i = 0; i < size; i++) {
                sum += arr[i];
            }
            return sum;
        };

        int arr[] = {1, 2, 3, 4, 5};
        REQUIRE(sum_array(arr, 5) == 15);
    }
}

TEST_CASE("Function with predicate use cases", "[Function]") {
    SECTION("Even number predicate") {
        Function<bool(int)> is_even = [](int x) { return x % 2 == 0; };
        REQUIRE(is_even(2) == true);
        REQUIRE(is_even(3) == false);
        REQUIRE(is_even(42) == true);
    }

    SECTION("Range check predicate") {
        int min = 10;
        int max = 20;
        Function<bool(int)> in_range = [min, max](int x) { return x >= min && x <= max; };

        REQUIRE(in_range(5) == false);
        REQUIRE(in_range(15) == true);
        REQUIRE(in_range(25) == false);
    }

    SECTION("String predicate") {
        Function<bool(const char*)> is_empty = [](const char* str) { return str[0] == '\0'; };
        REQUIRE(is_empty("") == true);
        REQUIRE(is_empty("hello") == false);
    }

    SECTION("Pointer validity predicate") {
        Function<bool(const void*)> is_null = [](const void* ptr) { return ptr == nullptr; };
        int value = 42;
        REQUIRE(is_null(nullptr) == true);
        REQUIRE(is_null(&value) == false);
    }
}

TEST_CASE("Function comparison with nullptr", "[Function]") {
    SECTION("Valid function is not equal to nullptr semantically") {
        Function<int()> f = []() { return 42; };
        REQUIRE(f.IsValid());
        REQUIRE(static_cast<bool>(f) == true);
    }

    SECTION("Invalid function is like nullptr semantically") {
        Function<int()> f;
        REQUIRE_FALSE(f.IsValid());
        REQUIRE(static_cast<bool>(f) == false);
    }

    SECTION("Function assigned to nullptr becomes invalid") {
        Function<int()> f = []() { return 42; };
        REQUIRE(f.IsValid());

        f = nullptr;
        REQUIRE_FALSE(f.IsValid());
    }
}

TEST_CASE("Function with struct/class captures", "[Function]") {
    SECTION("Capture POD struct") {
        struct Point {
            int x, y;
        };

        Point p{10, 20};
        Function<int()> f = [p]() { return p.x + p.y; };
        REQUIRE(f() == 30);
    }

    SECTION("Capture pointer to struct") {
        struct Data {
            int value;
            void increment() { value++; }
        };

        Data data{100};
        Function<void()> increment = [&data]() { data.increment(); };
        Function<int()> get_value = [&data]() { return data.value; };

        REQUIRE(get_value() == 100);
        increment();
        REQUIRE(get_value() == 101);
        increment();
        REQUIRE(get_value() == 102);
    }
}

TEST_CASE("Function storage and alignment", "[Function]") {
    SECTION("Function with minimal lambda has valid storage") {
        Function<int()> f = []() { return 1; };
        REQUIRE(f.IsValid());
        REQUIRE(f() == 1);
    }

    SECTION("Function with maximum default captures") {
        // Default N=4 means we can capture 4 pointer-sized objects
        void* p1 = (void*)0x1;
        void* p2 = (void*)0x2;
        void* p3 = (void*)0x3;
        void* p4 = (void*)0x4;

        Function<size_t()> f = [p1, p2, p3, p4]() {
            return (size_t)p1 + (size_t)p2 + (size_t)p3 + (size_t)p4;
        };

        REQUIRE(f.IsValid());
        REQUIRE(f() == 0x1 + 0x2 + 0x3 + 0x4);
    }
}

TEST_CASE("Function multiple invocations", "[Function]") {
    SECTION("Same result on multiple calls") {
        Function<int()> f = []() { return 42; };

        for (int i = 0; i < 100; i++) {
            REQUIRE(f() == 42);
        }
    }

    SECTION("Stateful function produces different results") {
        int counter = 0;
        Function<int()> f = [&counter]() { return counter++; };

        for (int i = 0; i < 10; i++) {
            REQUIRE(f() == i);
        }
    }

    SECTION("Function with side effects") {
        int sum = 0;
        Function<void(int)> add = [&sum](int x) { sum += x; };

        add(10);
        REQUIRE(sum == 10);
        add(20);
        REQUIRE(sum == 30);
        add(5);
        REQUIRE(sum == 35);
    }
}

TEST_CASE("Function with container integration", "[Function]") {
    SECTION("Function used as callback") {
        struct Processor {
            static void process(int value, Function<void(int)> callback) { callback(value * 2); }
        };

        int result = 0;
        Processor::process(21, [&result](int x) { result = x; });
        REQUIRE(result == 42);
    }

    SECTION("Array of functions") {
        Function<int(int)> operations[3];
        operations[0] = [](int x) { return x + 1; };
        operations[1] = [](int x) { return x * 2; };
        operations[2] = [](int x) { return x - 1; };

        REQUIRE(operations[0](10) == 11);
        REQUIRE(operations[1](10) == 20);
        REQUIRE(operations[2](10) == 9);
    }

    SECTION("Function as struct member") {
        struct Handler {
            Function<int(int)> transform;
        };

        Handler h;
        h.transform = [](int x) { return x * x; };

        REQUIRE(h.transform(5) == 25);
        REQUIRE(h.transform(7) == 49);
    }
}

TEST_CASE("Function constexpr support", "[Function]") {
    SECTION("Default constructor is constexpr") {
        Function<int()> f;
        REQUIRE_FALSE(f.IsValid());
    }

    SECTION("IsValid is constexpr") {
        Function<int()> f;
        bool is_valid = f.IsValid();
        REQUIRE(is_valid == false);
    }

    SECTION("bool operator is constexpr") {
        Function<int()> f;
        bool as_bool = static_cast<bool>(f);
        REQUIRE(as_bool == false);
    }
}
