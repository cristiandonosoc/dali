#pragma once

#include <engine/core/algorithm.h>
#include <engine/core/defines.h>
#include <engine/core/function.h>

#include <cstddef>
#include <span>

namespace kdk {

struct Arena;

// // TODO(cdc): Replace with no allocation version.
// template <typename T>
// using Function = std::function<T>;

template <typename T>
std::span<T> MakeSpan(T& t) {
    return {&t, 1};
}

// Iterator ----------------------------------------------------------------------------------------

template <typename T>
struct Iterator {
    Iterator() = default;

    T& Get() { return _Entities[_Index]; }
    T* GetPtr() { return _Entities + _Index; }
    T& operator*() { return _Entities[_Index]; }
    T* operator->() { return &_Entities[_Index]; }

    operator bool() const { return _Index < _EntityCount; }
    void operator++() { _Index++; }
    void operator++(int) { _Index++; }

    Iterator(T* entities, i32 entity_count, i32 index)
        : _Entities(entities), _EntityCount(entity_count), _Index(index) {}

    T* _Entities = nullptr;
    i32 _EntityCount = 0;
    i32 _Index = 0;
};

template <typename T>
struct Optional {
    Optional() : _HasValue(false) {}  // Don't construct _Value in this case.
    Optional(const T& value) : _Value(value), _HasValue(true) {}
    Optional(T&& value) : _Value(std::move(value)), _HasValue(true) {}

    // No copy constructor/assignment
    Optional(const Optional&) = delete;
    Optional& operator=(const Optional&) = delete;

    // Move constructor/assignment
    Optional(Optional&& other) noexcept
        : _Value(std::move(other._Value)), _HasValue(other._HasValue) {
        other._HasValue = false;
    }

    Optional& operator=(Optional&& other) noexcept {
        if (this != &other) {
            _Value = std::move(other._Value);
            _HasValue = other._HasValue;
            other._HasValue = false;
        }
        return *this;
    }

    explicit operator bool() const { return _HasValue; }
    bool HasValue() const { return _HasValue; }

    // T& GetValue() {
    //     ASSERT(_HasValue);
    //     return _Value;
    // }
    const T& GetValue() const {
        ASSERT(_HasValue);
        return _Value;
    }
    // T* operator->() {
    //     ASSERT(_HasValue);
    //     return &_Value;
    // }
    const T* operator->() const {
        ASSERT(_HasValue);
        return &_Value;
    }

    void Reset() { _HasValue = false; }

   private:
    T _Value;
    bool _HasValue;
};

// Array -------------------------------------------------------------------------------------------

template <typename T, i32 N>
struct Array {
    using ElementType = T;
    static constexpr i32 Size = N;  // For convenience of API with other containers.
    static constexpr i32 kSize = N;

    T _Data[N];

    T* DataPtr() { return _Data; }
    const T* DataPtr() const { return _Data; }

    T& operator[](i32 index);
    const T& operator[](i32 index) const;

    T& First() { return _Data[0]; }
    const T& First() const { return _Data[0]; }

    T& Last() { return _Data[Size - 1]; }
    const T& Last() const { return _Data[Size - 1]; }

    T& At(i32 index) { return _Data[index]; }
    const T& At(i32 index) const { return _Data[index]; }

    std::span<T> ToSpan() { return {_Data, (u32)Size}; }
    std::span<const T> ToSpan() const { return {_Data, (u32)Size}; }

    std::span<T> Slice(i32 begin, i32 end);
    std::span<const T> Slice(i32 begin, i32 end) const;

    std::pair<i32, T*> Find(const T& elem);
    std::pair<i32, const T*> Find(const T& elem) const;

    template <typename PREDICATE>
    std::pair<i32, T*> FindPred(const PREDICATE& pred);
    template <typename PREDICATE>
    std::pair<i32, const T*> FindPred(const PREDICATE& pred) const;

    bool Contains(const T& elem) const { return Find(elem).first != NONE; }

    void Sort() { ::kdk::Sort(begin(), end()); }

    template <typename PREDICATE>
    void SortPred(const PREDICATE& pred);

    void DefaultInitialize() {
        for (i32 i = 0; i < N; i++) {
            // In-place new to construct the object
            new (&_Data[i]) T();
        }
    }

    // Iterator support
    T* begin() { return _Data; }
    T* end() { return _Data + Size; }
    const T* begin() const { return _Data; }
    const T* end() const { return _Data + Size; }
    const T* cbegin() const { return _Data; }
    const T* cend() const { return _Data + Size; }
};
static_assert(sizeof(Array<int, 4>) == sizeof(Array<int, 4>::_Data));
// static_assert(offsetof(Array<int, 4>, _Data) == 0);

// Deduction guide for Array, so we can write code like:
//
//    Array filters = { nfdfilteritem_t{"YAML", "yml,yaml"} };
template <typename T, typename... U>
    requires(std::same_as<T, U> && ...)
Array(T, U...) -> Array<T, 1 + sizeof...(U)>;

// FixedVector -------------------------------------------------------------------------------------

template <typename T, i32 N>
struct FixedVector {
    using ElementType = T;
    static constexpr i32 kMaxSize = N;

    T _Data[N];
    i32 Size = 0;

    void Clear();

    T* DataPtr() { return _Data; }
    const T* DataPtr() const { return _Data; }

    T& operator[](i32 index);
    const T& operator[](i32 index) const;

    T& First() { return _Data[0]; }
    const T& First() const { return _Data[0]; }

    T& Last() { return _Data[Size - 1]; }
    const T& Last() const { return _Data[Size - 1]; }

    T& At(i32 index) { return _Data[index]; }
    const T& At(i32 index) const { return _Data[index]; }

    std::span<T> ToSpan() { return {_Data, (u32)Size}; }
    std::span<const T> ToSpan() const { return {_Data, (u32)Size}; }

    std::span<T> Slice(i32 begin, i32 end);
    std::span<const T> Slice(i32 begin, i32 end) const;

    T& Push(const T& elem);
    template <typename ITERATOR>
    i32 Push(ITERATOR begin, ITERATOR end);
    i32 Push(std::span<const T> span) { return Push(span.begin(), span.end()); }

    void Pop();

    bool IsFull() const { return Size >= N; }
    bool IsEmpty() const { return Size == 0; }
    i32 Capacity() const { return N; }

    std::pair<i32, T*> Find(const T& elem);
    std::pair<i32, const T*> Find(const T& elem) const;

    template <typename PREDICATE>
    std::pair<i32, T*> FindPred(const PREDICATE& pred);
    template <typename PREDICATE>
    std::pair<i32, const T*> FindPred(const PREDICATE& pred) const;

    bool Contains(const T& elem) const { return Find(elem).first != NONE; }

    void Sort() { ::kdk::Sort(begin(), end()); }

    template <typename PREDICATE>
    void SortPred(const PREDICATE& pred);

    i32 Remove(const T& elem, i32 count = 1);
    i32 RemovePred(const Function<bool(const T&)>& pred, i32 count = 1);

    i32 RemoveAll(const T& elem) { return Remove(elem, 0); }
    i32 RemoveAllPred(const Function<bool(const T&)>& pred) { return RemovePred(pred, 0); }

    bool RemoveUnordered(const T& elem);
    bool RemoveUnorderedPred(const Function<bool(const T& elem)>& pred);
    void RemoveUnorderedAt(i32 index);

    // Iterator support
    T* begin() { return _Data; }
    T* end() { return _Data + Size; }
    const T* begin() const { return _Data; }
    const T* end() const { return _Data + Size; }
    const T* cbegin() const { return _Data; }
    const T* cend() const { return _Data + Size; }
};
// Debug which requirements are failing
static_assert(std::is_trivially_copyable_v<FixedVector<const char*, 4>>, "Not trivially copyable");
static_assert(std::is_standard_layout_v<FixedVector<const char*, 4>>, "Not standard layout");

// AllocatedVector ---------------------------------------------------------------------------------

template <typename T>
struct AllocatedVector {
    using ElementType = T;

    std::span<T> _Data;
    i32 Size = 0;

    AllocatedVector() = default;
    AllocatedVector(std::span<T> data) : _Data(data) {}

    void Clear();

    T* DataPtr() { return _Data; }
    const T* DataPtr() const { return _Data; }

    T& operator[](i32 index);
    const T& operator[](i32 index) const;

    T& First() { return _Data[0]; }
    const T& First() const { return _Data[0]; }

    T& Last() { return _Data[Size - 1]; }
    const T& Last() const { return _Data[Size - 1]; }

    T& At(i32 index) { return _Data[index]; }
    const T& At(i32 index) const { return _Data[index]; }

    std::span<T> ToSpan() { return {_Data, (u32)Size}; }
    std::span<const T> ToSpan() const { return {_Data, (u32)Size}; }

    std::span<T> Slice(i32 begin, i32 end);
    std::span<const T> Slice(i32 begin, i32 end) const;

    T& Push(const T& elem);
    template <typename ITERATOR>
    i32 Push(ITERATOR begin, ITERATOR end);
    i32 Push(std::span<const T> span) { return Push(span.begin(), span.end()); }

    void Pop();

    bool IsFull() const { return Size >= _Data.size(); }
    bool IsEmpty() const { return Size == 0; }
    i32 Capacity() const { return (i32)_Data.size(); }

    std::pair<i32, T*> Find(const T& elem);
    std::pair<i32, const T*> Find(const T& elem) const;

    template <typename PREDICATE>
    std::pair<i32, T*> FindPred(const PREDICATE& pred);
    template <typename PREDICATE>
    std::pair<i32, const T*> FindPred(const PREDICATE& pred) const;

    bool Contains(const T& elem) const { return Find(elem).first != NONE; }

    void Sort() { ::kdk::Sort(begin(), end()); }

    template <typename PREDICATE>
    void SortPred(const PREDICATE& pred);

    i32 Remove(const T& elem, i32 count = 1);
    i32 RemovePred(const Function<bool(const T&)>& pred, i32 count = 1);

    i32 RemoveAll(const T& elem) { return Remove(elem, 0); }
    i32 RemoveAllPred(const Function<bool(const T&)>& pred) { return RemovePred(pred, 0); }

    bool RemoveUnordered(const T& elem);
    bool RemoveUnorderedPred(const Function<bool(const T& elem)>& pred);
    void RemoveUnorderedAt(i32 index);

    // Iterator support
    T* begin() { return &*(_Data.begin()); }
    T* end() { return &*(_Data.begin() + Size); }
    const T* begin() const { return &*_Data.begin(); }
    const T* end() const { return &*(_Data.begin() + Size); }
    const T* cbegin() const { return &*(_Data.begin()); }
    const T* cend() const { return &*(_Data.begin() + Size); }
};
// Debug which requirements are failing
static_assert(std::is_trivially_copyable_v<AllocatedVector<const char*>>, "Not trivially copyable");
static_assert(std::is_standard_layout_v<AllocatedVector<const char*>>, "Not standard layout");

// DynArray ----------------------------------------------------------------------------------------

static constexpr i32 kDynArrayInitialCap = 4;

template <typename T>
struct DynArray {
    Arena* _Arena = nullptr;  // Not owning!
    T* _Data = nullptr;
    i32 Size = 0;
    i32 Cap = 0;

    bool IsValid() const { return _Arena != nullptr && _Data != nullptr; }
    void SetArena(Arena* arena);

    T* DataPtr() { return _Data; }
    const T* DataPtr() const { return _Data; }

    T& operator[](i32 index);
    const T& operator[](i32 index) const;

    T& First() { return _Data[0]; }
    const T& First() const { return _Data[0]; }

    T& Last() { return _Data[Size - 1]; }
    const T& Last() const { return _Data[Size - 1]; }

    T& At(i32 index) { return _Data[index]; }
    const T& At(i32 index) const { return _Data[index]; }

    T& Push(const T& elem);
    T Pop();
    void Reserve(i32 new_cap);

    void Clear();

    // Iterator support
    T* begin() { return _Data; }
    T* end() { return _Data + Size; }
    const T* begin() const { return _Data; }
    const T* end() const { return _Data + Size; }
    const T* cbegin() const { return _Data; }
    const T* cend() const { return _Data + Size; }
};
static_assert(sizeof(DynArray<int>) == 24);

template <typename T>
DynArray<T> NewDynArray(Arena* arena, i32 initial_cap = kDynArrayInitialCap) {
    T* base = (T*)ArenaPush(arena, initial_cap * sizeof(T), alignof(T)).data();
    return DynArray<T>{
        ._Arena = arena,
        ._Data = base,
        .Size = 0,
        .Cap = initial_cap,
    };
}

// Queue -------------------------------------------------------------------------------------------

template <typename T, i32 N>
struct Queue {
    using ElementType = T;
    static constexpr i32 kMaxSize = N;

    T _Data[N];
    i32 _Front = 0;  // Index of the first element
    i32 _Back = 0;   // Index of the last element (one past the last element)
    i32 Size = 0;

    void Clear();

    T* DataPtr() { return _Data; }
    const T* DataPtr() const { return _Data; }

    void Push(const T& elem);
    T Pop();

    T& Front();
    const T& Front() const;

    T& Back();
    const T& Back() const;

    bool IsFull() const { return Size >= N; }
    bool IsEmpty() const { return Size == 0; }
    i32 Capacity() const { return N; }
};

}  // namespace kdk

// #################################################################################################
// TEMPLATE DEFINITIONS
// #################################################################################################

namespace kdk {

// Array -------------------------------------------------------------------------------------------

template <typename T, i32 N>
T& Array<T, N>::operator[](i32 index) {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T, i32 N>
const T& Array<T, N>::operator[](i32 index) const {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T, i32 N>
std::span<T> Array<T, N>::Slice(i32 begin, i32 end) {
    ASSERT(begin >= 0 && begin <= Size);
    ASSERT(end >= begin && end <= Size);
    return {_Data + begin, static_cast<size_t>(end - begin)};
}

template <typename T, i32 N>
std::span<const T> Array<T, N>::Slice(i32 begin, i32 end) const {
    ASSERT(begin >= 0 && begin <= Size);
    ASSERT(end >= begin && end <= Size);
    return {_Data + begin, static_cast<size_t>(end - begin)};
}

template <typename T, i32 N>
std::pair<i32, T*> Array<T, N>::Find(const T& elem) {
    return ::kdk::Find<T>(begin(), end(), elem);
}

template <typename T, i32 N>
std::pair<i32, const T*> Array<T, N>::Find(const T& elem) const {
    return ::kdk::Find<T>(begin(), end(), elem);
}

template <typename T, i32 N>
template <typename PREDICATE>
std::pair<i32, T*> Array<T, N>::FindPred(const PREDICATE& pred) {
    return ::kdk::FindPred<T>(begin(), end(), pred);
}

template <typename T, i32 N>
template <typename PREDICATE>
std::pair<i32, const T*> Array<T, N>::FindPred(const PREDICATE& pred) const {
    return ::kdk::FindPred<T>(begin(), end(), pred);
}

template <typename T, i32 N>
template <typename PREDICATE>
void Array<T, N>::SortPred(const PREDICATE& pred) {
    return ::kdk::SortPred(begin(), end(), pred);
}

// FixedVector -------------------------------------------------------------------------------------

template <typename T, i32 N>
void FixedVector<T, N>::Clear() {
    // If it's not trivially copyable, call the destructor for each element.
    if constexpr (!std::is_trivially_copyable_v<T>) {
        for (i32 i = 0; i < Size; i++) {
            _Data[i].~T();  // In-place destructor.
        }
    }

    Size = 0;
}

template <typename T, i32 N>
T& FixedVector<T, N>::operator[](i32 index) {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T, i32 N>
const T& FixedVector<T, N>::operator[](i32 index) const {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T, i32 N>
std::span<T> FixedVector<T, N>::Slice(i32 begin, i32 end) {
    ASSERT(begin >= 0 && begin <= Size);
    ASSERT(end >= begin && end <= Size);
    return {_Data + begin, static_cast<size_t>(end - begin)};
}

template <typename T, i32 N>
std::span<const T> FixedVector<T, N>::Slice(i32 begin, i32 end) const {
    ASSERT(begin >= 0 && begin <= Size);
    ASSERT(end >= begin && end <= Size);
    return {_Data + begin, static_cast<size_t>(end - begin)};
}

template <typename T, i32 N>
template <typename ITERATOR>
i32 FixedVector<T, N>::Push(ITERATOR begin, ITERATOR end) {
    i32 count = 0;
    for (auto it = begin; it != end && !IsFull(); ++it, ++count) {
        Push(*it);
    }
    return count;
}

template <typename T, i32 N>
T& FixedVector<T, N>::Push(const T& elem) {
    ASSERT(!IsFull());
    T* ptr = _Data + Size;
    Size++;

    if constexpr (std::is_pod_v<T>) {
        *ptr = elem;
    } else {
        // For non POD things, we want to make sure the memory will not do weird things.
        std::memset(ptr, 0, sizeof(T));
        new (ptr) T(elem);  // Placement new.
    }

    return Last();
}

template <typename T, i32 N>
void FixedVector<T, N>::Pop() {
    if (Size == 0) [[unlikely]] {
        return;
    }

    T& elem = _Data[Size - 1];
    Size--;

    if constexpr (!std::is_trivially_copyable_v<T>) {
        // For non-POD, we want to copy the result and destroy the original.
        elem.~T();  // In-place destructor.
    }
}

template <typename T, i32 N>
std::pair<i32, T*> FixedVector<T, N>::Find(const T& elem) {
    return ::kdk::Find<T>(begin(), end(), elem);
}

template <typename T, i32 N>
std::pair<i32, const T*> FixedVector<T, N>::Find(const T& elem) const {
    return ::kdk::Find<T>(begin(), end(), elem);
}

template <typename T, i32 N>
template <typename PREDICATE>
std::pair<i32, T*> FixedVector<T, N>::FindPred(const PREDICATE& pred) {
    return ::kdk::FindPred<T>(begin(), end(), pred);
}

template <typename T, i32 N>
template <typename PREDICATE>
std::pair<i32, const T*> FixedVector<T, N>::FindPred(const PREDICATE& pred) const {
    return ::kdk::FindPred<T>(begin(), end(), pred);
}

template <typename T, i32 N>
template <typename PREDICATE>
void FixedVector<T, N>::SortPred(const PREDICATE& pred) {
    return ::kdk::SortPred(begin(), end(), pred);
}

template <typename T, i32 N>
i32 FixedVector<T, N>::Remove(const T& elem, i32 count) {
    if (Size == 0) [[unlikely]] {
        return 0;  // Nothing to remove
    }

    if (count == 0) {
        count = N;
    }

    // Mark positions to remove with indices
    i32 write_pos = 0;  // Position to write next valid element
    i32 removed = 0;    // Count of elements marked for removal

    // First pass: identify elements to keep/remove
    for (i32 read_pos = 0; read_pos < Size; read_pos++) {
        if (removed < count && _Data[read_pos] == elem) {
            // Element should be removed
            removed++;
        } else {
            // Element should be kept - move it if necessary
            if (write_pos != read_pos) {
                if constexpr (std::is_trivially_copyable_v<T>) {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                } else {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                    _Data[read_pos].~T();
                }
            }
            write_pos++;
        }
    }

    // Update size
    Size = write_pos;
    return removed;
}

template <typename T, i32 N>
i32 FixedVector<T, N>::RemovePred(const Function<bool(const T&)>& pred, i32 count) {
    if (Size == 0) [[unlikely]] {
        return 0;  // Nothing to remove
    }

    if (count == 0) {
        count = N;
    }

    // Mark positions to remove with indices
    i32 write_pos = 0;  // Position to write next valid element
    i32 removed = 0;    // Count of elements marked for removal

    // First pass: identify elements to keep/remove
    for (i32 read_pos = 0; read_pos < Size; read_pos++) {
        if (removed < count && pred(_Data[read_pos])) {
            // Element should be removed
            removed++;
        } else {
            // Element should be kept - move it if necessary
            if (write_pos != read_pos) {
                if constexpr (std::is_trivially_copyable_v<T>) {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                } else {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                    _Data[read_pos].~T();
                }
            }
            write_pos++;
        }
    }

    // Update size
    Size = write_pos;
    return removed;
}

template <typename T, i32 N>
bool FixedVector<T, N>::RemoveUnordered(const T& elem) {
    if (Size == 0) [[unlikely]] {
        return false;
    }

    // Find the element to remove
    for (i32 i = 0; i < Size; i++) {
        if (_Data[i] == elem) {
            RemoveUnorderedAt(i);
            return true;
        }
    }

    return false;  // Element not found
}

template <typename T, i32 N>
bool FixedVector<T, N>::RemoveUnorderedPred(const Function<bool(const T&)>& pred) {
    if (Size == 0) [[unlikely]] {
        return false;
    }

    // Find the element to remove
    for (i32 i = 0; i < Size; i++) {
        if (pred(_Data[i])) {
            RemoveUnorderedAt(i);
            return true;
        }
    }

    return false;  // Element not found
}

template <typename T, i32 N>
void FixedVector<T, N>::RemoveUnorderedAt(i32 index) {
    ASSERT(index >= 0 && index < Size);

    // Move the last element to this position
    if (index != Size - 1) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            _Data[index] = _Data[Size - 1];
        } else {
            _Data[index] = std::move(_Data[Size - 1]);
            _Data[Size - 1].~T();
        }
    } else if constexpr (!std::is_trivially_copyable_v<T>) {
        // If removing the last element and it's non-trivial, call destructor
        _Data[index].~T();
    }

    Size--;
}

// AllocatedVector
// -------------------------------------------------------------------------------------

template <typename T>
void AllocatedVector<T>::Clear() {
    // If it's not trivially copyable, call the destructor for each element.
    if constexpr (!std::is_trivially_copyable_v<T>) {
        for (i32 i = 0; i < Size; i++) {
            _Data[i].~T();  // In-place destructor.
        }
    }

    Size = 0;
}

template <typename T>
T& AllocatedVector<T>::operator[](i32 index) {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T>
const T& AllocatedVector<T>::operator[](i32 index) const {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T>
std::span<T> AllocatedVector<T>::Slice(i32 begin, i32 end) {
    ASSERT(begin >= 0 && begin <= Size);
    ASSERT(end >= begin && end <= Size);
    return {_Data + begin, static_cast<size_t>(end - begin)};
}

template <typename T>
std::span<const T> AllocatedVector<T>::Slice(i32 begin, i32 end) const {
    ASSERT(begin >= 0 && begin <= Size);
    ASSERT(end >= begin && end <= Size);
    return {_Data + begin, static_cast<size_t>(end - begin)};
}

template <typename T>
template <typename ITERATOR>
i32 AllocatedVector<T>::Push(ITERATOR begin, ITERATOR end) {
    i32 count = 0;
    for (auto it = begin; it != end && !IsFull(); ++it, ++count) {
        Push(*it);
    }
    return count;
}

template <typename T>
T& AllocatedVector<T>::Push(const T& elem) {
    ASSERT(!IsFull());
    T* ptr = (&_Data[0]) + Size;
    Size++;

    if constexpr (std::is_pod_v<T>) {
        *ptr = elem;
    } else {
        // For non POD things, we want to make sure the memory will not do weird things.
        std::memset(ptr, 0, sizeof(T));
        new (ptr) T(elem);  // Placement new.
    }

    return Last();
}

template <typename T>
void AllocatedVector<T>::Pop() {
    if (Size == 0) [[unlikely]] {
        return;
    }

    T& elem = _Data[Size - 1];
    Size--;

    if constexpr (!std::is_trivially_copyable_v<T>) {
        // For non-POD, we want to copy the result and destroy the original.
        elem.~T();  // In-place destructor.
    }
}

template <typename T>
std::pair<i32, T*> AllocatedVector<T>::Find(const T& elem) {
    return ::kdk::Find<T>(begin(), end(), elem);
}

template <typename T>
std::pair<i32, const T*> AllocatedVector<T>::Find(const T& elem) const {
    return ::kdk::Find<T>(begin(), end(), elem);
}

template <typename T>
template <typename PREDICATE>
std::pair<i32, T*> AllocatedVector<T>::FindPred(const PREDICATE& pred) {
    return ::kdk::FindPred<T>(begin(), end(), pred);
}

template <typename T>
template <typename PREDICATE>
std::pair<i32, const T*> AllocatedVector<T>::FindPred(const PREDICATE& pred) const {
    return ::kdk::FindPred<T>(begin(), end(), pred);
}

template <typename T>
template <typename PREDICATE>
void AllocatedVector<T>::SortPred(const PREDICATE& pred) {
    return ::kdk::SortPred(begin(), end(), pred);
}

template <typename T>
i32 AllocatedVector<T>::Remove(const T& elem, i32 count) {
    if (Size == 0) [[unlikely]] {
        return 0;  // Nothing to remove
    }

    if (count == 0) {
        count = Capacity();
    }

    // Mark positions to remove with indices
    i32 write_pos = 0;  // Position to write next valid element
    i32 removed = 0;    // Count of elements marked for removal

    // First pass: identify elements to keep/remove
    for (i32 read_pos = 0; read_pos < Size; read_pos++) {
        if (removed < count && _Data[read_pos] == elem) {
            // Element should be removed
            removed++;
        } else {
            // Element should be kept - move it if necessary
            if (write_pos != read_pos) {
                if constexpr (std::is_trivially_copyable_v<T>) {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                } else {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                    _Data[read_pos].~T();
                }
            }
            write_pos++;
        }
    }

    // Update size
    Size = write_pos;
    return removed;
}

template <typename T>
i32 AllocatedVector<T>::RemovePred(const Function<bool(const T&)>& pred, i32 count) {
    if (Size == 0) [[unlikely]] {
        return 0;  // Nothing to remove
    }

    if (count == 0) {
        count = Capacity();
    }

    // Mark positions to remove with indices
    i32 write_pos = 0;  // Position to write next valid element
    i32 removed = 0;    // Count of elements marked for removal

    // First pass: identify elements to keep/remove
    for (i32 read_pos = 0; read_pos < Size; read_pos++) {
        if (removed < count && pred(_Data[read_pos])) {
            // Element should be removed
            removed++;
        } else {
            // Element should be kept - move it if necessary
            if (write_pos != read_pos) {
                if constexpr (std::is_trivially_copyable_v<T>) {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                } else {
                    _Data[write_pos] = std::move(_Data[read_pos]);
                    _Data[read_pos].~T();
                }
            }
            write_pos++;
        }
    }

    // Update size
    Size = write_pos;
    return removed;
}

template <typename T>
bool AllocatedVector<T>::RemoveUnordered(const T& elem) {
    if (Size == 0) [[unlikely]] {
        return false;
    }

    // Find the element to remove
    for (i32 i = 0; i < Size; i++) {
        if (_Data[i] == elem) {
            RemoveUnorderedAt(i);
            return true;
        }
    }

    return false;  // Element not found
}

template <typename T>
bool AllocatedVector<T>::RemoveUnorderedPred(const Function<bool(const T&)>& pred) {
    if (Size == 0) [[unlikely]] {
        return false;
    }

    // Find the element to remove
    for (i32 i = 0; i < Size; i++) {
        if (pred(_Data[i])) {
            RemoveUnorderedAt(i);
            return true;
        }
    }

    return false;  // Element not found
}

template <typename T>
void AllocatedVector<T>::RemoveUnorderedAt(i32 index) {
    ASSERT(index >= 0 && index < Size);

    // Move the last element to this position
    if (index != Size - 1) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            _Data[index] = _Data[Size - 1];
        } else {
            _Data[index] = std::move(_Data[Size - 1]);
            _Data[Size - 1].~T();
        }
    } else if constexpr (!std::is_trivially_copyable_v<T>) {
        // If removing the last element and it's non-trivial, call destructor
        _Data[index].~T();
    }

    Size--;
}

// DynArray ----------------------------------------------------------------------------------------

template <typename T>
void DynArray<T>::SetArena(Arena* arena) {
    ASSERT(arena != nullptr);

    // If it's the same arena, do nothing.
    if (_Arena == arena) {
        return;
    }
    _Arena = arena;

    // Need to transfer existing data into the new arena.
    T* new_base = ArenaPushArray<T>(_Arena, Cap).data();

    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(new_base, _Data, Size * sizeof(T));
    } else {
        // Move each element to the new location
        for (i32 i = 0; i < Size; i++) {
            new (new_base + i) T(std::move(_Data[i]));
            _Data[i].~T();
        }
    }
    _Data = new_base;
}

template <typename T>
T& DynArray<T>::operator[](i32 index) {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T>
const T& DynArray<T>::operator[](i32 index) const {
    ASSERT(index < Size);
    return _Data[index];
}

template <typename T>
T& DynArray<T>::Push(const T& value) {
    if (Cap == 0) [[unlikely]] {
        ASSERT(Size == 0);
        Cap = kDynArrayInitialCap;
        _Data = ArenaPushArray<T>(_Arena, Cap).data();
    }

    // Get more memory.
    if (Size == Cap) [[unlikely]] {
        Cap += Cap;
        T* new_base = ArenaPushArray<T>(_Arena, Cap).data();

        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(new_base, _Data, Size * sizeof(T));
        } else {
            // Move each element to the new location
            for (i32 i = 0; i < Size; i++) {
                new (new_base + i) T(std::move(_Data[i]));
                _Data[i].~T();
            }
        }
        _Data = new_base;
    }

    T* ptr = _Data + Size;
    if constexpr (std::is_trivially_copyable_v<T>) {
        *ptr = value;
    } else {
        new (ptr) T(std::move(value));
    }
    Size++;
    return *ptr;
}

template <typename T>
T DynArray<T>::Pop() {
    if (Size == 0) [[unlikely]] {
        return T{};
    }

    T& elem = _Data[Size - 1];
    Size--;

    if constexpr (std::is_trivially_copyable_v<T>) {
        return elem;
    } else {
        T result = std::move(elem);
        elem.~T();
        return result;
    }
}

template <typename T>
void DynArray<T>::Clear() {
    // If it's not trivially copyable, call the destructor for each element.
    if constexpr (!std::is_trivially_copyable_v<T>) {
        for (i32 i = 0; i < Size; i++) {
            _Data[i].~T();  // In-place destructor.
        }
    }

    Size = 0;
}

template <typename T>
void DynArray<T>::Reserve(i32 new_cap) {
    if (new_cap <= Cap) {
        return;  // Already have enough capacity
    }

    T* new_base = ArenaPushArray<T>(_Arena, new_cap).data();

    if (Size > 0) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(new_base, _Data, Size * sizeof(T));
        } else {
            // Move each element to the new location
            for (i32 i = 0; i < Size; i++) {
                new (new_base + i) T(std::move(_Data[i]));
                _Data[i].~T();
            }
        }
    }

    _Data = new_base;
    Cap = new_cap;
}

// Queue -------------------------------------------------------------------------------------------

template <typename T, i32 N>
void Queue<T, N>::Clear() {
    // If it's not trivially copyable, call the destructor for each element.
    if constexpr (!std::is_trivially_copyable_v<T>) {
        for (i32 i = 0; i < Size; i++) {
            i32 index = (_Front + i) % N;
            _Data[index].~T();
        }
    }

    _Front = 0;
    _Back = 0;
    Size = 0;
}

template <typename T, i32 N>
void Queue<T, N>::Push(const T& elem) {
    ASSERT(!IsFull());

    T* ptr = _Data + _Back;

    if constexpr (std::is_pod_v<T>) {
        *ptr = elem;
    } else {
        // For non POD things, we want to make sure the memory will not do weird things.
        std::memset(ptr, 0, sizeof(T));
        new (ptr) T(elem);  // Placement new.
    }

    _Back = (_Back + 1) % N;
    Size++;
}

template <typename T, i32 N>
T Queue<T, N>::Pop() {
    if (Size == 0) [[unlikely]] {
#ifdef HVN_BUILD_DEBUG
        if (!gRunningInTest) [[likely]] {
            ASSERT(ptr);
        }
#endif  // HVN_BUILD_DEBUG
        return T{};
    }

    T& elem = _Data[_Front];
    _Front = (_Front + 1) % N;
    Size--;

    if constexpr (std::is_trivially_copyable_v<T>) {
        return elem;
    } else {
        T result = std::move(elem);
        elem.~T();
        return result;
    }
}

template <typename T, i32 N>
T& Queue<T, N>::Front() {
    ASSERT(Size > 0);
    return _Data[_Front];
}

template <typename T, i32 N>
const T& Queue<T, N>::Front() const {
    ASSERT(Size > 0);
    return _Data[_Front];
}

template <typename T, i32 N>
T& Queue<T, N>::Back() {
    ASSERT(Size > 0);
    i32 back_index = (_Back - 1 + N) % N;
    return _Data[back_index];
}

template <typename T, i32 N>
const T& Queue<T, N>::Back() const {
    ASSERT(Size > 0);
    i32 back_index = (_Back - 1 + N) % N;
    return _Data[back_index];
}

}  // namespace kdk
