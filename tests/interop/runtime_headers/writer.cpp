#include <carven/runtime/writer.hpp>

static_assert(
    noexcept(carven::runtime::Writer(std::declval<carven::runtime::String&>(), 128, 128))
);
static_assert(noexcept(std::declval<carven::runtime::Writer&>().integer<16, true, true>(42, 8)));

auto writer_header_contract() noexcept -> bool {
    auto output = carven::runtime::String::from_str("prefix:");
    auto writer = carven::runtime::Writer(output, 11, 11);
    writer.integer<16, true, true>(42u, 8);
    writer.append("/");
    writer.integer<10, false, false>(-7, 0);
    return output.as_str() == "prefix:0000002A/-7";
}
