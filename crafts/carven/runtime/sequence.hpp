#pragma once

#include "index.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace carven::runtime {

// All elements are initialized values. Structural mutation invalidates element
// references; the source-language ownership check protects those borrows.
template<typename Element>
class Sequence final {
    struct Cell final {
        Element value;
    };

    template<bool Constant>
    class Iterator final {
        using Position = std::conditional_t<
            Constant,
            typename std::vector<Cell>::const_iterator,
            typename std::vector<Cell>::iterator>;
        Position position;

    public:
        explicit Iterator(Position position) noexcept
            : position(position) {}

        auto operator*() const noexcept -> decltype(auto) { return (position->value); }

        auto operator++() noexcept -> Iterator& {
            ++position;
            return *this;
        }

        auto operator==(const Iterator&) const noexcept -> bool = default;
    };

    std::vector<Cell> storage;

public:
    Sequence() noexcept = default;
    Sequence(const Sequence&) noexcept = default;
    Sequence(Sequence&&) noexcept = default;
    auto operator=(const Sequence&) noexcept -> Sequence& = default;
    auto operator=(Sequence&&) noexcept -> Sequence& = default;
    ~Sequence() = default;

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto empty() const noexcept -> bool { return storage.empty(); }

    auto push(Element value) noexcept -> void { storage.push_back(Cell {std::move(value)}); }

    auto clear() noexcept -> void { storage.clear(); }

    template<Integer Index>
    auto remove(Index index, SourceSite site) noexcept -> void {
        const auto offset = checked_index_offset(index, storage.size(), site);
        storage.erase(storage.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    template<Integer Index>
    auto at(Index index, SourceSite site) noexcept -> Element& {
        return storage[checked_index_offset(index, storage.size(), site)].value;
    }

    template<Integer Index>
    auto at(Index index, SourceSite site) const noexcept -> const Element& {
        return storage[checked_index_offset(index, storage.size(), site)].value;
    }

    auto begin() noexcept { return Iterator<false>(storage.begin()); }

    auto end() noexcept { return Iterator<false>(storage.end()); }

    auto begin() const noexcept { return Iterator<true>(storage.begin()); }

    auto end() const noexcept { return Iterator<true>(storage.end()); }
};

template<typename Element, Integer Index>
auto checked_sequence_index(Sequence<Element>& sequence, Index index, SourceSite site) noexcept
    -> Element& {
    return sequence.at(index, site);
}

template<typename Element, Integer Index>
auto checked_sequence_index(
    const Sequence<Element>& sequence,
    Index index,
    SourceSite site
) noexcept -> const Element& {
    return sequence.at(index, site);
}

} // namespace carven::runtime
