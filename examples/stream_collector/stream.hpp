#pragma once

#include <istream>
#include <string>
#include <string_view>

namespace collector {

struct Stream final {
    std::istream& input;
    std::string line;
};

} // namespace collector

// The returned view is valid until the next read from this stream.
auto next_record(collector::Stream& stream) noexcept -> std::string_view;
