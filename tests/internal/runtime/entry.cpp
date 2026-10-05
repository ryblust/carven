module;
#include <carven/runtime/entry.hpp>

module carven:test.internal.runtime.entry;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Runtime: entry argument ingress preserves order and UTF-8 bytes"_test = [] static noexcept {
        const auto arguments =
            std::to_array<const char*>({"program", "alpha", "\xe4\xbd\xa0", "omega"});
        auto observed = std::vector<std::pair<std::size_t, std::string_view>> {};
        for (const auto argument :
             carven::runtime::entry_args(static_cast<int>(arguments.size()), arguments.data())) {
            observed.push_back(argument);
        }

        const auto expected = std::vector {
            std::pair {0uz, std::string_view {"alpha"}},
            std::pair {1uz, std::string_view {"\xe4\xbd\xa0"}},
            std::pair {2uz, std::string_view {"omega"}},
        };
        expect(observed == expected);
    };

    "Runtime: entry argument ingress rejects truncated UTF-8"_test = [] static noexcept {
        expect(expect_termination("entry-argument-invalid-utf8", []() static noexcept {
            const auto arguments = std::to_array<const char*>({"program", "\xc2"});
            static_cast<void>(
                carven::runtime::entry_args(static_cast<int>(arguments.size()), arguments.data())
            );
        }));
    };
});

} // namespace
