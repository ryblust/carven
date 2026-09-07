module carven:semantic.semir.delegation;

import :semantic.semir.ids;
import :semantic.semir.operation;
import std;

enum class CppNameLookup {
    Global,
    ModuleScope,
};

struct CppNameReference final {
    ModuleID context_module;
    CppNameLookup lookup;
    std::vector<std::string> components;
    auto operator==(const CppNameReference&) const noexcept -> bool = default;
};

auto valid_cpp_name(const CppNameReference& name) noexcept -> bool;

struct CppNamedType final {
    CppNameReference name;
    std::vector<TypeID> arguments;
    auto operator==(const CppNamedType&) const noexcept -> bool = default;
};

struct CppCStringOperation final {
    std::string bytes;
    auto operator==(const CppCStringOperation&) const noexcept -> bool = default;
};

struct CppConstCharPointerType final {
    auto operator==(const CppConstCharPointerType&) const noexcept -> bool = default;
};

struct CppNameOperation final {
    CppNameReference name;

    auto operator==(const CppNameOperation&) const noexcept -> bool = default;
};

struct CppConstructOperation final {
    auto operator==(const CppConstructOperation&) const noexcept -> bool = default;
};

struct CppMemberOperation final {
    std::string name;
    auto operator==(const CppMemberOperation&) const noexcept -> bool = default;
};

struct CppIndexOperation final {
    auto operator==(const CppIndexOperation&) const noexcept -> bool = default;
};

struct CppConvertOperation final {
    bool explicit_cast;
    auto operator==(const CppConvertOperation&) const noexcept -> bool = default;
};

struct CppUpdateOperation final {
    bool increment;
    auto operator==(const CppUpdateOperation&) const noexcept -> bool = default;
};

struct CppBinaryOperation final {
    BinaryOperator operation;
    auto operator==(const CppBinaryOperation&) const noexcept -> bool = default;
};

struct CppUnaryOperation final {
    UnaryOperator operation;
    auto operator==(const CppUnaryOperation&) const noexcept -> bool = default;
};

using CppOperation = std::variant<
    CppCStringOperation,
    CppNameOperation,
    CppConstructOperation,
    CppMemberOperation,
    CppIndexOperation,
    CppConvertOperation,
    CppBinaryOperation,
    CppUnaryOperation,
    CppUpdateOperation>;

auto cpp_operation_accepts_arity(const CppOperation& operation, std::size_t arity) noexcept -> bool;

struct CppTypeOperand final {
    TypeID type;
    AccessMode access;
    auto operator==(const CppTypeOperand&) const noexcept -> bool = default;
};

template<typename Operand>
struct CppMemberCallee final {
    Operand receiver;
    std::string member;
    auto operator==(const CppMemberCallee&) const noexcept -> bool = default;
};

template<typename Operand>
using CppCallee = std::variant<CppNameReference, CppMemberCallee<Operand>, Operand>;

template<typename Operand, typename Visitor>
auto visit_cpp_callee_operand(const CppCallee<Operand>& callee, Visitor visit) noexcept -> void {
    std::visit(
        [&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, Operand>) {
                visit(value);
            } else if constexpr (std::same_as<Value, CppMemberCallee<Operand>>) {
                visit(value.receiver);
            }
        },
        callee
    );
}

struct CppCallQuery final {
    CppCallee<CppTypeOperand> callee;
    std::vector<CppTypeOperand> arguments;
    auto operator==(const CppCallQuery&) const noexcept -> bool = default;
};

struct CppMemberQuery final {
    CppTypeOperand receiver;
    std::string member;
    auto operator==(const CppMemberQuery&) const noexcept -> bool = default;
};

struct CppIndexQuery final {
    CppTypeOperand receiver;
    CppTypeOperand index;
    auto operator==(const CppIndexQuery&) const noexcept -> bool = default;
};

struct CppUnaryQuery final {
    UnaryOperator operation;
    CppTypeOperand operand;
    auto operator==(const CppUnaryQuery&) const noexcept -> bool = default;
};

struct CppBinaryQuery final {
    BinaryOperator operation;
    CppTypeOperand left;
    CppTypeOperand right;
    auto operator==(const CppBinaryQuery&) const noexcept -> bool = default;
};

struct CppQueryType final {
    std::variant<
        CppNameReference,
        CppCallQuery,
        CppMemberQuery,
        CppIndexQuery,
        CppUnaryQuery,
        CppBinaryQuery>
        expression;
    auto operator==(const CppQueryType&) const noexcept -> bool = default;
};

struct CppTypeValue final {
    std::variant<CppNamedType, CppQueryType, CppConstCharPointerType> form;
    auto operator==(const CppTypeValue&) const noexcept -> bool = default;
};

auto cpp_query_type(
    const CppOperation& operation,
    std::span<const CppTypeOperand> operands
) noexcept -> CppQueryType;
auto cpp_type_references(const CppTypeValue& type) noexcept -> std::vector<TypeID>;
auto cpp_type_names(const CppTypeValue& type) noexcept -> std::vector<CppNameReference>;
auto valid_cpp_type(const CppTypeValue& type) noexcept -> bool;
