module carven:semantic.hir.type;

import :semantic.hir.access;
import :semantic.hir.ids;
import :semantic.hir.interop;
import std;

enum class HIRBuiltinType {
    Bool,
    Char,
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Isize,
    Usize,
    F32,
    F64,
    Str,
    StrBytesView,
    StrCharsView,
    Void,
    EntryArgs,
};

auto builtin_is_integer(HIRBuiltinType type) noexcept -> bool;
auto builtin_is_signed_integer(HIRBuiltinType type) noexcept -> bool;
auto builtin_is_numeric(HIRBuiltinType type) noexcept -> bool;
auto builtin_integer_width(HIRBuiltinType type) noexcept -> std::optional<std::uint8_t>;

struct HIRBuiltinTypeValue final {
    HIRBuiltinType kind;
    constexpr auto operator<=>(const HIRBuiltinTypeValue&) const noexcept = default;
};

struct HIRStructTypeValue final {
    StructID structure;
    constexpr auto operator<=>(const HIRStructTypeValue&) const noexcept = default;
};

struct HIREnumTypeValue final {
    EnumID enumeration;
    constexpr auto operator<=>(const HIREnumTypeValue&) const noexcept = default;
};

struct HIRArrayTypeValue final {
    HIRTypeID element_type_id;
    std::uint64_t extent;
    constexpr auto operator<=>(const HIRArrayTypeValue&) const noexcept = default;
};

struct HIRFunctionParameterType final {
    HIRAccessMode access;
    HIRTypeID type;
    constexpr auto operator<=>(const HIRFunctionParameterType&) const noexcept = default;
};

struct HIRFailureSet final {
    std::vector<HIRTypeID> members;
    auto operator<=>(const HIRFailureSet&) const noexcept = default;
};

struct HIRCallableSignature final {
    std::vector<HIRFunctionParameterType> parameters;
    HIRTypeID result;
    FailureSetID failure_set;
    auto operator<=>(const HIRCallableSignature&) const noexcept = default;
};

struct HIRBodyImplementation final {
    BodyID body;
};

using HIRCallableImplementation = std::variant<HIRBodyImplementation, HIRCppImportImplementation>;

struct HIRCallable final {
    std::vector<HIRFunctionParameterType> parameters;
    HIRTypeID result;
    HIRCallableImplementation implementation;
};

auto callable_body_id(const HIRCallable& callable) noexcept -> std::optional<BodyID>;
auto cpp_import_form_origin(const HIRCallable& callable) noexcept -> std::optional<ProgramOriginID>;

struct HIRCallableFlow final {
    FailureSetID effective_failure_set;
};

struct HIRFunctionTypeValue final {
    CallableID callable;
    constexpr auto operator<=>(const HIRFunctionTypeValue&) const noexcept = default;
};

struct HIRFunctionRefTypeValue final {
    CallableSignatureID signature;
    constexpr auto operator<=>(const HIRFunctionRefTypeValue&) const noexcept = default;
};

struct HIRClosureTypeValue final {
    CallableID callable;
    bool capturing;
    constexpr auto operator<=>(const HIRClosureTypeValue&) const noexcept = default;
};

struct HIRErrorTypeValue final {
    constexpr auto operator<=>(const HIRErrorTypeValue&) const noexcept = default;
};

using HIRTypeValue = std::variant<
    HIRBuiltinTypeValue,
    HIRStructTypeValue,
    HIREnumTypeValue,
    HIRArrayTypeValue,
    HIRFunctionTypeValue,
    HIRFunctionRefTypeValue,
    HIRClosureTypeValue,
    HIRErrorTypeValue>;

struct HIRType final {
    HIRTypeValue value;
};
