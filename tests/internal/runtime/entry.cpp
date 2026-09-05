module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/entry.hpp>

module carven:test.internal.runtime.entry;

import std;

TEST_CASE("Runtime: entry argument ingress preserves order and UTF-8 bytes") {
    const auto arguments =
        std::to_array<const char*>({"program", "alpha", "\xe4\xbd\xa0", "omega"});
    auto observed = std::vector<std::pair<std::size_t, std::string_view>> {};
    for (const auto argument :
         carven::runtime::entry_args(static_cast<int>(arguments.size()), arguments.data())) {
        observed.push_back(argument);
    }

    REQUIRE_EQ(observed.size(), 3u);
    CHECK_EQ(observed[0], std::pair {0uz, std::string_view {"alpha"}});
    CHECK_EQ(observed[1], std::pair {1uz, std::string_view {"\xe4\xbd\xa0"}});
    CHECK_EQ(observed[2], std::pair {2uz, std::string_view {"omega"}});
}
