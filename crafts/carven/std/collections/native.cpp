#include <carven/std/collections/native.hpp>

#include <carven/runtime/collection/hash.hpp>

namespace carven::standard::collections {

auto hash_bytes(runtime::Slice<std::uint8_t> bytes) noexcept -> std::uint64_t {
    if (bytes.empty()) {
        return runtime::hash_bytes({});
    }
    return runtime::hash_bytes(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
    );
}

} // namespace carven::standard::collections
