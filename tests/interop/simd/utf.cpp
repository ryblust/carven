#include "provider.hpp"

#include <carven/runtime/utf.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

namespace rt = carven::runtime;
using namespace std::string_view_literals;

struct Encoding final {
    std::string_view name;
    std::string_view bytes;
    bool valid;
};

constexpr auto encodings = std::array {
    Encoding {.name = "NUL", .bytes = "\0"sv, .valid = true},
    Encoding {.name = "last ASCII", .bytes = "\x7f"sv, .valid = true},
    Encoding {.name = "first two-byte scalar", .bytes = "\xc2\x80"sv, .valid = true},
    Encoding {.name = "last two-byte scalar", .bytes = "\xdf\xbf"sv, .valid = true},
    Encoding {.name = "first three-byte scalar", .bytes = "\xe0\xa0\x80"sv, .valid = true},
    Encoding {.name = "before surrogates", .bytes = "\xed\x9f\xbf"sv, .valid = true},
    Encoding {.name = "after surrogates", .bytes = "\xee\x80\x80"sv, .valid = true},
    Encoding {.name = "last three-byte scalar", .bytes = "\xef\xbf\xbf"sv, .valid = true},
    Encoding {.name = "first four-byte scalar", .bytes = "\xf0\x90\x80\x80"sv, .valid = true},
    Encoding {.name = "last scalar", .bytes = "\xf4\x8f\xbf\xbf"sv, .valid = true},
    Encoding {.name = "unexpected continuation", .bytes = "\x80"sv, .valid = false},
    Encoding {.name = "last continuation", .bytes = "\xbf"sv, .valid = false},
    Encoding {.name = "overlong two-byte", .bytes = "\xc0\x80"sv, .valid = false},
    Encoding {.name = "forbidden C1", .bytes = "\xc1\xbf"sv, .valid = false},
    Encoding {.name = "overlong three-byte", .bytes = "\xe0\x9f\xbf"sv, .valid = false},
    Encoding {.name = "surrogate", .bytes = "\xed\xa0\x80"sv, .valid = false},
    Encoding {.name = "last surrogate", .bytes = "\xed\xbf\xbf"sv, .valid = false},
    Encoding {.name = "overlong four-byte", .bytes = "\xf0\x8f\xbf\xbf"sv, .valid = false},
    Encoding {.name = "above Unicode", .bytes = "\xf4\x90\x80\x80"sv, .valid = false},
    Encoding {.name = "forbidden F5", .bytes = "\xf5\x80\x80\x80"sv, .valid = false},
    Encoding {.name = "forbidden FF", .bytes = "\xff"sv, .valid = false},
    Encoding {.name = "missing continuation", .bytes = "\xc2"sv, .valid = false},
    Encoding {.name = "missing two continuations", .bytes = "\xe1"sv, .valid = false},
    Encoding {.name = "missing last continuation", .bytes = "\xe1\x80"sv, .valid = false},
    Encoding {.name = "missing three continuations", .bytes = "\xf1"sv, .valid = false},
    Encoding {.name = "four-byte partial pair", .bytes = "\xf1\x80"sv, .valid = false},
    Encoding {.name = "four-byte partial triple", .bytes = "\xf1\x80\x80"sv, .valid = false},
    Encoding {.name = "ASCII in scalar", .bytes = "\xe1\x80\x7f"sv, .valid = false},
    Encoding {.name = "nested lead", .bytes = "\xf1\xc2\x80\x80"sv, .valid = false},
};

constexpr auto constant_encodings() noexcept -> bool {
    for (const auto& input : encodings) {
        auto storage = std::array<char, 96>();
        storage.fill('a');
        for (auto i = std::size_t {0}; i < input.bytes.size(); ++i) {
            storage[63 + i] = input.bytes[i];
        }
        if (rt::utf8_is_valid(std::string_view(storage.data(), storage.size())) != input.valid) {
            return false;
        }
    }
    return true;
}

static_assert(constant_encodings());
static_assert(rt::utf8_is_valid({}));

} // namespace

auto simd_utf8_encodings() noexcept -> bool {
    for (const auto& input : encodings) {
        // Every lane placement, block transition, and scalar-tail position.
        for (auto offset = std::size_t {0}; offset <= 96; ++offset) {
            auto text = std::string(offset, 'a');
            text += input.bytes;
            for (const auto suffix : {0, 64}) {
                auto padded = text;
                padded.append(static_cast<std::size_t>(suffix), 'a');
                if (rt::utf8_is_valid(padded) != input.valid) {
                    std::fprintf(
                        stderr,
                        "UTF-8: %.*s at %zu with suffix %d\n",
                        static_cast<int>(input.name.size()),
                        input.name.data(),
                        offset,
                        suffix
                    );
                    return false;
                }
            }
        }
    }
    // Consecutive multibyte blocks must carry their own context, not ASCII fill.
    auto text = std::string();
    for (auto i = 0; i < 96; ++i) {
        text += "\xc2\xa2\xe4\xbd\xa0\xf0\x9f\x98\x80";
        if (!rt::utf8_is_valid(text)) {
            return false;
        }
    }
    text.pop_back();
    return !rt::utf8_is_valid(text);
}

auto simd_utf8_guarded() noexcept -> bool {
#if defined(__unix__) || defined(__APPLE__)
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 128) {
        return false;
    }
    const auto page = static_cast<std::size_t>(page_size);
    auto* allocation = mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (allocation == MAP_FAILED) {
        return false;
    }
    auto* bytes = static_cast<char*>(allocation) + page;
    if (mprotect(bytes, page, PROT_READ | PROT_WRITE) != 0) {
        static_cast<void>(munmap(allocation, page * 3));
        return false;
    }
    auto valid = true;
    for (auto length = std::size_t {0}; length <= 128; ++length) {
        for (const auto at_end : {false, true}) {
            auto* start = at_end ? bytes + page - length : bytes;
            std::memset(start, 'a', length);
            valid &= rt::utf8_is_valid(std::string_view(start, length));
            for (const auto& input : encodings) {
                if (input.bytes.size() > length) {
                    continue;
                }
                auto* suffix = start + length - input.bytes.size();
                std::memcpy(suffix, input.bytes.data(), input.bytes.size());
                valid &= rt::utf8_is_valid(std::string_view(start, length)) == input.valid;
                std::memset(suffix, 'a', input.bytes.size());
            }
        }
    }
    return munmap(allocation, page * 3) == 0 && valid;
#else
    return true;
#endif
}
