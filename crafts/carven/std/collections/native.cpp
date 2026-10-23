#include <carven/std/collections/native.hpp>

#include <carven/runtime/collection/hash.hpp>

#include <bit>

namespace carven::standard::collections {

auto hash_bytes(runtime::Slice<std::uint8_t> bytes) noexcept -> std::uint64_t {
    if (bytes.empty()) {
        return runtime::hash_bytes({});
    }
    return runtime::hash_bytes(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
    );
}

auto digest() noexcept -> DigestState {
    return {.first = runtime::hash_seed, .second = 0x6c62272e07bb0142ull};
}

auto update_digest(
    std::uint64_t first,
    std::uint64_t second,
    runtime::Slice<std::uint8_t> bytes
) noexcept -> DigestState {
    for (const auto byte : bytes) {
        first = runtime::hash_byte(first, byte);
        second = std::rotl(second ^ byte, 11) * 0x9e3779b185ebca87ull;
    }
    return {.first = first, .second = second};
}

} // namespace carven::standard::collections
