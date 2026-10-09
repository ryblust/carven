#include <carven/runtime/format_writer.hpp>

static_assert(
    noexcept(carven::runtime::FormatWriter(std::declval<carven::runtime::String&>(), 128, 128))
);
static_assert(
    noexcept(std::declval<carven::runtime::FormatWriter&>().integer<16, true, true>(42, 8))
);

template<typename Type>
concept FormatWriterInteger = requires (carven::runtime::FormatWriter& writer, Type value) {
    writer.integer<10, false, false>(value, 0);
};

static_assert(FormatWriterInteger<signed char> && FormatWriterInteger<unsigned char>);
static_assert(FormatWriterInteger<short> && FormatWriterInteger<unsigned short>);
static_assert(FormatWriterInteger<int> && FormatWriterInteger<unsigned int>);
static_assert(FormatWriterInteger<long> && FormatWriterInteger<unsigned long>);
static_assert(FormatWriterInteger<long long> && FormatWriterInteger<unsigned long long>);
static_assert(
    !FormatWriterInteger<bool> && !FormatWriterInteger<char> && !FormatWriterInteger<wchar_t>
);
static_assert(
    !FormatWriterInteger<char8_t>
    && !FormatWriterInteger<char16_t>
    && !FormatWriterInteger<char32_t>
);
static_assert(!FormatWriterInteger<float> && !FormatWriterInteger<double>);

auto format_writer_header_contract() noexcept -> bool {
    auto output = carven::runtime::String::from_str("prefix:");
    auto writer = carven::runtime::FormatWriter(output, 11, 11);
    writer.integer<16, true, true>(42u, 8);
    writer.append("/");
    writer.integer_dynamic_width<10, false, false>(-7, 0);
    return output.as_str() == "prefix:0000002A/-7";
}
