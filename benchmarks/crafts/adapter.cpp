#include <carven/api/BENCH_SIDE/api.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The script replaces BENCH_SIDE in the include path before compilation.
namespace api = carven::api::BENCH_SIDE::api;
#define JOIN_IMPL(a, b) a##b
#define JOIN(a, b) JOIN_IMPL(a, b)
#define NAME(suffix) JOIN(BENCH_SIDE, suffix)

extern "C" auto NAME(_invoke)(
    unsigned operation,
    unsigned set,
    const std::uint8_t* data,
    std::size_t size
) noexcept -> std::size_t {
    const auto bytes = carven::runtime::Slice<std::uint8_t>(std::span(data, size));
    const auto text = std::string_view(reinterpret_cast<const char*>(data), size);
    if (operation == 0) {
        if (set == 0) {
            return api::count_zero(bytes);
        }
        if (set == 1) {
            return api::count_space(bytes);
        }
        return api::count_digits(bytes);
    }
    if (operation == 1) {
        if (set == 0) {
            return api::find_zero(bytes);
        }
        if (set == 1) {
            return api::find_space(bytes);
        }
        return api::find_digits(bytes);
    }
    if (operation == 2) {
        if (set == 0) {
            return api::prefix_zero(bytes);
        }
        if (set == 1) {
            return api::prefix_space(bytes);
        }
        return api::prefix_digits(bytes);
    }
    if (operation == 3) {
        return api::utf_status(bytes);
    }
    if (operation == 4) {
        return api::json_text_status(text);
    }
    if (operation == 5) {
        return api::json_bytes_status(bytes);
    }
    const auto decoded = api::decode(text);
    const auto view = decoded.as_str();
    // Constant-time consumption keeps decode output alive without an extra traversal.
    return view.size() + (view.empty() ? 0 : static_cast<unsigned char>(view.back()) * 65536u);
}

extern "C" auto NAME(_decode_equals)(
    const std::uint8_t* data,
    std::size_t size,
    const char* expected,
    std::size_t expected_size
) noexcept -> bool {
    return api::decode(std::string_view(reinterpret_cast<const char*>(data), size)).as_str()
        == std::string_view(expected, expected_size);
}
