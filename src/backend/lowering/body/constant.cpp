module carven:backend.lowering.body.constant.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace body_lowering {

auto binary_expression(TargetExpr left, TargetBinaryOperator operation, TargetExpr right) noexcept
    -> TargetExpr {
    return {
        .value = TargetBinaryExpr {
            .left = target_child(std::move(left)),
            .op = operation,
            .right = target_child(std::move(right)),
        },
    };
}

auto prefix_expression(TargetPrefixOperator operation, TargetExpr operand) noexcept -> TargetExpr {
    return {
        .value = TargetPrefixExpr {
            .op = operation,
            .operand = target_child(std::move(operand)),
        },
    };
}

auto template_call_expression(
    TargetExpr callee,
    std::vector<TargetTypeID> template_arguments,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr {
    return {
        .value = TargetCallExpr {
            .callee = target_child(std::move(callee)),
            .template_argument_type_ids = std::move(template_arguments),
            .arguments = std::move(arguments),
        },
    };
}

auto call_member(
    TargetExpr owner,
    std::string_view member,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr {
    return call_expression(
        member_expression(std::move(owner), TargetIdentifier::from_spelling(member)),
        std::move(arguments)
    );
}

auto statement_expression(TargetExpr expression) noexcept -> TargetStmt {
    return generated_statement(
        TargetExprStmt {
            .expression = std::move(expression),
        }
    );
}
auto integer_suffix(const SemIRProgram& semantic, TypeID type) noexcept -> TargetIntegerSuffix {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic.types().type(type).value);
    if (builtin == nullptr) {
        return TargetIntegerSuffix::None;
    }
    switch (builtin->kind) {
        case BuiltinType::I64:
        case BuiltinType::Isize:        return TargetIntegerSuffix::LongLong;
        case BuiltinType::U64:
        case BuiltinType::Usize:        return TargetIntegerSuffix::UnsignedLongLong;
        case BuiltinType::U8:
        case BuiltinType::U16:
        case BuiltinType::U32:          return TargetIntegerSuffix::Unsigned;
        case BuiltinType::Bool:
        case BuiltinType::Char:
        case BuiltinType::I8:
        case BuiltinType::I16:
        case BuiltinType::I32:
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::Str:
        case BuiltinType::StrBytesView:
        case BuiltinType::StrCharsView:
        case BuiltinType::Void:
        case BuiltinType::EntryArgs:    return TargetIntegerSuffix::None;
    }
    std::unreachable();
}

auto typed_integer_expression(
    ModuleLowering& context,
    const IntegerConstant& value,
    TypeID type,
    LiteralContext use
) noexcept -> TargetExpr {
    auto result = TargetExpr {
        .value = TargetLiteralExpr {
            .value = TargetIntegerLiteral {
                .negative = value.negative(),
                .magnitude = value.magnitude(),
                .suffix = integer_suffix(context.semantic(), type),
            },
        },
    };
    if (use == LiteralContext::TargetTyped) {
        return result;
    }
    return {
        .value = TargetConstructionExpr {
            .type = context.lower_type(type),
            .initializer = target_expressions(std::move(result)),
        },
    };
}

auto enum_case_index(const SemIRProgram& semantic, EnumCaseID case_id) noexcept -> std::size_t {
    const auto owner = semantic.declarations().enum_case(case_id).owner;
    const auto& cases = semantic.declarations().enumeration(owner).cases;
    const auto found = std::ranges::find(cases, case_id);
    if (found == cases.end()) {
        invariant_violation("enum case is absent from its owner declaration");
    }
    return static_cast<std::size_t>(found - cases.begin());
}

auto enum_case_expression(
    ModuleLowering& context,
    EnumCaseID case_id,
    std::vector<TargetExpr> payload
) noexcept -> TargetExpr {
    const auto owner = context.semantic().declarations().enum_case(case_id).owner;
    const auto owner_type = context.named_type(context.enumeration_name(owner));
    auto member =
        static_member_expression(owner_type, context.names().enum_case_identifier(case_id));
    if (std::holds_alternative<PayloadEnumRepresentation>(
            context.semantic().declarations().enumeration(owner).representation
        )) {
        return call_expression(std::move(member), std::move(payload));
    }
    if (!payload.empty()) {
        invariant_violation("numeric enum case construction has a payload");
    }
    return member;
}

auto constant_expression(ModuleLowering& context, ConstantID id, LiteralContext use) noexcept
    -> TargetExpr {
    const auto& fact = context.semantic().constants().constant(id);
    return std::visit(
        Overloaded {
            [&](const IntegerConstant& value) noexcept {
                return typed_integer_expression(context, value, fact.type, use);
            },
            [](const BooleanConstant& value) noexcept { return bool_expression(value.value); },
            [&](const StringConstant& value) noexcept {
                return string_expression(
                    std::string(context.semantic().provenance().spelling(value.value)),
                    TargetStringLiteralKind::StringView
                );
            },
            [](const F32Constant& value) noexcept -> TargetExpr {
                return {
                    .value = TargetLiteralExpr {
                        .value = TargetFloatLiteral {.value = value.value},
                    },
                };
            },
            [](const F64Constant& value) noexcept -> TargetExpr {
                return {
                    .value = TargetLiteralExpr {
                        .value = TargetFloatLiteral {.value = value.value},
                    },
                };
            },
            [](const CharacterConstant& value) noexcept -> TargetExpr {
                return {
                    .value = TargetLiteralExpr {
                        .value = TargetCharacterLiteral {
                            .scalar = value.scalar,
                        },
                    },
                };
            },
            [&](const NumericEnumConstant& value) noexcept {
                return enum_case_expression(context, value.enum_case, {});
            },
            [&](const PayloadEnumConstant& value) noexcept {
                auto payload = std::vector<TargetExpr>();
                payload.reserve(value.payload.size());
                for (const auto child : value.payload) {
                    payload.push_back(constant_expression(context, child));
                }
                return enum_case_expression(context, value.enum_case, std::move(payload));
            },
        },
        fact.value
    );
}


} // namespace body_lowering

auto lower_constant_expression(ModuleLowering& context, ConstantID constant) noexcept
    -> TargetExpr {
    return body_lowering::constant_expression(context, constant);
}

auto lower_numeric_enum_case_value_expression(
    ModuleLowering& context,
    EnumCaseID enum_case
) noexcept -> TargetExpr {
    const auto& declaration = context.semantic().declarations().enum_case(enum_case);
    if (!declaration.constant.has_value()) {
        invariant_violation("numeric enum case has no canonical constant");
    }
    const auto& fact = context.semantic().constants().constant(*declaration.constant);
    const auto* numeric = std::get_if<NumericEnumConstant>(&fact.value);
    if (numeric == nullptr || numeric->enum_case != enum_case) {
        invariant_violation("numeric enum case has a non-numeric or foreign canonical constant");
    }
    const auto& enumeration = context.semantic().declarations().enumeration(declaration.owner);
    const auto* representation =
        std::get_if<NumericEnumRepresentation>(&enumeration.representation);
    if (representation == nullptr) {
        invariant_violation("numeric enum case belongs to a payload enum");
    }
    return body_lowering::typed_integer_expression(
        context,
        numeric->value,
        representation->underlying_type,
        body_lowering::LiteralContext::TargetTyped
    );
}
