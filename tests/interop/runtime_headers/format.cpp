#include <carven/runtime/format.hpp>

auto format_header_contract() noexcept -> bool {
    return carven::runtime::format("{}", 7).as_str() == "7";
}
