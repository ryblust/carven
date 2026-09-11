#include <carven/runtime/utf.hpp>

static_assert(carven::runtime::encode_valid_utf8(U'\U0001f600').width == 4);
static_assert(carven::runtime::decode_valid_utf8(std::span<const char>("A", 1)).scalar == U'A');
static_assert(!carven::runtime::utf8_is_valid("\xc0\x80"));
