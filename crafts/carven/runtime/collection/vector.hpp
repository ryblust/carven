#pragma once

#include <carven/runtime/collection/index.hpp>
#include <carven/runtime/collection/slice.hpp>

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace carven::runtime {

// Allocated and retained arrays share their read representation. The high length
// bit marks immutable program-lifetime backing; copies construct independent owners.
template<typename Element>
class Vector final {
    static constexpr auto retained_bit = std::size_t(1)
        << (std::numeric_limits<std::size_t>::digits - 1);

    static constexpr auto length_mask = retained_bit - 1;

    const Element* storage = nullptr;
    std::size_t tagged_length = 0;
    std::size_t capacity_value = 0;

    constexpr auto retained() const noexcept -> bool { return (tagged_length & retained_bit) != 0; }

    // Write access is admitted only for owners, whose elements were allocated mutable.
    constexpr auto writable() noexcept -> Element* { return const_cast<Element*>(storage); }

    static constexpr auto maximum_capacity() noexcept -> std::size_t {
        constexpr auto allocation_limit = std::numeric_limits<std::size_t>::max() / sizeof(Element);
        return allocation_limit < length_mask ? allocation_limit : length_mask;
    }

    constexpr auto release() noexcept -> void {
        if (!retained()) {
            clear();
            if (storage != nullptr) {
                std::allocator<Element>().deallocate(writable(), capacity_value);
            }
        }
    }

    constexpr auto grow(std::size_t count) noexcept -> void {
        constexpr auto maximum = maximum_capacity();
        if (count > maximum - size()) {
            trap("Vector capacity is out of bounds", SourceSite::native());
        }
        const auto minimum = size() + count;
        if (minimum > capacity_value) {
            const auto expanded = capacity_value > maximum / 2 ? maximum : capacity_value * 2;
            reserve(minimum > expanded ? minimum : expanded);
        }
    }

public:
    constexpr Vector() noexcept = default;

    template<typename... Values>
        requires (sizeof...(Values) > 0 && (std::is_same_v<Values, Element> && ...))
    constexpr explicit Vector(Values... values) noexcept {
        reserve(sizeof...(Values));
        (push(std::move(values)), ...);
    }

    constexpr Vector(const Vector& other) noexcept
        : Vector(from_slice(slice(other))) {}

    constexpr Vector(Vector&& other) noexcept
        : storage(std::exchange(other.storage, nullptr)),
          tagged_length(std::exchange(other.tagged_length, 0)),
          capacity_value(std::exchange(other.capacity_value, 0)) {}

    constexpr auto operator=(const Vector& other) noexcept -> Vector& {
        if (this != &other) {
            *this = Vector(other);
        }
        return *this;
    }

    constexpr auto operator=(Vector&& other) noexcept -> Vector& {
        if (this != &other) {
            release();
            storage = std::exchange(other.storage, nullptr);
            tagged_length = std::exchange(other.tagged_length, 0);
            capacity_value = std::exchange(other.capacity_value, 0);
        }
        return *this;
    }

    constexpr ~Vector() noexcept { release(); }

    static constexpr auto from_slice(Slice<Element> values) noexcept -> Vector {
        auto result = Vector();
        result.append(values);
        return result;
    }

    static constexpr auto with_capacity(std::size_t minimum) noexcept -> Vector {
        auto result = Vector();
        result.reserve(minimum);
        return result;
    }

    // Generated immutable data owns the backing for the program lifetime.
    static constexpr auto from_static(Slice<Element> values, std::size_t capacity) noexcept
        -> Vector {
        auto result = Vector();
        result.storage = values.data();
        result.tagged_length = values.size() | retained_bit;
        result.capacity_value = capacity;
        return result;
    }

    constexpr auto size() const noexcept -> std::size_t { return tagged_length & length_mask; }

    constexpr auto empty() const noexcept -> bool { return size() == 0; }

    constexpr auto capacity() const noexcept -> std::size_t { return capacity_value; }

    constexpr auto reserve(std::size_t minimum) noexcept -> void {
        if (minimum <= capacity_value) {
            return;
        }
        if (minimum > maximum_capacity()) {
            trap("Vector capacity is out of bounds", SourceSite::native());
        }
        auto* replacement = std::allocator<Element>().allocate(minimum);
        const auto count = size();
        for (auto index = std::size_t(0); index < count; ++index) {
            std::construct_at(replacement + index, std::move(writable()[index]));
        }
        for (auto index = count; index != 0; --index) {
            std::destroy_at(writable() + index - 1);
        }
        if (storage != nullptr) {
            std::allocator<Element>().deallocate(writable(), capacity_value);
        }
        storage = replacement;
        capacity_value = minimum;
    }

    constexpr auto push(Element value) noexcept -> void {
        grow(1);
        std::construct_at(writable() + tagged_length, std::move(value));
        ++tagged_length;
    }

    // The source must not borrow this vector's storage.
    constexpr auto append(Slice<Element> values) noexcept -> void {
        if (values.empty()) {
            return;
        }
        grow(values.size());
        if constexpr (std::is_trivially_copyable_v<Element>
                      && std::is_trivially_copy_constructible_v<Element>) {
            if (!std::is_constant_evaluated()) {
                std::memcpy(
                    writable() + tagged_length,
                    values.data(),
                    values.size() * sizeof(Element)
                );
                tagged_length += values.size();
                return;
            }
        }
        for (const auto& value : values) {
            std::construct_at(writable() + tagged_length, value);
            ++tagged_length;
        }
    }

    constexpr auto clear() noexcept -> void {
        if constexpr (std::is_trivially_destructible_v<Element>) {
            if (!std::is_constant_evaluated()) {
                tagged_length = 0;
                return;
            }
        }
        while (tagged_length != 0) {
            --tagged_length;
            std::destroy_at(writable() + tagged_length);
        }
    }

    template<Integer Index>
    constexpr auto remove(Index index, SourceSite site) noexcept -> Element {
        const auto offset = checked_index_offset(index, size(), site);
        // The returned owner needs implicit move when Element is move-only.
        auto result = std::move(writable()[offset]); // NOLINT(misc-const-correctness)
        for (auto next = offset + 1; next < tagged_length; ++next) {
            writable()[next - 1] = std::move(writable()[next]);
        }
        --tagged_length;
        std::destroy_at(writable() + tagged_length);
        return result;
    }

    template<Integer Index>
    constexpr auto at(Index index, SourceSite site) noexcept -> Element& {
        return writable()[checked_index_offset(index, size(), site)];
    }

    template<Integer Index>
    constexpr auto at(Index index, SourceSite site) const noexcept -> const Element& {
        return storage[checked_index_offset(index, size(), site)];
    }

    constexpr auto begin() noexcept -> Element* { return writable(); }

    constexpr auto end() noexcept -> Element* { return empty() ? begin() : begin() + size(); }

    constexpr auto begin() const noexcept -> const Element* { return storage; }

    constexpr auto end() const noexcept -> const Element* {
        return empty() ? begin() : begin() + size();
    }
};

template<typename Element>
constexpr auto slice(const Vector<Element>& values) noexcept -> Slice<Element> {
    return Slice<Element>(std::span<const Element>(values.begin(), values.size()));
}

template<typename Element, Integer Index>
constexpr auto checked_vector_index(Vector<Element>& values, Index index, SourceSite site) noexcept
    -> Element& {
    return values.at(index, site);
}

template<typename Element, Integer Index>
constexpr auto checked_vector_index(
    const Vector<Element>& values,
    Index index,
    SourceSite site
) noexcept -> const Element& {
    return values.at(index, site);
}

} // namespace carven::runtime
