#pragma once

#include <carven/runtime/collection/slice.hpp>

#include <array>
#include <cstddef>

namespace slice_observation {

template<typename Element, std::size_t Size>
auto same_backing(
    const std::array<Element, Size>& owner,
    carven::runtime::Slice<Element> view
) noexcept -> bool {
    return owner.data() == view.data();
}

} // namespace slice_observation
