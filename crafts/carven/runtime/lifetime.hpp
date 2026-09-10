#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace carven::runtime {

// A reference borrows its owner; a value is constructed once in this storage.
// The caller retains any borrowed owner through the source cleanup boundary.
template<typename Result>
    requires (!std::is_void_v<Result>)
class DeferredResult final {
    using Object = std::remove_reference_t<Result>;
    static constexpr bool borrowed = std::is_reference_v<Result>;

    struct OwnedStorage final {
        alignas(Object) std::byte bytes[sizeof(Object)];
    };

    struct BorrowedStorage final {};

    using Storage = std::conditional_t<borrowed, BorrowedStorage, OwnedStorage>;

public:
    constexpr DeferredResult() noexcept = default;

    constexpr ~DeferredResult() noexcept {
        if constexpr (!borrowed) {
            if (value != nullptr) {
                std::destroy_at(value);
            }
        }
    }

    DeferredResult(const DeferredResult&) = delete;
    DeferredResult(DeferredResult&&) = delete;
    auto operator=(const DeferredResult&) -> DeferredResult& = delete;
    auto operator=(DeferredResult&&) -> DeferredResult& = delete;

    template<typename Factory>
    constexpr auto initialize(Factory&& factory) noexcept -> void {
        // Scalar prvalues discard top-level cv; class and reference results retain it.
        using Expected =
            std::conditional_t<std::is_scalar_v<Result>, std::remove_cv_t<Result>, Result>;
        static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, Expected>);
        assert(value == nullptr);
        if constexpr (borrowed) {
            auto&& reference = std::forward<Factory>(factory)();
            value = std::addressof(reference);
        } else {
            value =
                ::new (static_cast<void*>(storage.bytes)) Result(std::forward<Factory>(factory)());
        }
    }

    constexpr auto operator*() noexcept -> Object& { return *value; }

    constexpr auto operator*() const noexcept -> decltype(auto) {
        if constexpr (borrowed) {
            return *value;
        } else {
            return static_cast<const Object&>(*value);
        }
    }

private:
    [[no_unique_address]] Storage storage;
    Object* value = nullptr;
};

} // namespace carven::runtime
