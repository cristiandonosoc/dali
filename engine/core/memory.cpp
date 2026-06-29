#include <engine/core/memory.h>

#include <engine/core/container.h>

#include <cstdlib>
#include <cstring>

namespace kdk {

namespace memory_private {

static constexpr u32 kScratchArenaSize = 32 * MEGABYTE;

Array<StringView, 8> kScratchArenaNames = {
    "ScratchArena0"sv,
    "ScratchArena1"sv,
    "ScratchArena2"sv,
    "ScratchArena3"sv,
    "ScratchArena4"sv,
    "ScratchArena5"sv,
    "ScratchArena6"sv,
    "ScratchArena7"sv,
};

void* AllocMemory(Arena* arena, u64 size) {
    arena->Stats.AllocCalls++;
    return malloc(size);
}

void FreeMemory(Arena* arena, void* ptr) {
    arena->Stats.FreeCalls++;
    free(ptr);
}

Arena AllocExtendableArena(u64 size) {
    Arena arena{
        .Size = size,
        .Offset = 0,
        .Type = EArenaType::Extendable,
        .ExtendableData = {},
    };
    arena.Start = (u8*)AllocMemory(&arena, size);

    // We "embed" the next link at the end of the buffer.
    u64 max_offset = (u64)Align((void*)(size - sizeof(Arena)), 8);
    arena.ExtendableData.NextArena = nullptr;
    arena.ExtendableData.MaxLinkOffset = max_offset;

    return arena;
}

// Return free calls.
u32 FreeExtendableArena(Arena* arena) {
    ASSERT(IsValid(*arena));

    u32 free_calls = 0;
    if (arena->ExtendableData.NextArena) {
        free_calls += FreeExtendableArena(arena->ExtendableData.NextArena);
    }

    FreeMemory(arena, arena->Start);
    free_calls++;
    *arena = {};

    return free_calls;
}

u8* ExtendableArenaPush(Arena* arena, u64 size, u64 alignment) {
    ASSERT(size <= arena->ExtendableData.MaxLinkOffset);

    // Determine the new offset
    u8* ptr = arena->Start + arena->Offset;
    ptr = (u8*)AlignForward(ptr, alignment);
    u64 offset = ptr - arena->Start;

#if ENABLE_FOR_DEBUGGING
    printf("Offset: %llu, Size: %llu, Offset + Size: %llu, MaxLinkOffset: %llu\n",
           offset,
           size,
           offset + size,
           arena->ExtendableData.MaxLinkOffset);
#endif

    // Check if this link is full.
    if (offset + size > arena->ExtendableData.MaxLinkOffset) {
        // If we're full, check if we have a link already. If not, we create one.
        if (!arena->ExtendableData.NextArena) {
            // We allocate it *exactly* where |MaxLinkOffset| is, since we created an aligned
            // space for it.
            Arena* new_arena = (Arena*)(arena->Start + arena->ExtendableData.MaxLinkOffset);
            *new_arena = AllocExtendableArena(arena->Size);
            arena->ExtendableData.NextArena = new_arena;
        }

        return ExtendableArenaPush(arena->ExtendableData.NextArena, size, alignment);
    }

    // Otherwise we alloc as normal.
    arena->Offset = offset + size;
    return ptr;
}

void ConsolidateNumbers(Arena* arena) {
    switch (arena->Type) {
        case EArenaType::FixedSize: return;
        case EArenaType::Extendable: {
            Arena* next_arena = arena->ExtendableData.NextArena;

            arena->ExtendableData.TotalSize = arena->Size;
            while (next_arena) {
                arena->ExtendableData.TotalSize += next_arena->Size;
                next_arena->ExtendableData.TotalSize = 0;
                arena->Stats.AllocCalls += next_arena->Stats.AllocCalls;
                next_arena->Stats.AllocCalls = 0;
                arena->Stats.FreeCalls += next_arena->Stats.AllocCalls;
                next_arena->Stats.FreeCalls = 0;

                next_arena = next_arena->ExtendableData.NextArena;
            }

            break;
        }
    }
}

}  // namespace memory_private

StringView ToString(EArenaType type) {
    switch (type) {
        case EArenaType::FixedSize: return "FixedSize"sv;
        case EArenaType::Extendable: return "Extendable"sv;
    }

    ASSERT(false);
    return "<unknown>"sv;
}

bool IsValid(const Arena& arena) {
    if (!arena.Start) {
        return false;
    }

    if (arena.Size == 0) {
        return false;
    }

    if (arena.Offset >= arena.Size) {
        return false;
    }

    return true;
}

Arena AllocateArena(StringView name, u64 size, EArenaType type) {
    Arena out = {};
    switch (type) {
        case EArenaType::FixedSize: {
            Arena arena{
                .Size = size,
                .Offset = 0,
                .Type = type,
                .FixedData = {},
            };
            arena.Start = (u8*)memory_private::AllocMemory(&arena, size);
            out = std::move(arena);

            break;
        }
        case EArenaType::Extendable: {
            out = memory_private::AllocExtendableArena(size);
            break;
        }
    }

    memory_private::ConsolidateNumbers(&out);
    out.Name = name;
    return out;
}

void FreeArena(Arena* arena) {
    ASSERT(IsValid(*arena));

    switch (arena->Type) {
        case EArenaType::FixedSize: {
            memory_private::FreeMemory(arena, arena->Start);
            break;
        }
        case EArenaType::Extendable: {
            memory_private::FreeExtendableArena(arena);
            break;
        }
    }
}

Arena CarveArena(StringView name, Arena* arena, u64 size) {
    auto scratch = GetScratchArena(arena);
    StringView out_name = Printf(scratch, "%s:%s", arena->Name.Str(), name.Str());

    Arena out = {
        .Start = ArenaPush(arena, size).data(),
        .Size = size,
        .Type = EArenaType::FixedSize,
        .Name = out_name,
    };

    memory_private::ConsolidateNumbers(&out);
    return out;
}

void ArenaReset(Arena* arena) {
    switch (arena->Type) {
        case EArenaType::FixedSize: {
            arena->Offset = 0;
            break;
        }
        case EArenaType::Extendable: {
            if (Arena* next_arena = arena->ExtendableData.NextArena) {
                arena->Stats.FreeCalls += memory_private::FreeExtendableArena(next_arena);
                arena->ExtendableData.NextArena = nullptr;
            }
            arena->Offset = 0;
            break;
        }
    }

    memory_private::ConsolidateNumbers(arena);
}

std::span<u8> ArenaPush(Arena* arena, u64 size, u64 alignment) {
    ASSERT(IsValid(*arena));

    u8* out = nullptr;

    switch (arena->Type) {
        case EArenaType::FixedSize: {
            // Determine the new offset
            u8* ptr = arena->Start + arena->Offset;
            ptr = (u8*)AlignForward(ptr, alignment);
            u64 offset = ptr - arena->Start;

            ASSERT(offset + size < arena->Size);

            arena->Offset = offset + size;
            out = ptr;
            break;
        }
        case EArenaType::Extendable: {
            out = memory_private::ExtendableArenaPush(arena, size, alignment);
            break;
        }
    }

    memory_private::ConsolidateNumbers(arena);
    return {out, size};
}

std::span<u8> ArenaPushZero(Arena* arena, u64 size, u64 alignment) {
    auto data = ArenaPush(arena, size, alignment);
    std::memset(data.data(), 0, size);
    return data;
}

std::span<Arena> ReferenceScratchArenas() {
    using namespace memory_private;
    constexpr i32 kScratchArenaCount = 4;
    static_assert(kScratchArenaCount <= (i32)kScratchArenaNames.Size,
                  "Not enough scratch arena names");
    static bool gInitialized = false;
    static Array<Arena, kScratchArenaCount> gArenas = {};
    if (!gInitialized) [[unlikely]] {
        for (i32 i = 0; i < kScratchArenaCount; i++) {
            Arena& arena = gArenas[i];
            StringView name = kScratchArenaNames[i];
            arena = AllocateArena(name, kScratchArenaSize);
        }

        gInitialized = true;
    }

    return gArenas;
}

ScopedArena::ScopedArena(struct Arena* arena, u64 original_offset)
    : Arena(arena), OriginalOffset(original_offset) {}

ScopedArena::~ScopedArena() {
    ASSERTF(Arena, "No weird shenanigans with arenas!");
    Arena->Offset = OriginalOffset;
}

ScopedArena GetScratchArena(Arena* conflict1, Arena* conflict2) {
    Arena* conflicts[2] = {
        conflict1,
        conflict2,
    };

    auto scratch_arenas = ReferenceScratchArenas();

    // Search for a valid scratch arena.
    Arena* scratch_arena = nullptr;
    for (Arena& a : scratch_arenas) {
        bool conflict_detected = false;
        for (Arena* conflict : conflicts) {
            if (conflict == &a) {
                conflict_detected = true;
                break;
            }
        }

        if (!conflict_detected) {
            scratch_arena = &a;
            break;
        }
    }

    ASSERTF(scratch_arena, "No scratch arena could be found");
    return ScopedArena(scratch_arena, scratch_arena->Offset);
}

// BLOCK ARENA MANAGER -----------------------------------------------------------------------------

namespace memory_private {}  // namespace memory_private

void Init(BlockArenaManager* bam) {
    ResetStruct(bam);

#define X(SIZE_NAME, BLOCK_SIZE, BLOCK_COUNT, ...)                              \
    {                                                                           \
        u32 size = sizeof(BlockArena<BLOCK_SIZE, BLOCK_COUNT>);                 \
        auto* block_arena = (BlockArena<BLOCK_SIZE, BLOCK_COUNT>*)malloc(size); \
        block_arena->Init(StringView("BlockArena_" #SIZE_NAME));                \
        bam->_BlockArena_##SIZE_NAME = block_arena;                             \
    }
    BLOCK_ARENA_TYPES(X)
#undef X
}

void Shutdown(BlockArenaManager* bam){
#define X(SIZE_NAME, ...)                         \
    {                                             \
        bam->_BlockArena_##SIZE_NAME->Shutdown(); \
        free(bam->_BlockArena_##SIZE_NAME);       \
        bam->_BlockArena_##SIZE_NAME = nullptr;   \
    }
    BLOCK_ARENA_TYPES(X)
#undef X
}

BlockAllocationResult
    AllocateBlock(BlockArenaManager* bam, u32 byte_size, std::source_location source_location) {
    // Go over all the sizes and see if one fits.
#define X(SIZE_NAME, BLOCK_SIZE, ...)                                        \
    if (byte_size <= BLOCK_SIZE) {                                           \
        return bam->_BlockArena_##SIZE_NAME->AllocateBlock(source_location); \
    }

    BLOCK_ARENA_TYPES(X)
#undef X

    // If we got here, it means that we don't have a bit enough BlockArena.
    ASSERT(false);
    return {};
}

bool FreeBlock(BlockArenaManager* bam, const void* ptr) {
#define X(SIZE_NAME, BLOCK_SIZE, BLOCK_COUNT, ...)                                    \
    {                                                                                 \
        auto* block_arena = bam->_BlockArena_##SIZE_NAME;                             \
        if (i32 block_index = block_arena->GetBlockIndex(ptr); block_index != NONE) { \
            return block_arena->FreeBlockByIndex(block_index);                        \
        }                                                                             \
    }

    BLOCK_ARENA_TYPES(X)
#undef X

#ifdef HVN_BUILD_DEBUG
    if (!gRunningInTest) [[likely]] {
        ASSERT(false);
    }
#endif  // HVN_BUILD_DEBUG
    return false;
}

BlockMetadata* GetBlockMetadata(BlockArenaManager* bam, const void* ptr) {
#define X(SIZE_NAME, BLOCK_SIZE, BLOCK_COUNT, ...)                                    \
    {                                                                                 \
        auto* block_arena = bam->_BlockArena_##SIZE_NAME;                             \
        if (i32 block_index = block_arena->GetBlockIndex(ptr); block_index != NONE) { \
            return block_arena->GetBlockMetadataByIndex(block_index);                 \
        }                                                                             \
    }

    BLOCK_ARENA_TYPES(X)
#undef X

#ifdef HVN_BUILD_DEBUG
    if (!gRunningInTest) [[likely]] {
        ASSERT(false);
    }
#endif  // HVN_BUILD_DEBUG

    return nullptr;
}

// ALIGNMENT ---------------------------------------------------------------------------------------

void* Align(void* ptr, u64 alignment) {
    ASSERT(IsPowerOf2(alignment));

    u64 v = (u64)ptr;
    u64 mask = alignment - 1;

    // Clear the least significant bits up to alignment using bitwise AND with inverted mask.
    v &= ~mask;

    return (void*)v;
}

void* AlignForward(void* ptr, u64 alignment) {
    ASSERT(IsPowerOf2(alignment));

    // Same as (p % a), but faster since alignment is power of 2.
    u64 v = (u64)ptr;
    u64 mask = alignment - 1;
    u64 modulo = v & mask;

    if (modulo != 0) {
        v += alignment - modulo;
    }

    return (void*)v;
}

StringView ToMemoryString(Arena* arena, u64 bytes) {
    // Define thresholds for different units
    constexpr f64 kb_threshold = (f64)KILOBYTE;
    constexpr f64 mb_threshold = (f64)MEGABYTE;
    constexpr f64 gb_threshold = (f64)GIGABYTE;
    constexpr f64 tb_threshold = (f64)TERABYTE;

    f64 value;
    const char* suffix = nullptr;

    if (bytes >= tb_threshold) {
        value = bytes / tb_threshold;
        suffix = "TBs";
    } else if (bytes >= gb_threshold) {
        value = bytes / gb_threshold;
        suffix = "GBs";
    } else if (bytes >= mb_threshold) {
        value = bytes / mb_threshold;
        suffix = "MBs";
    } else if (bytes >= kb_threshold) {
        value = bytes / kb_threshold;
        suffix = "KBs";
    } else {
        value = (f32)bytes;
        suffix = "bytes";
    }

    // For fractional numbers, show up to 2 decimal places
    return Printf(arena, "%.2f %s", value, suffix);
}

}  // namespace kdk
