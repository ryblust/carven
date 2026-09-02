#include "unicode_contract/import_provider.hpp"

auto invalid_unicode_scalar() noexcept -> char32_t {
    return static_cast<char32_t>(0xd800);
}
