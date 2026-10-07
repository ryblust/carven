#include <carven/std/number/parse.hpp>

auto number_parse_header_contract() noexcept -> bool {
    return carven::number::parse_i64("-7") == -7
        && carven::number::parse_u64("7") == 7
        && carven::number::parse_f64("1.25") == 1.25;
}
