module carven:semantic.semir.delegation.impl;

import :semantic.semir.delegation;
import :source.cpp.identifier;
import std;

auto valid_cpp_name(const CppNameReference& name) noexcept -> bool {
    return (name.lookup == CppNameLookup::Global || name.lookup == CppNameLookup::ModuleScope)
        && !name.components.empty()
        && std::ranges::all_of(name.components, is_supported_cpp_identifier);
}

auto cpp_operation_accepts_arity(const CppOperation& operation, std::size_t arity) noexcept
    -> bool {
    return operation.visit([arity](const auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, CppNameOperation>) {
            return arity == 0uz && valid_cpp_name(value.name);
        } else if constexpr (std::same_as<Value, CppStaticMemberOperation>) {
            return arity == 0uz && is_supported_cpp_identifier(value.member);
        } else if constexpr (std::same_as<Value, CppConstructOperation>) {
            return true;
        } else if constexpr (std::same_as<Value, CppMemberOperation>) {
            return arity == 1uz && !value.name.empty();
        } else if constexpr (std::same_as<Value, CppIndexOperation>
                             || std::same_as<Value, CppBinaryOperation>) {
            return arity == 2uz;
        } else {
            return arity == 1uz;
        }
    });
}

namespace {

template<typename Visitor>
auto visit_query_operands(const CppQueryType& query, Visitor visit) noexcept -> void {
    query.expression.visit([&](const auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, CppCallQuery>) {
            visit_cpp_callee_operand(value.callee, visit);
            for (const auto& argument : value.arguments) {
                visit(argument);
            }
        } else if constexpr (std::same_as<Value, CppConstructQuery>) {
            for (const auto& argument : value.arguments) {
                visit(argument.operand);
            }
        } else if constexpr (std::same_as<Value, CppMemberQuery>) {
            visit(value.receiver);
        } else if constexpr (std::same_as<Value, CppIndexQuery>) {
            visit(value.receiver);
            visit(value.index);
        } else if constexpr (std::same_as<Value, CppUnaryQuery>) {
            visit(value.operand);
        } else if constexpr (std::same_as<Value, CppBinaryQuery>) {
            visit(value.left);
            visit(value.right);
        }
    });
}

} // namespace

auto cpp_type_name(const CppTypeValue& type) noexcept -> const CppNameReference* {
    if (const auto* named = std::get_if<CppNamedType>(&type.form)) {
        return std::addressof(named->name);
    }
    if (std::holds_alternative<CppConstCharPointerType>(type.form)) {
        return nullptr;
    }
    const auto& query = std::get<CppQueryType>(type.form);
    if (const auto* name = std::get_if<CppNameReference>(&query.expression)) {
        return name;
    }
    if (const auto* call = std::get_if<CppCallQuery>(&query.expression)) {
        if (const auto* name = std::get_if<CppNameReference>(&call->callee)) {
            return name;
        }
    }
    return nullptr;
}

auto valid_cpp_type(const CppTypeValue& type) noexcept -> bool {
    if (const auto* name = cpp_type_name(type); name != nullptr && !valid_cpp_name(*name)) {
        return false;
    }
    const auto* query = std::get_if<CppQueryType>(&type.form);
    if (query == nullptr) {
        return true;
    }
    auto valid = true;
    visit_query_operands(*query, [&](const CppTypeOperand& operand) noexcept {
        valid &= operand.access == AccessMode::Read
            || operand.access == AccessMode::Write
            || operand.access == AccessMode::Take;
    });
    return valid && query->expression.visit([](const auto& value) static noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, CppMemberQuery>
                      || std::same_as<Value, CppStaticMemberQuery>) {
            return is_supported_cpp_identifier(value.member);
        } else if constexpr (std::same_as<Value, CppConstructQuery>) {
            return std::ranges::all_of(
                value.arguments,
                [](const CppConstructArgument& argument) static noexcept {
                    return !argument.constant || argument.operand.access == AccessMode::Read;
                }
            );
        } else if constexpr (std::same_as<Value, CppCallQuery>) {
            if (const auto* member = std::get_if<CppStaticMemberCallee<TypeID>>(&value.callee)) {
                return is_supported_cpp_identifier(member->member);
            }
            const auto* member = std::get_if<CppMemberCallee<CppTypeOperand>>(&value.callee);
            return member == nullptr || is_supported_cpp_identifier(member->member);
        } else {
            return true;
        }
    });
}
