module carven:semantic.hir.type.impl;

import :semantic.hir.type;
import std;

auto callable_body_id(const HIRCallable& callable) noexcept -> std::optional<BodyID> {
    const auto* implementation = std::get_if<HIRBodyImplementation>(&callable.implementation);
    return implementation == nullptr ? std::nullopt : std::optional(implementation->body);
}

auto cpp_import_form_origin(const HIRCallable& callable) noexcept
    -> std::optional<ProgramOriginID> {
    const auto* implementation = std::get_if<HIRCppImportImplementation>(&callable.implementation);
    return implementation == nullptr ? std::nullopt : std::optional(implementation->form_origin);
}

auto builtin_is_signed_integer(HIRBuiltinType type) noexcept -> bool {
    switch (type) {
        case HIRBuiltinType::I8:
        case HIRBuiltinType::I16:
        case HIRBuiltinType::I32:
        case HIRBuiltinType::I64:
        case HIRBuiltinType::Isize: return true;
        default:                    return false;
    }
}

auto builtin_integer_width(HIRBuiltinType type) noexcept -> std::optional<std::uint8_t> {
    switch (type) {
        case HIRBuiltinType::I8:
        case HIRBuiltinType::U8:  return 8;
        case HIRBuiltinType::I16:
        case HIRBuiltinType::U16: return 16;
        case HIRBuiltinType::I32:
        case HIRBuiltinType::U32: return 32;
        case HIRBuiltinType::I64:
        case HIRBuiltinType::U64: return 64;
        case HIRBuiltinType::Isize:
            return static_cast<std::uint8_t>(std::numeric_limits<std::ptrdiff_t>::digits + 1);
        case HIRBuiltinType::Usize:
            return static_cast<std::uint8_t>(std::numeric_limits<std::size_t>::digits);
        default: return std::nullopt;
    }
}

auto builtin_is_integer(HIRBuiltinType type) noexcept -> bool {
    return builtin_integer_width(type).has_value();
}

auto builtin_is_numeric(HIRBuiltinType type) noexcept -> bool {
    return builtin_is_integer(type) || type == HIRBuiltinType::F32 || type == HIRBuiltinType::F64;
}
