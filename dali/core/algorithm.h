#pragma once

#include <dali/core/defines.h>

#include <algorithm>

namespace kdk {

void Sort(auto begin, auto end) { std::sort(begin, end); }

template <typename PREDICATE>
void SortPred(auto begin, auto end, const PREDICATE& pred) {
    std::sort(begin, end, pred);
}

template <typename T>
std::pair<i32, T*> Find(auto begin, auto end, const T& elem) {
    for (auto it = begin; it != end; ++it) {
        if (*it == elem) {
            i32 distance = (i32)(std::distance(begin, it));
            T* ptr = (T*)&(*it);
            return {distance, ptr};
        }
    }

    return {NONE, nullptr};
}

template <typename T, typename PREDICATE>
std::pair<i32, T*> FindPred(auto begin, auto end, const PREDICATE& pred) {
    for (auto it = begin; it != end; ++it) {
        if (pred(*it)) {
            i32 distance = (i32)(std::distance(begin, it));
            T* ptr = (T*)&(*it);
            return {distance, ptr};
        }
    }

    return {NONE, nullptr};
}

}  // namespace kdk
