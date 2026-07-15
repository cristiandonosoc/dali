#include <dali/core/memory.h>

#include <dali/core/defines.h>
#include <dali/core/string.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;

TEST_CASE("Arena - FixedSize", "[memory][arena]") {
    SECTION("Simple allocation") {
        constexpr u64 kAllocSize = 4 * KILOBYTE;
        Arena arena = Arena::Allocate("TestArena"sv, kAllocSize);
        DEFER { Arena::Free(&arena); };
        REQUIRE(arena.Start != nullptr);
        REQUIRE(arena.Size == kAllocSize);
        REQUIRE(arena.Offset == 0);

        constexpr u64 kPushSize = 16;
        {
            u8* ptr = arena.Push(kPushSize).data();
            REQUIRE(arena.Offset == kPushSize);
            for (u32 i = 0; i < kPushSize; i++) {
                *ptr++ = (u8)i;
            }
        }

        arena.Reset();
        REQUIRE(arena.Size == kAllocSize);
        REQUIRE(arena.Offset == 0);

        {
            u8* ptr = arena.PushZero(kPushSize).data();
            for (u32 i = 0; i < kPushSize; i++) {
                REQUIRE(*ptr++ == 0);
            }
        }
    }
}

TEST_CASE("Align", "[memory][Align]") {
    SECTION("Already aligned pointers remain unchanged") {
        void* ptr = (void*)0x1000;  // Aligned to 4096
        REQUIRE(Align(ptr, 16) == ptr);
        REQUIRE(Align(ptr, 32) == ptr);
        REQUIRE(Align(ptr, 4096) == ptr);
    }

    SECTION("Align to different power-of-2 boundaries") {
        void* ptr = (void*)0x1234;
        REQUIRE((u64)Align(ptr, 16) == 0x1230);
        REQUIRE((u64)Align(ptr, 32) == 0x1220);
        REQUIRE((u64)Align(ptr, 64) == 0x1200);
    }

    SECTION("Align null pointer") {
        void* ptr = nullptr;
        REQUIRE(Align(ptr, 16) == ptr);
        REQUIRE(Align(ptr, 32) == ptr);
        REQUIRE(Align(ptr, 4096) == ptr);
    }

    SECTION("Align with different starting addresses") {
        void* ptr1 = (void*)0x1235;
        void* ptr2 = (void*)0x1239;

        // Both should align to 0x1230 with 16-byte alignment
        REQUIRE(Align(ptr1, 16) == Align(ptr2, 16));
        REQUIRE((u64)Align(ptr1, 16) == 0x1230);
    }

    SECTION("Maximum alignment values") {
        void* ptr = (void*)0x1234567890ABCDEF;

        // Test with large power-of-2 alignments
        REQUIRE((u64)Align(ptr, 0x1000) == 0x1234567890ABC000);
        REQUIRE((u64)Align(ptr, 0x10000) == 0x1234567890AB0000);
    }

    SECTION("Address near zero") {
        void* ptr = (void*)15;  // 0xF
        REQUIRE((u64)Align(ptr, 16) == 0x0);
        REQUIRE((u64)Align(ptr, 32) == 0x0);
    }

    SECTION("Address near type limit") {
        void* ptr = (void*)(~0ULL - 10);       // Near maximum value
        u64 expected = (~0ULL - 10) & ~15ULL;  // Manual calculation for 16-byte alignment
        REQUIRE((u64)Align(ptr, 16) == expected);
    }
}

TEST_CASE("AlignForward", "[memory][AlignForward]") {
    SECTION("Already aligned pointers remain unchanged") {
        void* ptr = (void*)0x1000;  // Aligned to 4096
        REQUIRE(AlignForward(ptr, 16) == ptr);
        REQUIRE(AlignForward(ptr, 32) == ptr);
        REQUIRE(AlignForward(ptr, 4096) == ptr);
    }

    SECTION("Align to next power-of-2 boundary") {
        void* ptr = (void*)0x1234;                      // 4660 in decimal
        REQUIRE((u64)AlignForward(ptr, 16) == 0x1240);  // Next 16-byte boundary
        REQUIRE((u64)AlignForward(ptr, 32) == 0x1240);  // Next 32-byte boundary
        REQUIRE((u64)AlignForward(ptr, 64) == 0x1240);  // Next 64-byte boundary
    }

    SECTION("Align null pointer") {
        void* ptr = nullptr;
        REQUIRE(AlignForward(ptr, 16) == ptr);
        REQUIRE(AlignForward(ptr, 32) == ptr);
        REQUIRE(AlignForward(ptr, 4096) == ptr);
    }

    SECTION("Address one below alignment boundary") {
        void* ptr = (void*)0x123F;  // One below 0x1240 (16-byte boundary)
        REQUIRE((u64)AlignForward(ptr, 16) == 0x1240);
    }

    SECTION("Address one above alignment boundary") {
        void* ptr = (void*)0x1241;  // One above 0x1240 (16-byte boundary)
        REQUIRE((u64)AlignForward(ptr, 16) == 0x1250);
    }

    SECTION("Maximum alignment values") {
        void* ptr = (void*)0x1234567890ABCDEF;

        // Test with large power-of-2 alignments
        REQUIRE((u64)AlignForward(ptr, 0x1000) == 0x1234567890ABD000);
        REQUIRE((u64)AlignForward(ptr, 0x10000) == 0x1234567890AC0000);
    }

    SECTION("Address near zero") {
        void* ptr = (void*)1;
        REQUIRE((u64)AlignForward(ptr, 16) == 0x10);
        REQUIRE((u64)AlignForward(ptr, 32) == 0x20);
    }

    SECTION("Address near type limit") {
        // Test case near maximum value, but with enough space to align forward
        void* ptr = (void*)(~0ULL - 20);
        u64 aligned = (u64)AlignForward(ptr, 16);
        REQUIRE(aligned > (u64)ptr);
        REQUIRE((aligned & 15) == 0);  // Verify alignment
    }
}

TEST_CASE("Compare Align and AlignForward behaviors", "[Memory][Align][AlignForward]") {
    SECTION("Compare Align and AlignForward") {
        void* ptr = (void*)0x1234;
        REQUIRE((u64)Align(ptr, 16) == 0x1230);         // Previous 16-byte boundary
        REQUIRE((u64)AlignForward(ptr, 16) == 0x1240);  // Next 16-byte boundary
    }

    SECTION("Alignment differences for various addresses") {
        // Test several addresses with different alignments
        for (u64 addr = 0x1230; addr < 0x1240; addr++) {
            void* ptr = (void*)addr;
            u64 backward = (u64)Align(ptr, 16);
            u64 forward = (u64)AlignForward(ptr, 16);

            // Forward alignment should be >= original address
            REQUIRE(forward >= addr);
            // Backward alignment should be <= original address
            REQUIRE(backward <= addr);

            // Both alignments should be 16-byte aligned
            REQUIRE((forward & 15) == 0);
            REQUIRE((backward & 15) == 0);

            // Forward alignment should be the next 16-byte boundary after backward
            if (addr & 15) {  // If address is not already aligned
                REQUIRE(forward == backward + 16);
            } else {  // If address is already aligned
                REQUIRE(forward == backward);
            }
        }
    }
}

TEST_CASE("IsPowerOf2", "[memory]") {
    SECTION("Valid power of 2 values") {
        REQUIRE(IsPowerOf2(1));
        REQUIRE(IsPowerOf2(2));
        REQUIRE(IsPowerOf2(4));
        REQUIRE(IsPowerOf2(8));
        REQUIRE(IsPowerOf2(16));
        REQUIRE(IsPowerOf2(32));
        REQUIRE(IsPowerOf2(64));
        REQUIRE(IsPowerOf2(128));
        REQUIRE(IsPowerOf2(256));
        REQUIRE(IsPowerOf2(512));
        REQUIRE(IsPowerOf2(1024));
        REQUIRE(IsPowerOf2(0x8000000000000000));
    }

    SECTION("Non-power of 2 values") {
        REQUIRE_FALSE(IsPowerOf2(3));
        REQUIRE_FALSE(IsPowerOf2(6));
        REQUIRE_FALSE(IsPowerOf2(7));
        REQUIRE_FALSE(IsPowerOf2(9));
        REQUIRE_FALSE(IsPowerOf2(10));
        REQUIRE_FALSE(IsPowerOf2(15));
        REQUIRE_FALSE(IsPowerOf2(0xFFFFFFFFFFFFFFFF));
    }
}

namespace memory_test_private {

StringView SomeFile(Arena* arena, int number) { return Printf(arena, "foo_%d", number); }

}  // namespace memory_test_private

using TestBlockArena = BlockArena<16 * BYTE, 16>;

TEST_CASE("BlockArena - Initialization", "[memory][blockarena]") {
    SECTION("Initialize empty arena") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        REQUIRE(block_arena->NextFreeBlock == 0);
        REQUIRE(block_arena->Stats.TotalAllocCalls == 0);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 0);

        // Verify free list is properly linked
        for (u32 i = 0; i < 16 - 1; i++) {
            REQUIRE(block_arena->BlocksFreeList[i] == i + 1);
        }
        REQUIRE(block_arena->BlocksFreeList[15] == std::numeric_limits<i32>::min());
    }
}

TEST_CASE("BlockArena - Basic allocation", "[memory][blockarena]") {
    SECTION("Allocate single block") {
        Arena arena = Arena::Allocate("TestArena"sv, 1 * MEGABYTE);
        DEFER { Arena::Free(&arena); };

        auto* block_arena = arena.Push<BlockArena<1 * KILOBYTE, 8>>();
        block_arena->Init("TestBlockArena"sv);

        auto [span, _] = block_arena->AllocateBlock();

        REQUIRE(span.size_bytes() == 1 * KILOBYTE);
        REQUIRE(block_arena->NextFreeBlock == 1);
        REQUIRE(block_arena->Stats.TotalAllocCalls == 1);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);
    }

    SECTION("Allocate multiple blocks") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        BlockAllocationResult result;
        result = block_arena->AllocateBlock();
        result = block_arena->AllocateBlock();
        result = block_arena->AllocateBlock();

        REQUIRE(block_arena->NextFreeBlock == 3);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 3);
        REQUIRE(block_arena->Stats.TotalAllocCalls == 3);
    }

    SECTION("Write to allocated blocks") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span1, metadata1] = block_arena->AllocateBlock();
        auto [span2, metadata2] = block_arena->AllocateBlock();

        // Write pattern to first block
        for (size_t i = 0; i < span1.size(); i++) {
            span1[i] = (u8)(i & 0xFF);
        }

        // Write different pattern to second block
        for (size_t i = 0; i < span2.size(); i++) {
            span2[i] = (u8)((i + 128) & 0xFF);
        }

        // Verify patterns
        for (size_t i = 0; i < span1.size(); i++) {
            REQUIRE(span1[i] == (u8)(i & 0xFF));
        }
        for (size_t i = 0; i < span2.size(); i++) {
            REQUIRE(span2[i] == (u8)((i + 128) & 0xFF));
        }
    }
}

TEST_CASE("BlockArena - Free blocks", "[memory][blockarena]") {
    SECTION("Allocate and free single block") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span, _] = block_arena->AllocateBlock();
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);

        bool freed = block_arena->FreeBlock(span);
        REQUIRE(freed);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 0);
        REQUIRE(block_arena->NextFreeBlock == 0);
    }

    SECTION("Allocate, free, and reallocate") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span1, metadata1] = block_arena->AllocateBlock();
        i32 index1 = block_arena->GetBlockIndex(span1);
        REQUIRE(index1 != NONE);

        block_arena->FreeBlock(span1);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 0);

        // Next allocation should reuse the freed block
        auto [span2, metadata2] = block_arena->AllocateBlock();
        i32 index2 = block_arena->GetBlockIndex(span2);
        REQUIRE(index2 != NONE);
        REQUIRE(index2 == index1);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);
        REQUIRE(block_arena->Stats.TotalAllocCalls == 2);
    }

    SECTION("Free blocks in different order") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span1, metadata1] = block_arena->AllocateBlock();
        auto [span2, metadata2] = block_arena->AllocateBlock();
        auto [span3, metadata3] = block_arena->AllocateBlock();

        REQUIRE(block_arena->Stats.AllocatedBlocks == 3);

        // Free middle block
        block_arena->FreeBlock(span2);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 2);

        // Free last block
        block_arena->FreeBlock(span3);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);

        // Free first block
        block_arena->FreeBlock(span1);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 0);
    }
}

TEST_CASE("BlockArena - Exhaustion", "[memory][blockarena]") {
    SECTION("Allocate all blocks") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        void* ptrs[block_arena->kBlockCount];

        // Allocate all blocks
        for (u32 i = 0; i < block_arena->kBlockCount; i++) {
            auto [span, _] = block_arena->AllocateBlock();
            REQUIRE(!span.empty());
            ptrs[i] = span.data();
        }

        REQUIRE(block_arena->Stats.AllocatedBlocks == block_arena->kBlockCount);
        REQUIRE(block_arena->NextFreeBlock == std::numeric_limits<i32>::min());

        // Try to allocate one more - should fail
        auto [span_fail, metadata_fail] = block_arena->AllocateBlock();
        REQUIRE(span_fail.empty());
        REQUIRE(metadata_fail == nullptr);
        REQUIRE(block_arena->Stats.AllocatedBlocks == block_arena->kBlockCount);

        // Free one block
        block_arena->FreeBlock(ptrs[3]);
        REQUIRE(block_arena->Stats.AllocatedBlocks == block_arena->kBlockCount - 1);
        REQUIRE(block_arena->NextFreeBlock == 3);

        // Now we should be able to allocate again
        auto [span_new, _] = block_arena->AllocateBlock();
        REQUIRE(!span_new.empty());
        REQUIRE(span_new.data() == ptrs[3]);
        REQUIRE(block_arena->NextFreeBlock == std::numeric_limits<i32>::min());
    }
}

TEST_CASE("BlockArena - Stats tracking", "[memory][blockarena]") {
    SECTION("Track allocation statistics") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        REQUIRE(block_arena->Stats.TotalAllocCalls == 0);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 0);

        auto [span1, metadata1] = block_arena->AllocateBlock();
        REQUIRE(!span1.empty());
        REQUIRE(block_arena->Stats.TotalAllocCalls == 1);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);

        auto [span2, metadata2] = block_arena->AllocateBlock();
        REQUIRE(!span2.empty());
        REQUIRE(block_arena->Stats.TotalAllocCalls == 2);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 2);

        block_arena->FreeBlock(span1);
        REQUIRE(block_arena->Stats.TotalAllocCalls == 2);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);

        auto [span3, metadata3] = block_arena->AllocateBlock();
        REQUIRE(!span3.empty());
        REQUIRE(block_arena->Stats.TotalAllocCalls == 3);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 2);
    }
}

TEST_CASE("Scratch arena", "[memory]") {
    SECTION("Can get scratch arena") {
        using namespace memory_test_private;

        {
            ScopedArena scratch = Arena::GetScratch();
            REQUIRE(scratch.Arena->Offset == 0);

            StringView msg1 = SomeFile(scratch.Arena, 33);
            REQUIRE(strcmp(msg1.Str(), "foo_33") == 0);
            REQUIRE(scratch.Arena->Offset == 2048);

            StringView msg2 = SomeFile(scratch.Arena, 88);
            REQUIRE(strcmp(msg2.Str(), "foo_88") == 0);
            REQUIRE(scratch.Arena->Offset == 4096);
        }

        auto scratch = Arena::GetScratch();
        REQUIRE(scratch.Arena->Offset == 0);
    }

    SECTION("Nested scratch arenas share and stack") {
        auto scratch_arenas = Arena::ReferenceScratch();

        // Plain (conflict-less) nested calls all share the first arena; each scope only rolls back
        // what it pushed, so depth is unbounded by the pool size.
        {
            auto scratch1 = Arena::GetScratch();
            auto scratch2 = Arena::GetScratch();
            ScopedArena scratch3 = Arena::GetScratch();
            REQUIRE(scratch1.Arena == &scratch_arenas[0]);
            REQUIRE(scratch2.Arena == scratch1.Arena);
            REQUIRE(scratch3.Arena == scratch1.Arena);
        }

        // Inner scopes restore only their own pushes; the outer data survives them.
        {
            auto scratch1 = Arena::GetScratch();
            auto data = scratch1.Arena->Push(64);
            REQUIRE(data.size() == 64);
            u64 outer_offset = scratch1.Arena->Offset;
            {
                auto scratch2 = Arena::GetScratch();
                auto inner = scratch2.Arena->Push(64);
                REQUIRE(inner.size() == 64);
                REQUIRE(scratch2.Arena->Offset > outer_offset);
            }
            REQUIRE(scratch1.Arena->Offset == outer_offset);
        }
        REQUIRE(scratch_arenas[0].Offset == 0);
    }

    SECTION("Conflicts are never returned") {
        auto scratch_arenas = Arena::ReferenceScratch();

        {
            auto scratch = Arena::GetScratch(&scratch_arenas[0]);
            REQUIRE(scratch.Arena == &scratch_arenas[1]);
        }
        {
            auto scratch = Arena::GetScratch(&scratch_arenas[0], &scratch_arenas[1]);
            REQUIRE(scratch.Arena == &scratch_arenas[2]);
        }
        // A conflict deeper in the pool doesn't shift the pick off an earlier free arena.
        {
            auto scratch = Arena::GetScratch(&scratch_arenas[1]);
            REQUIRE(scratch.Arena == &scratch_arenas[0]);
        }
        // A non-scratch arena as conflict is harmless (it can never be picked anyway).
        {
            Arena other = Arena::Allocate("Other"sv, 4 * KILOBYTE);
            auto scratch = Arena::GetScratch(&other);
            REQUIRE(scratch.Arena == &scratch_arenas[0]);
            Arena::Free(&other);
        }
    }

    SECTION("Multiple functions") {
        auto scratch_arenas = Arena::ReferenceScratch();
        auto verify_arenas = [&](u64 offset0, u64 offset1, u64 offset2, u64 offset3) {
            REQUIRE(scratch_arenas[0].Offset == offset0);
            REQUIRE(scratch_arenas[1].Offset == offset1);
            REQUIRE(scratch_arenas[2].Offset == offset2);
            REQUIRE(scratch_arenas[3].Offset == offset3);
        };

        verify_arenas(0, 0, 0, 0);

        // Every plain nested call shares arena 0; offsets stack on the way down and are restored
        // (LIFO) as each scope exits.
        auto fn1 = [&]() {
            auto scratch = Arena::GetScratch();
            REQUIRE(scratch.Arena == &scratch_arenas[0]);

            auto result = scratch.Arena->Push(1024);
            REQUIRE(result.size() == 1024);
            verify_arenas(4096, 0, 0, 0);
        };

        auto fn2 = [&]() {
            auto scratch = Arena::GetScratch();
            REQUIRE(scratch.Arena == &scratch_arenas[0]);

            auto result = scratch.Arena->Push(1024);
            REQUIRE(result.size() == 1024);
            verify_arenas(3072, 0, 0, 0);
            fn1();
            verify_arenas(3072, 0, 0, 0);
        };

        auto fn3 = [&]() {
            auto scratch = Arena::GetScratch();
            REQUIRE(scratch.Arena == &scratch_arenas[0]);

            auto result = scratch.Arena->Push(1024);
            REQUIRE(result.size() == 1024);
            verify_arenas(2048, 0, 0, 0);
            fn2();
            verify_arenas(2048, 0, 0, 0);
        };

        {
            auto scratch = Arena::GetScratch();
            REQUIRE(scratch.Arena == &scratch_arenas[0]);
            auto result = scratch.Arena->Push(1024);
            REQUIRE(result.size() == 1024);
            verify_arenas(1024, 0, 0, 0);
            fn3();
            verify_arenas(1024, 0, 0, 0);
        }
        verify_arenas(0, 0, 0, 0);
    }
}

TEST_CASE("BlockArena - Pointer-based API", "[memory][blockarena]") {
    SECTION("Get handle from pointer") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span1, metadata1] = block_arena->AllocateBlock();
        auto [span2, metadata2] = block_arena->AllocateBlock();
        auto [span3, metadata3] = block_arena->AllocateBlock();

        // Test getting handle from the start of each block
        i32 retrieved_index1 = block_arena->GetBlockIndex(span1);
        i32 retrieved_index2 = block_arena->GetBlockIndex(span2);
        i32 retrieved_index3 = block_arena->GetBlockIndex(span3);

        REQUIRE(retrieved_index1 != NONE);
        REQUIRE(retrieved_index2 != NONE);
        REQUIRE(retrieved_index3 != NONE);
        REQUIRE(block_arena->GetBlockByIndex(retrieved_index1).data() == span1.data());
        REQUIRE(block_arena->GetBlockByIndex(retrieved_index2).data() == span2.data());
        REQUIRE(block_arena->GetBlockByIndex(retrieved_index3).data() == span3.data());
    }

    SECTION("ContainsPointer check") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span, _] = block_arena->AllocateBlock();

        // Should contain pointers within the allocated block
        REQUIRE(block_arena->GetBlockIndex(span.data()) != NONE);
        REQUIRE(block_arena->GetBlockIndex(span.data() + 8) != NONE);
        REQUIRE(block_arena->GetBlockIndex(span.data() + span.size() - 1) != NONE);

        // Should not contain null pointer
        REQUIRE(block_arena->GetBlockIndex(nullptr) == NONE);

        // Should not contain external pointers
        u8 external_memory[16];
        REQUIRE(block_arena->GetBlockIndex(external_memory) == NONE);
    }

    SECTION("Free block by pointer") {
        Arena arena = Arena::Allocate("TestArena"sv, 16 * KILOBYTE);
        DEFER { Arena::Free(&arena); };

        TestBlockArena* block_arena = arena.Push<TestBlockArena>();
        block_arena->Init("TestBlockArena"sv);

        auto [span1, _1] = block_arena->AllocateBlock();
        auto [span2, _2] = block_arena->AllocateBlock();
        auto [span3, _3] = block_arena->AllocateBlock();

        REQUIRE(block_arena->Stats.AllocatedBlocks == 3);

        // Free middle block using pointer
        bool freed = block_arena->FreeBlock(span2.data());
        REQUIRE(freed);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 2);

        // Free from middle of first block
        freed = block_arena->FreeBlock(span1.data() + 8);
        REQUIRE(freed);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 1);

        // Free last block from end pointer
        freed = block_arena->FreeBlock(span3.data() + span3.size() - 1);
        REQUIRE(freed);
        REQUIRE(block_arena->Stats.AllocatedBlocks == 0);
    }
}

TEST_CASE("BlockArenaManager - Pointer-based API", "[memory][blockarena][manager]") {
    SECTION("Get handle from pointer with multiple arena sizes") {
        BlockArenaManager bam;
        Init(&bam);
        DEFER { Shutdown(&bam); };

        // Allocate blocks of different sizes
        auto [span_1kb, _1] = AllocateBlock(&bam, 512);    // Should use 1KB arena
        auto [span_4kb, _2] = AllocateBlock(&bam, 2048);   // Should use 4KB arena
        auto [span_16kb, _3] = AllocateBlock(&bam, 8192);  // Should use 16KB arena

        REQUIRE(!span_1kb.empty());
        REQUIRE(span_1kb.size() == 1024);
        REQUIRE(!span_4kb.empty());
        REQUIRE(span_4kb.size() == 4096);
        REQUIRE(!span_16kb.empty());
        REQUIRE(span_16kb.size() == 16384);
    }

    SECTION("Free block by pointer in manager") {
        BlockArenaManager bam;
        Init(&bam);
        DEFER { Shutdown(&bam); };

        auto [span1, _1] = AllocateBlock(&bam, 512);
        auto [span2, _2] = AllocateBlock(&bam, 512);
        auto [span3, _3] = AllocateBlock(&bam, 2048);

        REQUIRE(!span1.empty());
        REQUIRE(!span2.empty());
        REQUIRE(!span3.empty());

        bool freed = false;

        // Free using exact pointer
        freed = FreeBlock(&bam, span1.data());
        REQUIRE(freed);

        // Free larger block
        freed = FreeBlock(&bam, span3.data());
        REQUIRE(freed);

        // Try to free invalid pointer
        freed = FreeBlock(&bam, nullptr);
        REQUIRE_FALSE(freed);

        u8 external_memory[16];
        freed = FreeBlock(&bam, external_memory);
        REQUIRE_FALSE(freed);
    }

    SECTION("Get metadata by pointer") {
        BlockArenaManager bam;
        Init(&bam);
        DEFER { Shutdown(&bam); };

        auto [span, metadata_original] = AllocateBlock(&bam, 512);
        REQUIRE(!span.empty());
        REQUIRE(metadata_original != nullptr);

        // Get metadata using pointer
        BlockMetadata* metadata_from_ptr = GetBlockMetadata(&bam, span.data());
        REQUIRE(metadata_from_ptr != nullptr);
        REQUIRE(metadata_from_ptr == metadata_original);

        // Get metadata from middle of block
        metadata_from_ptr = GetBlockMetadata(&bam, span.data() + 256);
        REQUIRE(metadata_from_ptr == metadata_original);

        // Get metadata from end of block
        metadata_from_ptr = GetBlockMetadata(&bam, span.data() + span.size() - 1);
        REQUIRE(metadata_from_ptr == metadata_original);

        // Invalid pointers should return null
        metadata_from_ptr = GetBlockMetadata(&bam, nullptr);
        REQUIRE(metadata_from_ptr == nullptr);

        u8 external_memory[16];
        metadata_from_ptr = GetBlockMetadata(&bam, external_memory);
        REQUIRE(metadata_from_ptr == nullptr);
    }

    SECTION("Pointer-based workflow without storing handles") {
        BlockArenaManager bam;
        Init(&bam);
        DEFER { Shutdown(&bam); };

        // Allocate and only keep the memory pointers
        auto [span1, __1] = AllocateBlock(&bam, 512);
        auto [span2, __2] = AllocateBlock(&bam, 1024);
        auto [span3, __3] = AllocateBlock(&bam, 4096);

        u8* ptr1 = span1.data();
        u8* ptr2 = span2.data();
        u8* ptr3 = span3.data();

        // Write some data
        ptr1[0] = 42;
        ptr2[100] = 84;
        ptr3[1000] = 126;

        // Verify data
        REQUIRE(ptr1[0] == 42);
        REQUIRE(ptr2[100] == 84);
        REQUIRE(ptr3[1000] == 126);

        // Free using only pointers (no handles needed!)
        bool freed = FreeBlock(&bam, ptr2);
        REQUIRE(freed);

        // Clean up remaining blocks using pointers
        FreeBlock(&bam, ptr1);
        FreeBlock(&bam, ptr3);
    }
}
