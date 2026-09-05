module;
#include <memory>

export module carven:support.unique_indirect;

import std;

template<typename T>
class UniqueIndirect final {
public:
    template<typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, T>
    explicit UniqueIndirect(Value&& value)
        : pointer(std::make_unique<T>(std::forward<Value>(value))) {}

    UniqueIndirect(const UniqueIndirect&) = delete;
    UniqueIndirect(UniqueIndirect&&) noexcept = default;
    ~UniqueIndirect() = default;

    auto operator=(const UniqueIndirect&) -> UniqueIndirect& = delete;
    auto operator=(UniqueIndirect&&) noexcept -> UniqueIndirect& = default;

    auto operator*() noexcept -> T& {
        require_live();
        return *pointer;
    }

    auto operator*() const noexcept -> const T& {
        require_live();
        return *pointer;
    }

    auto operator->() noexcept -> T* {
        require_live();
        return pointer.get();
    }

    auto operator->() const noexcept -> const T* {
        require_live();
        return pointer.get();
    }

private:
    auto require_live() const noexcept -> void {
        if (pointer == nullptr) {
            std::terminate();
        }
    }

    std::unique_ptr<T> pointer;
};

template<typename T>
UniqueIndirect(T&&) -> UniqueIndirect<std::remove_cvref_t<T>>;
