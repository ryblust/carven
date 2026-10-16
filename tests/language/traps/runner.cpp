#include <carven/api/tests/language/traps/operations.hpp>

#include <carven/generated/carven-test-runner.hpp>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace {

namespace api = carven::api::tests::language::traps::operations;

struct Scenario final {
    std::string_view name;
    void (*action)() noexcept;
};

} // namespace

// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) noexcept -> int {
    if (argc != 2) {
        return 1;
    }
    // Writes before a trap must remain observable when abort skips stream flushing.
    if (std::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
        return 1;
    }
    const auto scenarios = std::array {
        Scenario {"remainder", +[]() noexcept { api::remainder(0); }},
        Scenario {"shift", +[]() noexcept { api::shift(-1); }},
        Scenario {"width", +[]() noexcept { api::shift(32); }},
        Scenario {"slice-index", +[]() noexcept { api::slice_index(1); }},
        Scenario {"slice-negative", +[]() noexcept { api::slice_index(-1); }},
        Scenario {"slice-range", +[]() noexcept { api::slice_range(0, 2); }},
        Scenario {"slice-reversed", +[]() noexcept { api::slice_range(1, 0); }},
        Scenario {"slice-empty", +[]() noexcept { api::slice_known_empty(); }},
        Scenario {"slice-format", +[]() noexcept { api::slice_known_format(); }},
        Scenario {"unicode", +[]() noexcept { api::unicode(); }},
        Scenario {
            "unicode-export",
            +[]() noexcept { api::accept_unicode_scalar(static_cast<char32_t>(0xd800)); }
        },
        Scenario {"formatted-text", api::formatted_text},
        Scenario {"clamp", +[]() noexcept { api::clamp(2.0f, 1.0f); }},
        Scenario {
            "simd-clamp",
            +[]() noexcept { api::simd_clamp(std::numeric_limits<float>::quiet_NaN()); }
        },
        Scenario {"simd-load", +[]() noexcept { api::simd_load(); }},
        Scenario {"simd-partial", +[]() noexcept { api::simd_partial(); }},
        Scenario {"simd-lane", +[]() noexcept { api::simd_lane(16); }},
        Scenario {"simd-with-lane", +[]() noexcept { api::simd_with_lane(16); }},
        Scenario {"simd-prefix", +[]() noexcept { api::simd_prefix(17); }},
        Scenario {"outside-test", api::check_without_test},
        Scenario {"assertion-snapshot", api::assertion_snapshot},
        Scenario {
            "fatal-tests",
            +[]() noexcept { static_cast<void>(carven::runtime::run_generated_tests()); }
        },
        Scenario {"divide", +[]() noexcept { api::divide(0); }},
        Scenario {"index", +[]() noexcept { api::index(1); }},
        Scenario {"slice-length", api::slice_length},
        Scenario {"assertion", +[]() noexcept { api::assertion(false); }},
    };
    for (const auto& scenario : scenarios) {
        if (scenario.name != argv[1]) {
            continue;
        }
        // Only the selected operation's SIGABRT completes this observation.
        if (std::signal(SIGABRT, +[](int) { std::_Exit(EXIT_SUCCESS); }) == SIG_ERR) {
            return EXIT_FAILURE;
        }
        scenario.action();
        return EXIT_FAILURE;
    }
    return 1;
}
