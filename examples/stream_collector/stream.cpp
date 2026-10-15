#include "stream.hpp"

auto next_record(collector::Stream& stream) noexcept -> std::string_view {
    while (std::getline(stream.input, stream.line)) {
        if (!stream.line.empty()) {
            return stream.line;
        }
    }
    return {};
}
