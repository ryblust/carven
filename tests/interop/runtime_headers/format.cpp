#include <carven/runtime/format.hpp>

namespace {

constexpr auto integer_format = std::format_string<const int&>("{}");
constexpr auto scalar_format = std::format_string<carven::runtime::String, const bool&>("{} {}");

} // namespace

static_assert(noexcept(carven::runtime::format_valid_utf8(integer_format, 7)));
static_assert(noexcept(carven::runtime::format_valid_utf8(scalar_format, U'我', true)));
static_assert(noexcept(
    carven::runtime::append_format(std::declval<carven::runtime::String&>(), integer_format, 7)
));
static_assert(noexcept(carven::runtime::append_format_valid_utf8(
    std::declval<carven::runtime::String&>(),
    scalar_format,
    U'我',
    true
)));

auto format_header_contract() noexcept -> bool {
    auto appended = carven::runtime::String::from_str("prefix:");
    carven::runtime::append_format(appended, "{:.2f}", 1.25);
    carven::runtime::append_format_valid_utf8(appended, " {:04x} {} {}", 42, U'我', true);
    return carven::runtime::format("{}", 7).as_str() == "7"
        && carven::runtime::format_valid_utf8("{:04x} {} {}", 42, U'我', true).as_str()
        == "002a 我 true"
        && appended.as_str() == "prefix:1.25 002a 我 true";
}
