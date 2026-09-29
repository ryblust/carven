module;
#include <csignal>

module carven:test.harness.contracts;

import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

enum class ByteEnum : std::uint8_t { One = 1, Two = 2 };

const ct::Suite contracts([] static noexcept {
    ct::test("Test framework: success", [] static noexcept {
        ct::expect_equal(std::string("same"), std::string_view("same"))
            .note("context:", [] static noexcept {
                std::abort();
                return "unexpected failure message";
            });
        const auto first_text = std::array {'s', 'a', 'm', 'e', '\0'};
        const auto second_text = std::array {'s', 'a', 'm', 'e', '\0'};
        ct::expect_equal(first_text.data(), second_text.data());
        ct::expect_equal(first_text.data(), "same");
        ct::expect_less("a", "b");
        const char* null_text = nullptr;
        ct::expect_equal(null_text, null_text);
        ct::expect_not_equal(null_text, "");
        ct::expect_range_equal(std::array<int, 0> {}, std::array<int, 0> {});
        struct Case final {
            std::string_view name;
            int value;
        };
        const auto cases = std::to_array<Case>({
            {.name = "first", .value = 1},
            {.name = "second", .value = 2},
        });
        ct::each(cases, &Case::name, [](const Case& item) static noexcept {
            ct::expect_greater(item.value, 0);
        });
    });
    ct::test("Test framework failure: first fails", [] static noexcept {
        struct Case final {
            std::string_view name;
            bool fail_assertions;
        };
        const auto cases = std::to_array<Case>({
            {.name = "failing input", .fail_assertions = true},
            {.name = "later input", .fail_assertions = false},
        });
        ct::each(cases, &Case::name, [](const Case& item) static noexcept {
            if (!item.fail_assertions) {
                ct::expect(true);
                std::cerr << "LATER INPUT EXECUTED\n";
                return;
            }
            ct::expect(false).note("input rejected", [] static noexcept { return "details"; });
            ct::expect_equal(ByteEnum::One, ByteEnum::Two);
            ct::expect_less(9, 7);
            const char* null_text = nullptr;
            ct::expect_equal(null_text, "");
            ct::expect_range_equal(std::array {1, 2}, std::array {1, 3, 4});
            if (!ct::expect_equal(std::string("line\n\0\xff", 7), std::string_view("你好\t\"\\"))) {
                return;
            }
            std::cerr << "UNSAFE CONTINUATION\n";
        });
    });
    ct::test("Test framework failure: second executes", [] static noexcept {
        ct::expect(true);
        std::cerr << "LATER CASE EXECUTED\n";
    });
    ct::test("Test framework: fatal premise", [] static noexcept {
        if (std::signal(SIGABRT, [](int) static noexcept { std::_Exit(73); }) == SIG_ERR) {
            std::_Exit(74);
        }
        ct::require_equal(7, 9).note("fatal", [] static noexcept { return "context"; });
        std::cerr << "UNSAFE CONTINUATION\n";
    });
    ct::test("Test framework: no assertions", [] static noexcept {});
    ct::test("Test framework: empty table", [] static noexcept {
        const auto cases = std::array<int, 0> {};
        ct::each(
            cases,
            [](int) static noexcept -> std::string_view { return "unreachable"; },
            [](int) static noexcept { ct::expect(true); }
        );
    });
});

} // namespace

extern "C++" auto main(int argc, const char* const* argv) noexcept -> int {
    if (argc == 2 && std::string_view(argv[1]) == "--duplicate-declaration") {
        ct::Suite([] static noexcept {
            ct::test("Duplicate", [] static noexcept { std::abort(); });
            ct::test("Duplicate", [] static noexcept { std::abort(); });
        });
        return ct::run(1, argv);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--invalid-declaration") {
        ct::Suite([] static noexcept { ct::test("", nullptr); });
        return ct::run(1, argv);
    }
    return ct::run(argc, argv);
}
