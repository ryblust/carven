#include <carven/api/examples/interop/export/pricing.hpp>

#include <iostream>

auto main() -> int {
    std::cout << "Price in cents:\n"
              << carven::api::examples::interop::cv_escaped_6578706f7274::pricing::price_cents(12)
              << '\n';
}
