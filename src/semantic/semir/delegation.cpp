module carven:semantic.semir.delegation.impl;

import :semantic.semir.delegation;
import :source.cpp.identifier;
import :support.invariant;
import :support.visit;
import std;

auto valid_cpp_name(const CppNameReference& name) noexcept -> bool {
    return (name.lookup == CppNameLookup::Global || name.lookup == CppNameLookup::ModuleScope)
        && !name.components.empty()
        && std::ranges::all_of(name.components, is_supported_cpp_identifier);
}

auto cpp_operation_accepts_arity(const CppOperation& operation, std::size_t arity) noexcept
    -> bool {
    return std::visit(
        [arity](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, CppNameOperation>) {
                return arity == 0uz && valid_cpp_name(value.name);
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
        },
        operation
    );
}

namespace {
template<typename Visitor>
auto visit_query_operands(const CppQueryType& query, Visitor visit) noexcept -> void {
    std::visit(
        [&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, CppCallQuery>) {
                visit_cpp_callee_operand(value.callee, visit);
                for (const auto& argument : value.arguments) {
                    visit(argument);
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
        },
        query.expression
    );
}
} // namespace

auto cpp_query_type(
    const CppOperation& operation,
    std::span<const CppTypeOperand> operands
) noexcept -> CppQueryType {
    if (!cpp_operation_accepts_arity(operation, operands.size())) {
        invariant_violation("C++ query operands disagree with the operation");
    }
    return std::visit(
        Overloaded {
            [&](const CppNameOperation& value) noexcept -> CppQueryType {
                return {.expression = value.name};
            },
            [&](const CppMemberOperation& value) noexcept -> CppQueryType {
                return {
                    .expression = CppMemberQuery {.receiver = operands[0], .member = value.name}
                };
            },
            [&](const CppIndexOperation&) noexcept -> CppQueryType {
                return {
                    .expression = CppIndexQuery {.receiver = operands[0], .index = operands[1]}
                };
            },
            [&](const CppUnaryOperation& value) noexcept -> CppQueryType {
                return {
                    .expression =
                        CppUnaryQuery {.operation = value.operation, .operand = operands[0]}
                };
            },
            [&](const CppBinaryOperation& value) noexcept -> CppQueryType {
                return {
                    .expression = CppBinaryQuery {
                        .operation = value.operation,
                        .left = operands[0],
                        .right = operands[1]
                    }
                };
            },
            [](const auto&) static noexcept -> CppQueryType {
                invariant_violation("operation requires an explicit result type");
            }
        },
        operation
    );
}

auto cpp_type_references(const CppTypeValue& type) noexcept -> std::vector<TypeID> {
    if (const auto* named = std::get_if<CppNamedType>(&type.form)) {
        return named->arguments;
    }
    auto result = std::vector<TypeID>();
    visit_query_operands(
        std::get<CppQueryType>(type.form),
        [&](const CppTypeOperand& operand) noexcept { result.push_back(operand.type); }
    );
    return result;
}

auto cpp_type_names(const CppTypeValue& type) noexcept -> std::vector<CppNameReference> {
    if (const auto* named = std::get_if<CppNamedType>(&type.form)) {
        return {named->name};
    }
    const auto& query = std::get<CppQueryType>(type.form);
    if (const auto* name = std::get_if<CppNameReference>(&query.expression)) {
        return {*name};
    }
    if (const auto* call = std::get_if<CppCallQuery>(&query.expression)) {
        if (const auto* name = std::get_if<CppNameReference>(&call->callee)) {
            return {*name};
        }
    }
    return {};
}

auto valid_cpp_type(const CppTypeValue& type) noexcept -> bool {
    for (const auto& name : cpp_type_names(type)) {
        if (!valid_cpp_name(name)) {
            return false;
        }
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
    return valid
        && std::visit(
               [](const auto& value) static noexcept {
                   using Value = std::remove_cvref_t<decltype(value)>;
                   if constexpr (std::same_as<Value, CppMemberQuery>) {
                       return is_supported_cpp_identifier(value.member);
                   } else if constexpr (std::same_as<Value, CppCallQuery>) {
                       const auto* member =
                           std::get_if<CppMemberCallee<CppTypeOperand>>(&value.callee);
                       return member == nullptr || is_supported_cpp_identifier(member->member);
                   } else {
                       return true;
                   }
               },
               query->expression
        );
}
