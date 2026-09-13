#pragma once

#include <array>
#include <cstddef>

#include <carven/runtime/slice.hpp>

namespace slice_observation {

template<typename Element, std::size_t Size>
auto same_backing(
    const std::array<Element, Size>& owner,
    carven::runtime::Slice<Element> view
) noexcept -> bool {
    return owner.data() == view.data();
}

}
