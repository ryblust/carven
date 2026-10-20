#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

enum class HostTypeKind {
    Opaque,
    Void,
    Integer,
    Boolean,
    String,
    Str,
    Vector,
    Slice,
    Record,
    Enum
};

struct HostType;

struct HostTypeField final {
    std::string_view name;
    const HostType& (*describe)() noexcept;
};

struct HostTypeCase final {
    std::string_view name;
    std::span<const HostTypeField> payload;
    std::optional<std::uint64_t> numeric_value;
};

// Child descriptions are lazy so recursive nominal types do not initialize themselves.
struct HostType final {
    HostTypeKind kind;
    const void* native_type;
    bool read_borrows;
    std::size_t integer_bits;
    bool integer_signed;
    std::string_view name;
    const HostType& (*element)() noexcept;
    std::span<const HostTypeField> fields;
    std::span<const HostTypeCase> cases;
};

struct HostCallType final {
    const HostType& (*describe)() noexcept;
    bool allow_cpp;
};

struct HostFailureType final {
    std::string module_path;
    std::string name;
    HostCallType type;
};
