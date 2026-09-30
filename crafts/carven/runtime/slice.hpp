#pragma once

#include "index.hpp"

#include <array>
#include <cstddef>
#include <span>

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

    // A native subscript reports this position; generated code calls
    // checked_slice_index with the position of the Carven subscript.
    template<Integer Index>
    constexpr auto operator[](Index index) const noexcept -> const Element& {
        return storage[checked_index_offset(index, size(), SourceSite::native())];
    }

    constexpr auto slice(std::size_t start, std::size_t end, SourceSite site) const noexcept
        -> Slice {
        if (start > end || end > size()) {
            trap("slice range is out of bounds", site);
        }
        return Slice(storage.subspan(start, end - start));
    }

private:
    std::span<const Element> storage;
};

template<typename Element, Integer Index>
constexpr auto checked_slice_index(Slice<Element> slice, Index index, SourceSite site) noexcept
    -> const Element& {
    return slice.data()[checked_index_offset(index, slice.size(), site)];
}

template<typename Element, std::size_t Size>
constexpr auto as_slice(const std::array<Element, Size>& input) noexcept -> Slice<Element> {
    return Slice<Element>(std::span<const Element>(input));
}

} // namespace carven::runtime
