#pragma once

#include "numeric.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <span>
#include <type_traits>

namespace carven::runtime {

// The provider retains live, unchanged backing storage for every use of the view.
template<typename Element>
class Slice final {
public:
    constexpr Slice() noexcept = default;

    constexpr explicit Slice(std::span<const Element> input) noexcept
        : storage(input) {}

    constexpr auto data() const noexcept -> const Element* { return storage.data(); }

    constexpr auto size() const noexcept -> std::size_t { return storage.size(); }

    constexpr auto empty() const noexcept -> bool { return storage.empty(); }

    constexpr auto begin() const noexcept { return storage.begin(); }

    constexpr auto end() const noexcept { return storage.end(); }

    template<Integer Index>
    constexpr auto operator[](Index index) const noexcept -> const Element& {
        if constexpr (std::is_signed_v<Index>) {
            if (index < 0) {
                std::abort();
            }
        }
        if (static_cast<std::make_unsigned_t<Index>>(index) >= size()) {
            std::abort();
        }
        return storage[static_cast<std::size_t>(index)];
    }

    constexpr auto slice(std::size_t start, std::size_t end) const noexcept -> Slice {
        if (start > end || end > size()) {
            std::abort();
        }
        return Slice(storage.subspan(start, end - start));
    }

private:
    std::span<const Element> storage;
};

template<typename Element, std::size_t Size>
constexpr auto as_slice(const std::array<Element, Size>& input) noexcept -> Slice<Element> {
    return Slice<Element>(std::span<const Element>(input));
}

} // namespace carven::runtime
