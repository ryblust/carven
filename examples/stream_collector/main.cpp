#include <carven/api/examples/stream_collector/workflow.hpp>
#include "stream.hpp"

#include <iostream>
#include <sstream>

auto main() -> int {
    auto first_input = std::istringstream("alpha\nbeta\nalpha\ngamma\n");
    auto second_input = std::istringstream("beta\ndelta\ngamma\ndelta\n");
    auto first = collector::Stream {.input = first_input, .line = {}};
    auto second = collector::Stream {.input = second_input, .line = {}};
    const auto result = carven::api::examples::stream_collector::workflow::collect(first, second);
    const auto* success = result.success_if();
    if (success == nullptr) {
        std::cerr << "Stream collection stopped.\n";
        return 1;
    }
    const auto& report = success->value;
    std::cout << "Read " << report.total << " records; unique: " << report.records.size()
              << "; duplicates: " << report.duplicates << '\n';
    for (const auto& record : report.records) {
        std::cout << record.as_str() << '\n';
    }
}
