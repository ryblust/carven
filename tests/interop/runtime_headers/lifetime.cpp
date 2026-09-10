#include <carven/runtime/lifetime.hpp>

auto lifetime_header_contract() noexcept -> int {
    auto storage = carven::runtime::DeferredResult<const int> {};
    storage.initialize([]() noexcept -> int { return 42; });
    return *storage;
}
