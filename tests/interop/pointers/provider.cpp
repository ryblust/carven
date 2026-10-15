#include "provider.hpp"

auto pointer_probe::output_adapter() noexcept -> std::int32_t* {
    std::int32_t* result = nullptr;
    output(&result);
    return result;
}
