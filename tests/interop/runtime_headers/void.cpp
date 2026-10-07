#include <carven/runtime/void.hpp>

auto void_header_contract() noexcept -> bool {
    return carven::runtime::Void {} == carven::runtime::Void {};
}
