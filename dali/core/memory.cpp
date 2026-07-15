#include <dali/core/memory.h>

#include <dali/core/container.h>

#include <cstdlib>
#include <cstring>

namespace kdk {

namespace memory_private {

static constexpr u32 kScratchArenaSize = 32 * MEGABYTE;

// Scratch scopes stack on a shared arena (see Arena::GetScratch), so the pool only needs to cover
// conflict skipping (max 2 conflicts + 1) — not call depth. 4 gives headroom.
Array<StringView, 4> kScratchArenaNames = {
    "ScratchArena0"sv,
    "ScratchArena1"sv,
    "ScratchArena2"sv,
    "ScratchArena3"sv,
};

void* AllocMemory(Arena* arena, u64 size) {
    arena->Stats.AllocCalls++;
    return malloc(size);
}

void FreeMemory(Arena* arena, void* ptr) {
    arena->Stats.FreeCalls++;
    free(ptr);
}

}  // namespace memory_private

bool Arena::IsValid() const {
    if (!Start) {
        return false;
    }

    if (Size == 0) {
        return false;
    }

    if (Offset >= Size) {
        return false;
    }

    return true;
}

Arena Arena::Allocate(StringView name, u64 size) {
    Arena out = {};
    Arena arena{
        .Size = size,
        .Offset = 0,
    };
    arena.Start = (u8*)memory_private::AllocMemory(&arena, size);
    out = std::move(arena);

    out.Name = name;
    return out;
}

void Arena::Free(Arena* arena) {
    ASSERT(arena->IsValid());

    memory_private::FreeMemory(arena, arena->Start);
}

Arena Arena::Carve(StringView name, u64 size) {
    // |this| is a conflict: the carved block is pushed into this arena, and if the scratch were
    // this same arena its scope-exit reset would reclaim that block.
    auto scratch = Arena::GetScratch(this);
    StringView out_name = Printf(scratch, "%s:%s", Name.Str(), name.Str());

    Arena out = {
        .Name = out_name,
        .Start = Push(size).data(),
        .Size = size,
    };

    return out;
}

std::span<u8> Arena::Push(u64 size, u64 alignment) {
    ASSERT(IsValid());

    u8* out = nullptr;

    // Determine the new offset
    u8* ptr = Start + Offset;
    ptr = (u8*)AlignForward(ptr, alignment);
    u64 offset = ptr - Start;

    ASSERT(offset + size < Size);
    Offset = offset + size;
    out = ptr;

    return {out, size};
}

std::span<u8> Arena::PushZero(u64 size, u64 alignment) {
    auto data = Push(size, alignment);
    std::memset(data.data(), 0, size);
    return data;
}

std::span<Arena> Arena::ReferenceScratch() {
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
            arena = Arena::Allocate(name, kScratchArenaSize);
        }

        gInitialized = true;
    }

    return gArenas;
}

ScopedArena::ScopedArena(struct Arena* arena, u64 original_offset)
    : Arena(arena), OriginalOffset(original_offset) {}

ScopedArena::~ScopedArena() {
    ASSERTF(Arena, "No weird shenanigans with arenas!");
#ifndef NDEBUG
    // Poison the reclaimed range so any pointer kept past this scope (e.g. a missed GetScratch
    // conflict) reads 0xDD deterministically instead of stale-but-valid data.
    std::memset(Arena->Start + OriginalOffset, 0xDD, Arena->Offset - OriginalOffset);
#endif  // NDEBUG
    Arena->Offset = OriginalOffset;
}

ScopedArena Arena::GetScratch(const Arena* conflict1, const Arena* conflict2) {
    auto scratch_arenas = Arena::ReferenceScratch();

    // Hand out the first arena that isn't a conflict. Plain (conflict-less) calls all share the
    // first arena: the scope restores the offset on exit, so nesting stacks safely and depth is
    // unbounded by the pool size.
    for (Arena& a : scratch_arenas) {
        if (&a == conflict1) {
            continue;
        }
        if (&a == conflict2) {
            continue;
        }
        return a.GetScoped();
    }

    ASSERTF(false, "No scratch arena could be found");
    return scratch_arenas[0].GetScoped();
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

StringView Arena::ToMemoryString(u64 bytes) {
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
    return Printf(this, "%.2f %s", value, suffix);
}

}  // namespace kdk
