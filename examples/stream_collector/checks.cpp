#include <carven/api/examples/stream_collector/workflow.hpp>
#include "stream.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct Case final {
    const char* name;
    std::string_view first;
    std::string_view second;
    std::string_view expected;
    std::size_t total;
    std::size_t duplicates;
};

auto collect(const Case& input) noexcept {
    auto first_input = std::istringstream(std::string(input.first));
    auto second_input = std::istringstream(std::string(input.second));
    auto first = collector::Stream {.input = first_input, .line = {}};
    auto second = collector::Stream {.input = second_input, .line = {}};
    return carven::api::examples::stream_collector::workflow::collect(first, second);
}

} // namespace

auto main() -> int {
    constexpr auto cases = std::array {
        Case {
            .name = "empty",
            .first = "",
            .second = "",
            .expected = "",
            .total = 0,
            .duplicates = 0,
        },
        Case {
            .name = "first only and final partial line",
            .first = "alpha\n\nbeta",
            .second = "",
            .expected = "alpha\nbeta\n",
            .total = 2,
            .duplicates = 0,
        },
        Case {
            .name = "second only",
            .first = "",
            .second = "gamma\ndelta\n",
            .expected = "gamma\ndelta\n",
            .total = 2,
            .duplicates = 0,
        },
        Case {
            .name = "within and across streams",
            .first = "alpha\nbeta\nalpha\ngamma\n",
            .second = "beta\ndelta\ngamma\ndelta\n",
            .expected = "alpha\nbeta\ngamma\ndelta\n",
            .total = 8,
            .duplicates = 4,
        },
        Case {
            .name = "all duplicates",
            .first = "same\nsame\n",
            .second = "same\nsame\n",
            .expected = "same\n",
            .total = 4,
            .duplicates = 3,
        },
        Case {
            .name = "case whitespace and UTF8",
            .first = "same\nSame\n世界\n",
            .second = "same \n世界\nsame\n",
            .expected = "same\nSame\n世界\nsame \n",
            .total = 6,
            .duplicates = 2,
        },
    };
    for (const auto& input : cases) {
        // Native streams and their line buffers are already destroyed here.
        const auto result = collect(input);
        const auto* success = result.success_if();
        if (success == nullptr) {
            std::fprintf(stderr, "stream collection stopped: %s\n", input.name);
            return 1;
        }
        const auto& report = success->value;
        auto records = std::string();
        for (const auto& record : report.records) {
            records.append(record.as_str());
            records.push_back('\n');
        }
        if (report.total != input.total
            || report.duplicates != input.duplicates
            || report.records.size() != input.total - input.duplicates
            || records != input.expected) {
            std::fprintf(stderr, "stream collector failed: %s\n", input.name);
            return 1;
        }
    }
}
