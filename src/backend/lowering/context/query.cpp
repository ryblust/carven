module carven:backend.lowering.context.query.impl;

import :backend.lowering.context;
import :backend.target.symbol;
import :support.visit;
import std;

auto ModuleLowering::cpp_name(const CppNameReference& name) noexcept -> TargetName {
    artifact_lowering.require_cpp_environment(
        name.context_module,
        name.lookup == CppNameLookup::Global ? CppEnvironmentRequirement::Declarations
                                             : CppEnvironmentRequirement::Using
    );
    auto components = std::vector<TargetIdentifier>();
    if (name.lookup == CppNameLookup::ModuleScope) {
        const auto owner =
            plan().names().module_names(name.context_module).qualified_namespace_name;
        components.assign(owner.components().begin(), owner.components().end());
    }
    for (const auto& component : name.components) {
        components.push_back(TargetIdentifier::from_spelling(component));
    }
    return TargetName::globally_qualified(std::move(components));
}

auto ModuleLowering::cpp_type_query(const CppQueryType& query) noexcept -> TargetExpr {
    const auto operand = [&](const CppTypeOperand& value) noexcept -> TargetExpr {
        return {
            .value = TargetCallExpr {
                .callee = target_child(intrinsic_expression(TargetSymbol::StdDeclval)),
                .template_argument_type_ids = {reference_type(
                    lower_type(value.type),
                    value.access == AccessMode::Read,
                    value.access == AccessMode::Take
                )},
                .arguments = {}
            }
        };
    };
    const auto member = [&](const CppTypeOperand& receiver,
                            std::string_view name) noexcept -> TargetExpr {
        return {
            .value = TargetMemberExpr {
                .operand = UniqueIndirect(operand(receiver)),
                .name = TargetIdentifier::from_spelling(name)
            }
        };
    };
    const auto operator_kind = Overloaded {
        [](const CppUnaryQuery& unary) static noexcept -> TargetPrefixOperator {
            switch (unary.operation) {
                case UnaryOperator::LogicalNot: return TargetPrefixOperator::LogicalNot;
                case UnaryOperator::Negate:     return TargetPrefixOperator::Negate;
                case UnaryOperator::BitwiseNot: return TargetPrefixOperator::BitwiseNot;
            }
            std::unreachable();
        },
        [](const CppBinaryQuery& binary) static noexcept -> TargetBinaryOperator {
            switch (binary.operation) {
                case BinaryOperator::BitwiseOr:    return TargetBinaryOperator::BitwiseOr;
                case BinaryOperator::BitwiseXor:   return TargetBinaryOperator::BitwiseXor;
                case BinaryOperator::BitwiseAnd:   return TargetBinaryOperator::BitwiseAnd;
                case BinaryOperator::Equal:        return TargetBinaryOperator::Equal;
                case BinaryOperator::NotEqual:     return TargetBinaryOperator::NotEqual;
                case BinaryOperator::Less:         return TargetBinaryOperator::Less;
                case BinaryOperator::LessEqual:    return TargetBinaryOperator::LessEqual;
                case BinaryOperator::Greater:      return TargetBinaryOperator::Greater;
                case BinaryOperator::GreaterEqual: return TargetBinaryOperator::GreaterEqual;
                case BinaryOperator::LeftShift:    return TargetBinaryOperator::LeftShift;
                case BinaryOperator::RightShift:   return TargetBinaryOperator::RightShift;
                case BinaryOperator::Add:          return TargetBinaryOperator::Add;
                case BinaryOperator::Subtract:     return TargetBinaryOperator::Subtract;
                case BinaryOperator::Multiply:     return TargetBinaryOperator::Multiply;
                case BinaryOperator::Divide:       return TargetBinaryOperator::Divide;
                case BinaryOperator::Remainder:    return TargetBinaryOperator::Remainder;
            }
            std::unreachable();
        },
    };
    return std::visit(
        Overloaded {
            [&](const CppNameReference& name) noexcept -> TargetExpr {
                return {.value = TargetNameExpr {.name = cpp_name(name)}};
            },
            [&](const CppMemberQuery& value) noexcept {
                return member(value.receiver, value.member);
            },
            [&](const CppIndexQuery& value) noexcept -> TargetExpr {
                return {
                    .value = TargetIndexExpr {
                        .operand = UniqueIndirect(operand(value.receiver)),
                        .index = UniqueIndirect(operand(value.index))
                    }
                };
            },
            [&](const CppUnaryQuery& value) noexcept -> TargetExpr {
                return {
                    .value = TargetPrefixExpr {
                        .op = operator_kind(value),
                        .operand = UniqueIndirect(operand(value.operand))
                    }
                };
            },
            [&](const CppBinaryQuery& value) noexcept -> TargetExpr {
                return {
                    .value = TargetBinaryExpr {
                        .left = UniqueIndirect(operand(value.left)),
                        .op = operator_kind(value),
                        .right = UniqueIndirect(operand(value.right))
                    }
                };
            },
            [&](const CppCallQuery& call) noexcept -> TargetExpr {
                auto callee = std::visit(
                    Overloaded {
                        [&](const CppNameReference& name) noexcept -> TargetExpr {
                            return {.value = TargetNameExpr {.name = cpp_name(name)}};
                        },
                        [&](const CppMemberCallee<CppTypeOperand>& value) noexcept {
                            return member(value.receiver, value.member);
                        },
                        [&](const CppTypeOperand& value) noexcept { return operand(value); }
                    },
                    call.callee
                );
                auto arguments = std::vector<TargetExpr>();
                for (const auto& value : call.arguments) {
                    arguments.push_back(operand(value));
                }
                return {
                    .value = TargetCallExpr {
                        .callee = UniqueIndirect(std::move(callee)),
                        .template_argument_type_ids = {},
                        .arguments = std::move(arguments)
                    }
                };
            }
        },
        query.expression
    );
}
