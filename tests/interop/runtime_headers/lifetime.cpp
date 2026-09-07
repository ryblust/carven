#include <carven/runtime/lifetime.hpp>

auto lifetime_header_contract() noexcept -> int {
    auto storage = carven::runtime::DeferredStorage<int> {};
    storage.initialize([](void* address) noexcept {
        return std::construct_at(static_cast<int*>(address), 42);
    });
    return *storage;
}
