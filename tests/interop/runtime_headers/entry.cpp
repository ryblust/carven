#include <carven/runtime/entry.hpp>

auto entry_header_contract(int argc, const char* const* argv) noexcept -> std::size_t {
    auto count = std::size_t {0};
    for (const auto& [index, value] : carven::runtime::entry_args(argc, argv)) {
        count += index + value.size();
    }
    return count;
}
