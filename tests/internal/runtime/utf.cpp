module;
#include <carven/runtime/utf.hpp>

module carven:test.internal.runtime.utf;

import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Runtime: UTF-8 ingress validator rejects malformed scalar encodings",
        [] static noexcept {
            const auto rejects = [](std::string_view bytes) static noexcept {
                return !carven::runtime::utf8_is_valid(bytes);
            };
            ct::expect(carven::runtime::utf8_is_valid("plain"));
            ct::expect(carven::runtime::utf8_is_valid("\xc3\xa9\xe4\xbd\xa0"));
            ct::expect(rejects(std::string_view("\xc2", 1)));
            ct::expect(rejects(std::string_view("\xc0\x80", 2)));
            ct::expect(rejects(std::string_view("\xed\xa0\x80", 3)));
            ct::expect(rejects(std::string_view("\xf4\x90\x80\x80", 4)));
            ct::expect(rejects(std::string_view("\xe2\x28\xa1", 3)));
        }
    );

    ct::test("Runtime: UTF primitives encode and decode scalar boundaries", [] static noexcept {
        const auto cases = std::array {
            std::pair {U'\0', std::string_view("\0", 1)},
            std::pair {U'\x7f', std::string_view("\x7f")},
            std::pair {U'\u0080', std::string_view("\xc2\x80")},
            std::pair {U'\u07ff', std::string_view("\xdf\xbf")},
            std::pair {U'\u0800', std::string_view("\xe0\xa0\x80")},
            std::pair {U'\uffff', std::string_view("\xef\xbf\xbf")},
            std::pair {U'\U00010000', std::string_view("\xf0\x90\x80\x80")},
            std::pair {U'\U0010ffff', std::string_view("\xf4\x8f\xbf\xbf")},
        };
        ct::each(
            cases,
            [](const auto& item) static noexcept -> std::string {
                return std::format("U+{:04X}", static_cast<std::uint32_t>(item.first));
            },
            [](const auto& item) static noexcept {
                const auto& [scalar, bytes] = item;
                const auto encoded = carven::runtime::encode_valid_utf8(scalar);
                ct::expect_equal(std::string_view(encoded.bytes.data(), encoded.width), bytes);
                const auto decoded =
                    carven::runtime::decode_valid_utf8(std::span(bytes.data(), bytes.size()));
                ct::expect_equal(decoded.scalar, scalar);
                ct::expect_equal(decoded.width, bytes.size());
                ct::expect(carven::runtime::utf8_is_valid(bytes));
            }
        );
    });
});

} // namespace
