#include <carven/api/examples/cpp_host/pricing.hpp>

#include <iostream>
#include <utility>

namespace pricing = carven::api::examples::cpp_host::pricing;

auto main() -> int {
    auto quote = pricing::quote(12);
    pricing::append_note(quote, " (delivery included)");
    const auto finished = pricing::finish(std::move(quote));
    std::cout << finished.as_str() << '\n';
}
