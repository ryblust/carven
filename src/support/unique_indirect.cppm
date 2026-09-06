module carven:support.unique_indirect;

import std;

template<typename T>
class UniqueIndirect final {
public:
    template<typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, T> && std::is_constructible_v<T, Value&&>
    explicit constexpr UniqueIndirect(Value&& value) noexcept
        : pointer(std::make_unique<T>(std::forward<Value>(value))) {}

    constexpr UniqueIndirect(const UniqueIndirect&) = delete;
    constexpr UniqueIndirect(UniqueIndirect&&) = default;
    constexpr ~UniqueIndirect() = default;

    constexpr auto operator=(const UniqueIndirect&) -> UniqueIndirect& = delete;
    constexpr auto operator=(UniqueIndirect&&) -> UniqueIndirect& = default;

    constexpr auto operator*() & noexcept -> T& {
        require_live();
        return *pointer;
    }

    constexpr auto operator*() const& noexcept -> const T& {
        require_live();
        return *pointer;
    }

    constexpr auto operator*() && noexcept -> T&& {
        require_live();
        return std::move(*pointer);
    }

    constexpr auto operator*() const&& noexcept -> const T&& {
        require_live();
        return std::move(*pointer);
    }

    constexpr auto operator->() noexcept -> T* {
        require_live();
        return pointer.get();
    }

    constexpr auto operator->() const noexcept -> const T* {
        require_live();
        return pointer.get();
    }

private:
    constexpr auto require_live() const noexcept -> void {
        if (pointer == nullptr) {
            std::terminate();
        }
    }

    std::unique_ptr<T> pointer;
};

template<typename T>
UniqueIndirect(T&&) -> UniqueIndirect<std::remove_cvref_t<T>>;
