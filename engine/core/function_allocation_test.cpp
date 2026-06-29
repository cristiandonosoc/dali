#include <engine/core/function.h>

#include <iostream>
#include <new>

// 1. Define a thread-local tracking flag
thread_local int gAllocationCount = 0;

// 2. Override the global allocation operator
void* operator new(std::size_t size) {
    gAllocationCount++;

    // Fallback to standard malloc or system allocator
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](std::size_t size) {
    gAllocationCount++;
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }

int main() {
    int alloc_count = gAllocationCount;

    u64 a = 1;
    u64 b = 2;
    u64 c = 3;
    u64 d = 4;
    kdk::Function<u64(void)> function = [a, b, c, d]() {
        return a + b + c + d;
    };

    u64 result = function();
    if (result != 10) {
        std::cout << "WRONG RESULT. GOT: " << result << ", EXPECTED: 10" << std::endl;
        return 1;
    }

    if (int new_alloc_count = gAllocationCount; new_alloc_count != alloc_count) {
        std::cout << "FUNCTION ALLOCATED. GOT: " << new_alloc_count << ", EXPECTED: " << alloc_count
                  << std::endl;
        return 1;
    }
}
