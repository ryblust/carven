#include <carven/runtime/number_text.hpp>

auto number_text_header_contract() noexcept -> bool {
    return carven::runtime::parse_i64("-7") == -7
        && carven::runtime::parse_u64("7") == 7
        && carven::runtime::parse_f64("1.25") == 1.25;
}
