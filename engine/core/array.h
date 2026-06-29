#pragma once

#include <engine/core/algorithm.h>
#include <engine/core/defines.h>

#include <span>

namespace kdk {

// Array -------------------------------------------------------------------------------------------
//
// Separated into its own array because it is such a fundamental building block.
// Could be moved into defines honestly.

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

// IMPLEMENTATION ----------------------------------------------------------------------------------

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

}  // namespace kdk
