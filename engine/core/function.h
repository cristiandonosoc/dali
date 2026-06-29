#pragma once

#include <engine/core/defines.h>

namespace kdk {

namespace function_private {

template <typename T>
inline constexpr bool IsFunctionInstance = false;

template <typename F>
struct Functor {
    F lambda;

    static Functor* Cast(void* ptr) noexcept { return (Functor*)(ptr); }

    static const Functor* Cast(const void* storage) noexcept { return (const Functor*)(storage); }

    template <typename R, typename... Args>
    static R Call(const void* self, Args&&... args) {
        return Cast(self)->lambda(std::forward<Args>(args)...);
    }

    static void destroy(void* self) { Cast(self)->~Functor(); }

    static void Move(void* to, void* from) {
        new (to) Functor{(Functor&&)(*Cast(from))};
        destroy(from);
    }

    static void Copy(void* to, const void* from) {
        new (to) Functor{(const Functor&)(*Cast(from))};
    }
};

template <typename T, i32 N>
struct ArrayType {
    T _Data[N];
};

}  // namespace function_private

template <typename, i32 = 4>
struct Function;

// A simple std::function alternative that never allocates.
// It contains enough space to store a lambda that captures N pointer-sized objects.
//
// The lambda's destructor and copy/move constructors are never used, which makes it very cheap to
// pass around.
//
// This also implies that only trivial types, such as pointers and references, can safely be
// captured.
template <typename R, typename... Args, i32 N>
struct Function<R(Args...), N> {
    constexpr Function() noexcept = default;
    constexpr ~Function() = default;
    constexpr Function(Function&&) noexcept = default;
    constexpr Function(const Function&) noexcept = default;
    constexpr Function& operator=(Function&&) noexcept = default;
    constexpr Function& operator=(const Function&) noexcept = default;

    constexpr Function(std::nullptr_t) noexcept : Function{} {}

    template <typename F>
        requires(!function_private::IsFunctionInstance<std::remove_cvref_t<F>>)
    Function(F&& func) : Function{Create(std::forward<F>(func))} {}

    template <typename F>
    Function& operator=(F&& func) noexcept {
        return *this = Function{std::forward<F>(func)};
    }

    template <i32 M>
        requires(M < N)
    Function(const Function<R(Args...), M>& other) noexcept : Call{other.Call} {
        std::memcpy(Storage, &other.Storage, sizeof(other.Storage));
    }

    R operator()(Args... args) const { return Call(&Storage, std::forward<Args>(args)...); }

    constexpr bool IsValid() const noexcept { return Call != nullptr; }
    explicit constexpr operator bool() const noexcept { return IsValid(); }

   private:
    template <typename F>
    static Function Create(F&& func) {
        using Functor = function_private::Functor<std::remove_cvref_t<F>>;

        static_assert(std::is_trivially_destructible_v<Functor>,
                      "Lambda captures must be trivially destructible (no owning types like "
                      "std::string, std::vector, etc. — only pointers and references).");
        static_assert(sizeof(Functor) <= sizeof(Dummy),
                      "Lambda is too large for this Function's inline storage. "
                      "Reduce the number of captures, or increase N in Function<Sig, N>. "
                      "Hint: sizeof(lambda captures) must be <= N * sizeof(void*).");
        static_assert(alignof(Functor) <= alignof(Dummy),
                      "Lambda has stricter alignment than this Function's inline storage supports.");

        Function f;
        new (&f.Storage) Functor{std::forward<F>(func)};
        f.Call = Functor::template Call<R, Args...>;
        return f;
    }

    template <typename, i32>
    friend struct Function;

    using Dummy = function_private::Functor<
        decltype([x = std::declval<function_private::ArrayType<void*, N>>()](Args...) {})>;

    alignas(Dummy) u8 Storage[sizeof(Dummy)];
    R (*Call)(const void*, Args&&...) = nullptr;
};

namespace function_private {

template <typename Sig, i32 N>
inline constexpr bool IsFunctionInstance<Function<Sig, N>> = true;

}  // namespace function_private

}  // namespace kdk
