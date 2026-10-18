module carven:support.binary.impl;

import :support.binary;
import std;

BinaryWriter::BinaryWriter() noexcept
    : depth(0) {}

auto BinaryWriter::unsigned_value(std::uint64_t value, std::size_t width) noexcept -> void {
    if (width > sizeof(value)) {
        fail("unsupported binary integer width");
        return;
    }
    auto bytes = std::array<std::byte, sizeof(value)> {};
    for (auto index = 0uz; index < width; ++index) {
        bytes[index] = static_cast<std::byte>((value >> (index * 8)) & 0xff);
    }
    append(std::span(bytes).first(width));
}

auto BinaryWriter::append(std::span<const std::byte> bytes) noexcept -> void {
    if (failure) {
        return;
    }
    if (bytes.size() > compilation_binary_limits.bytes - storage.size()) {
        fail("binary output exceeds byte limit");
        return;
    }
    storage.insert(storage.end(), bytes.begin(), bytes.end());
}

auto BinaryWriter::fail(std::string message) noexcept -> void {
    if (!failure) {
        failure.emplace(BinaryError {.message = std::move(message), .offset = storage.size()});
    }
}

auto BinaryWriter::error() const noexcept -> const std::optional<BinaryError>& {
    return failure;
}

auto BinaryWriter::take() && noexcept -> std::vector<std::byte> {
    return std::move(storage);
}

BinaryReader::BinaryReader(std::span<const std::byte> bytes) noexcept
    : storage(bytes),
      offset(0),
      elements_remaining(compilation_binary_limits.elements),
      allocation_remaining(compilation_binary_limits.bytes),
      depth(0) {}

auto BinaryReader::unsigned_value(std::size_t width) noexcept
    -> std::expected<std::uint64_t, BinaryError> {
    if (width > sizeof(std::uint64_t)) {
        return fail("unsupported binary integer width");
    }
    auto bytes = take(width);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    auto value = 0ull;
    for (auto index = 0uz; index < width; ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>((*bytes)[index]))
            << (index * 8);
    }
    return value;
}

auto BinaryReader::take(std::size_t count) noexcept
    -> std::expected<std::span<const std::byte>, BinaryError> {
    if (storage.size() > compilation_binary_limits.bytes) {
        return fail("binary input exceeds byte limit");
    }
    if (count > storage.size() - offset) {
        return fail("truncated binary input");
    }
    const auto bytes = storage.subspan(offset, count);
    offset += count;
    return bytes;
}

auto BinaryReader::count(std::size_t element_size) noexcept
    -> std::expected<std::size_t, BinaryError> {
    auto value = unsigned_value(8);
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    if (*value > std::numeric_limits<std::size_t>::max() || *value > elements_remaining) {
        return fail("binary collection exceeds element limit");
    }
    const auto size = static_cast<std::size_t>(*value);
    if (element_size != 0 && size > allocation_remaining / element_size) {
        return fail("binary collection exceeds allocation limit");
    }
    elements_remaining -= size;
    allocation_remaining -= size * element_size;
    return size;
}

auto BinaryReader::fail(std::string message) const noexcept -> std::unexpected<BinaryError> {
    return std::unexpected(BinaryError {.message = std::move(message), .offset = offset});
}

auto BinaryReader::remaining() const noexcept -> std::size_t {
    return storage.size() - offset;
}

auto BinaryReader::finish() const noexcept -> std::expected<void, BinaryError> {
    if (remaining() != 0) {
        return fail("trailing bytes after binary value");
    }
    return {};
}
