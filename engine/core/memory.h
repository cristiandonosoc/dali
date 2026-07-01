#pragma once

#include <engine/core/array.h>
#include <engine/core/defines.h>
#include <engine/core/string.h>

#include <bit>
#include <limits>
#include <source_location>
#include <span>

namespace kdk {

// System ------------------------------------------------------------------------------------------

constexpr u64 BYTE = 1;
constexpr u64 KILOBYTE = 1024 * BYTE;
constexpr u64 MEGABYTE = 1024 * KILOBYTE;
constexpr u64 GIGABYTE = 1024 * MEGABYTE;
constexpr u64 TERABYTE = 1024 * GIGABYTE;

// Arenas ------------------------------------------------------------------------------------------

struct Arena {
    FixedString<124> Name = {};
    u8* Start = nullptr;
    // NOTE: Shows the size of this particular arena.
    //       In the case of Extendable arenas, this represents only that "link".
    //       To know the total size of the arena, refer to |ExtendableData.TotalSize|.
    u64 Size = 0;
    u64 Offset = 0;

    struct Stats {
        u32 AllocCalls = 0;
        u32 FreeCalls = 0;
    } Stats = {};

    Arena* ParentArena = nullptr;

    Arena* GetPtr() { return this; }
};
bool IsValid(const Arena& arena);

Arena AllocateArena(StringView name, u64 size);
void FreeArena(Arena* arena);

// Creates an arena from another arena.
//
// Returns an FixedSize arena.
// IMPORTANT: It is the caller responsability that the owning arena will not be cleaned before this
// one. Basically correctly manage the arena lifetimes.
Arena CarveArena(StringView name, Arena* arena, u64 size);

void ArenaReset(Arena* arena);
[[nodiscard]] std::span<u8> ArenaPush(Arena* arena, u64 size, u64 alignment = 8);
[[nodiscard]] std::span<u8> ArenaPushZero(Arena* arena, u64 size, u64 alignment = 8);

template <typename T>
T* ArenaPush(Arena* arena) {
    return (T*)ArenaPush(arena, sizeof(T), alignof(T)).data();
}

template <typename T>
T* ArenaPushZero(Arena* arena) {
    return (T*)ArenaPushZero(arena, sizeof(T), alignof(T)).data();
}

// Allocates memory and calls the struct initializer.
template <typename T>
T* ArenaPushInit(Arena* arena) {
    auto data = ArenaPushZero(arena, sizeof(T), alignof(T));
    T* t = (T*)data.data();
    new (t) T;  // Placement new.
    return t;
}

template <typename T>
[[nodiscard]] std::span<T> ArenaPushArray(Arena* arena, u64 count) {
    T* t = (T*)ArenaPush(arena, count * sizeof(T), alignof(T)).data();
    return {t, count};
}

template <typename T>
[[nodiscard]] std::span<T> ArenaPushArrayZero(Arena* arena, u64 count) {
    T* t = (T*)ArenaPushZero(arena, count * sizeof(T), alignof(T)).data();
    return {t, count};
}

// Copy into the arena some data.
[[nodiscard]] inline std::span<u8> ArenaCopy(Arena* arena, std::span<u8> data, u64 alignment = 8) {
    auto new_data = ArenaPush(arena, data.size_bytes(), alignment);
    std::memcpy(new_data.data(), data.data(), data.size_bytes());
    return new_data;
}

// Non-copyable, Non-movable RAII style temporary arena.
// This is meant to be used in the scope of a stack frame only.
struct ScopedArena {
    Arena* Arena = nullptr;
    u64 OriginalOffset = 0;

    // Implicit converstion to Arena*
    operator struct Arena *() { return Arena; }

    explicit ScopedArena(struct Arena* arena, u64 original_offset);
    ~ScopedArena();

    ScopedArena(const ScopedArena&) = delete;
    ScopedArena& operator=(const ScopedArena&) = delete;

    ScopedArena(ScopedArena&&) = delete;
    ScopedArena& operator=(ScopedArena&&) = delete;
};
inline ScopedArena GetScopedArena(Arena* arena) { return ScopedArena(arena, arena->Offset); }

ScopedArena GetScratchArena(Arena* conflict1 = nullptr, Arena* conflict2 = nullptr);

// Use for testing.
std::span<Arena> ReferenceScratchArenas();

// BLOCK ARENA -------------------------------------------------------------------------------------

struct BlockMetadata {
    FixedString<128> ContextMsg;
    std::source_location SourceLocation;
};

struct BlockAllocationResult {
    std::span<u8> Memory = {};
    BlockMetadata* BlockMetadata = nullptr;
};

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
struct BlockArena {
    static constexpr u64 kTotalSize = BLOCK_SIZE * BLOCK_COUNT;
    static constexpr u64 kBlockSize = BLOCK_SIZE;
    static constexpr u64 kBlockCount = BLOCK_COUNT;
    static constexpr u64 kBlockShift = std::countr_zero(BLOCK_SIZE);

    Array<Array<u8, BLOCK_SIZE>, BLOCK_COUNT> Blocks = {};
    Array<BlockMetadata, BLOCK_COUNT> BlocksMetadata = {};
    Array<i32, BLOCK_COUNT> BlocksFreeList = {};

    FixedString<124> Name = {};
    i32 NextFreeBlock = 0;
    struct {
        i64 TotalAllocCalls = 0;
        i32 AllocatedBlocks = 0;
    } Stats;

    void Init(StringView name);
    void Shutdown() {}
    [[nodiscard]] BlockAllocationResult AllocateBlock(
        std::source_location source_location = std::source_location::current());
    bool FreeBlock(const void* ptr);
    template <typename T>
    bool FreeBlock(std::span<T> span) {
        return FreeBlock(span.data());
    }
    bool FreeBlockByIndex(i32 block_index);
    [[nodiscard]] bool BlockIndexAllocated(u32 block_index) const;

    // Convert pointer to handle. Returns NONE handle if pointer doesn't belong to this arena.
    [[nodiscard]] i32 GetBlockIndex(const void* ptr) const;
    template <typename T>
    [[nodiscard]] i32 GetBlockIndex(std::span<T> span) const {
        return GetBlockIndex(span.data());
    }

    [[nodiscard]] BlockMetadata* GetBlockMetadataByIndex(i32 block_index);

    // For testing.
    std::span<u8> GetBlockByIndex(i32 index);
};

// clang-format off
static_assert(BlockArena<  1 * KILOBYTE, 1024>::kBlockShift == 10);
static_assert(BlockArena<  4 * KILOBYTE, 1024>::kBlockShift == 12);
static_assert(BlockArena< 16 * KILOBYTE, 1024>::kBlockShift == 14);
static_assert(BlockArena< 64 * KILOBYTE, 1024>::kBlockShift == 16);
static_assert(BlockArena<256 * KILOBYTE, 1024>::kBlockShift == 18);
static_assert(BlockArena<  1 * MEGABYTE, 1024>::kBlockShift == 20);
// clang-format on

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
void BlockArena<BLOCK_SIZE, BLOCK_COUNT>::Init(StringView name) {
    Name = name;
    for (u32 i = 0; i < BLOCK_COUNT; i++) {
        BlocksFreeList[i] = i + 1;
    }
    BlocksFreeList[BLOCK_COUNT - 1] = std::numeric_limits<i32>::min();
    NextFreeBlock = 0;
    Stats = {};
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
BlockAllocationResult BlockArena<BLOCK_SIZE, BLOCK_COUNT>::AllocateBlock(
    std::source_location source_location) {
    if (Stats.AllocatedBlocks == BLOCK_COUNT) [[unlikely]] {
        ASSERT(false);
        return {};
    }

    // Find the next free block index.
    u32 block_index = NextFreeBlock;
    NextFreeBlock = BlocksFreeList[block_index];
    BlocksFreeList[block_index] = NONE;
    ASSERT(block_index < BLOCK_COUNT);
    ASSERT(block_index < (1 << 24));

    std::span<u8> block_span = Blocks[block_index].ToSpan();
    BlockMetadata* metadata = &BlocksMetadata[block_index];
    metadata->SourceLocation = std::move(source_location);

    Stats.AllocatedBlocks++;
    Stats.TotalAllocCalls++;

    // return {handle, block_span, metadata};
    return {block_span, metadata};
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
bool BlockArena<BLOCK_SIZE, BLOCK_COUNT>::FreeBlock(const void* ptr) {
    i32 block_index = GetBlockIndex(ptr);
    return FreeBlockByIndex(block_index);
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
bool BlockArena<BLOCK_SIZE, BLOCK_COUNT>::FreeBlockByIndex(i32 block_index) {
    if (!BlockIndexAllocated((u32)block_index)) {
        ASSERT(false);
        return false;
    }

    // Add the block back to the free list.
    BlocksFreeList[block_index] = NextFreeBlock;
    NextFreeBlock = block_index;

    Stats.AllocatedBlocks--;
    ASSERT(Stats.AllocatedBlocks >= 0);

    return true;
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
BlockMetadata* BlockArena<BLOCK_SIZE, BLOCK_COUNT>::GetBlockMetadataByIndex(i32 block_index) {
    ASSERT(BlockIndexAllocated(block_index));
    return &BlocksMetadata[block_index];
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
std::span<u8> BlockArena<BLOCK_SIZE, BLOCK_COUNT>::GetBlockByIndex(i32 block_index) {
    ASSERT(BlockIndexAllocated(block_index));
    return Blocks[block_index].ToSpan();
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
bool BlockArena<BLOCK_SIZE, BLOCK_COUNT>::BlockIndexAllocated(u32 block_index) const {
    if (block_index >= BLOCK_COUNT) {
        return false;
    }

    return BlocksFreeList[block_index] == NONE;
}

template <u32 BLOCK_SIZE, u32 BLOCK_COUNT>
i32 BlockArena<BLOCK_SIZE, BLOCK_COUNT>::GetBlockIndex(const void* ptr) const {
#ifdef HVN_BUILD_DEBUG
    if (!gRunningInTest) [[likely]] {
        ASSERT(ptr);
    }
#endif  // HVN_BUILD_DEBUG

    u8* byte_ptr = (u8*)ptr;
    u8* arena_start = (u8*)&Blocks[0];
    if (byte_ptr < arena_start) {
        return NONE;
    }

    // u8* arena_end = arena_start + kTotalSize;

    u64 offset = byte_ptr - arena_start;
    u32 block_index = (u32)(offset >> kBlockShift);
    if (block_index >= BLOCK_COUNT) {
        return NONE;
    }

    // The given pointer must be at the same address as the start of the block index we calculated.
#ifdef HVN_BUILD_DEBUG
    if (!gRunningInTest) [[likely]] {
        ASSERT(Blocks[block_index].Data == byte_ptr);
    }
#endif  // HVN_BUILD_DEBUG
    return block_index;
}

// Format: (SIZE_NAME, BLOCK_SIZE, BLOCK_COUNT, BLOCK_SHIFT)
// clang-format off
#define BLOCK_ARENA_TYPES(X)					\
    X( 1KB,  1 * KILOBYTE, (u32)1 << 10, 10)	\
    X( 4KB,  4 * KILOBYTE, (u32)1 << 10, 12)	\
    X(16KB, 16 * KILOBYTE, (u32)1 << 10, 14)
// clang-format on

struct BlockArenaManager {
#define X(SIZE_NAME, BLOCK_SIZE, BLOCK_COUNT, ...) \
    BlockArena<BLOCK_SIZE, BLOCK_COUNT>* _BlockArena_##SIZE_NAME = nullptr;
    BLOCK_ARENA_TYPES(X)
#undef X
};

void Init(BlockArenaManager* bam);
void Shutdown(BlockArenaManager* bam);

BlockAllocationResult AllocateBlock(
    BlockArenaManager* bam,
    u32 byte_size,
    std::source_location source_location = std::source_location::current());
bool FreeBlock(BlockArenaManager* bam, const void* ptr);
template <typename T>
bool FreeBlock(BlockArenaManager* bam, std::span<T> span) {
    return FreeBlock(bam, span.data());
}

BlockMetadata* GetBlockMetadata(BlockArenaManager* bam, const void* ptr);
template <typename T>
BlockMetadata* GetBlockMetadata(BlockArenaManager* bam, std::span<T> span) {
    return GetBlockMetadata(bam, span.data());
}

// Memory Alignment --------------------------------------------------------------------------------

inline bool IsPowerOf2(u64 a) { return ((a & (a - 1)) == 0); }
void* Align(void* ptr, u64 alignment);
void* AlignForward(void* ptr, u64 alignment);

// UTILITIES ---------------------------------------------------------------------------------------

StringView ToMemoryString(Arena* arena, u64 bytes);

template <typename T>
void ZeroSpan(std::span<T> span) {
    std::memset(span.data(), 0, span.size_bytes());
}

}  // namespace kdk
