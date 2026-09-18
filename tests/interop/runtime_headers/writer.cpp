#include <carven/runtime/writer.hpp>

static_assert(
    noexcept(carven::runtime::Writer(std::declval<carven::runtime::String&>(), 128, 128))
);
static_assert(noexcept(std::declval<carven::runtime::Writer&>().integer<16, true, true>(42, 8)));

template<typename Type>
concept WriterInteger = requires (carven::runtime::Writer& writer, Type value) {
    writer.integer<10, false, false>(value, 0);
};

static_assert(WriterInteger<signed char> && WriterInteger<unsigned char>);
static_assert(WriterInteger<short> && WriterInteger<unsigned short>);
static_assert(WriterInteger<int> && WriterInteger<unsigned int>);
static_assert(WriterInteger<long> && WriterInteger<unsigned long>);
static_assert(WriterInteger<long long> && WriterInteger<unsigned long long>);
static_assert(!WriterInteger<bool> && !WriterInteger<char> && !WriterInteger<wchar_t>);
static_assert(!WriterInteger<char8_t> && !WriterInteger<char16_t> && !WriterInteger<char32_t>);
static_assert(!WriterInteger<float> && !WriterInteger<double>);

auto writer_header_contract() noexcept -> bool {
    auto output = carven::runtime::String::from_str("prefix:");
    auto writer = carven::runtime::Writer(output, 11, 11);
    writer.integer<16, true, true>(42u, 8);
    writer.append("/");
    writer.integer_dynamic_width<10, false, false>(-7, 0);
    return output.as_str() == "prefix:0000002A/-7";
}
