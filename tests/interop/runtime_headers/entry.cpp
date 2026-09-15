#include <carven/runtime/entry.hpp>

#include <type_traits>

static_assert(
    std::is_same_v<carven::runtime::EntryArgs, decltype(carven::runtime::entry_args(0, nullptr))>
);

auto entry_header_contract(int argc, const char* const* argv) noexcept -> std::size_t {
    auto count = std::size_t {0};
    const carven::runtime::EntryArgs arguments = carven::runtime::entry_args(argc, argv);
    for (const auto& [index, value] : arguments) {
        count += index + value.size();
    }
    return count;
}
