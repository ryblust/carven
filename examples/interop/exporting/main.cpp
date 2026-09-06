#include <carven/api/examples/interop/exporting/pricing.hpp>

#include <iostream>

auto main() -> int {
    std::cout << "Price in cents:\n"
              << carven::api::examples::interop::exporting::pricing::price_cents(12) << '\n';
}
