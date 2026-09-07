#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

namespace carven::runtime {

template<typename Value>
class DeferredStorage final {
public:
    constexpr DeferredStorage() noexcept {}
    constexpr ~DeferredStorage() noexcept {
        if (value != nullptr) {
            std::destroy_at(value);
        }
    }

    DeferredStorage(const DeferredStorage&) = delete;
    DeferredStorage(DeferredStorage&&) = delete;
    auto operator=(const DeferredStorage&) -> DeferredStorage& = delete;
    auto operator=(DeferredStorage&&) -> DeferredStorage& = delete;

    // A source evaluation initializes its storage once per scope activation.
    template<typename Factory>
    constexpr auto initialize(Factory&& factory) noexcept -> void {
        assert(value == nullptr);
        value = std::forward<Factory>(factory)(static_cast<void*>(storage));
    }

    constexpr auto operator*() noexcept -> Value& { return *value; }
    constexpr auto operator*() const noexcept -> const Value& { return *value; }

private:
    alignas(Value) std::byte storage[sizeof(Value)];
    Value* value = nullptr;
};

} // namespace carven::runtime
