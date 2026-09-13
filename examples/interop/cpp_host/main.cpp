#include <carven/api/examples/interop/cpp_host/pricing.hpp>

#include <iostream>

namespace pricing = carven::api::examples::interop::cpp_host::pricing;

auto main() -> int {
    std::cout << "Price in cents:\n" << pricing::price_cents(12) << '\n';
}
