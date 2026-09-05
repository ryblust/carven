module carven:backend.lowering.context.construction.impl;

import :backend.lowering.context;
import :support.invariant;
import :support.visit;
import std;

auto target_child(TargetExpr expression) noexcept -> UniqueIndirect<TargetExpr> {
    return UniqueIndirect<TargetExpr>(std::move(expression));
}

auto name_expression(TargetName name) noexcept -> TargetExpr {
    return {.value = TargetNameExpr {.name = std::move(name)}};
}

auto name_expression(TargetIdentifier name) noexcept -> TargetExpr {
    return name_expression(TargetName {std::move(name)});
}

auto intrinsic_expression(TargetSymbol symbol) noexcept -> TargetExpr {
    return {.value = TargetIntrinsicNameExpr {.symbol = symbol}};
}

auto call_expression(TargetExpr callee, std::vector<TargetExpr> arguments) noexcept -> TargetExpr {
    return {
        .value = TargetCallExpr {
            .callee = target_child(std::move(callee)),
            .template_argument_type_ids = {},
            .arguments = std::move(arguments),
        },
    };
}

auto target_expressions(TargetExpr value) noexcept -> std::vector<TargetExpr> {
    auto result = std::vector<TargetExpr>();
    result.push_back(std::move(value));
    return result;
}

auto target_expressions(TargetExpr first, TargetExpr second) noexcept -> std::vector<TargetExpr> {
    auto result = std::vector<TargetExpr>();
    result.reserve(2);
    result.push_back(std::move(first));
    result.push_back(std::move(second));
    return result;
}

auto target_expressions(TargetExpr first, TargetExpr second, TargetExpr third) noexcept
    -> std::vector<TargetExpr> {
    auto result = std::vector<TargetExpr>();
    result.reserve(3);
    result.push_back(std::move(first));
    result.push_back(std::move(second));
    result.push_back(std::move(third));
    return result;
}

auto member_expression(TargetExpr operand, TargetMemberName member) noexcept -> TargetExpr {
    return {
        .value = TargetMemberExpr {
            .operand = target_child(std::move(operand)),
            .name = std::move(member),
        },
    };
}

auto scope_member_expression(TargetExpr operand, TargetMemberName member) noexcept -> TargetExpr {
    return {
        .value = TargetScopeMemberExpr {
            .operand = target_child(std::move(operand)),
            .name = std::move(member),
        },
    };
}

auto static_member_expression(TargetTypeID owner, TargetIdentifier member) noexcept -> TargetExpr {
    return {
        .value = TargetStaticMemberExpr {
            .owner = owner,
            .name = std::move(member),
        },
    };
}

auto move_expression(TargetExpr operand) noexcept -> TargetExpr {
    const auto* call = std::get_if<TargetCallExpr>(&operand.value);
    if (std::holds_alternative<TargetLiteralExpr>(operand.value)
        || std::holds_alternative<TargetConstructionExpr>(operand.value)
        || std::holds_alternative<TargetArrayExpr>(operand.value)
        || std::holds_alternative<TargetRegionExpr>(operand.value)
        // Named source callables return values; static members here are value factories.
        // Intrinsic calls can return places, so they must retain explicit moves.
        || (call != nullptr
            && (std::holds_alternative<TargetNameExpr>(call->callee->value)
                || std::holds_alternative<TargetStaticMemberExpr>(call->callee->value)))) {
        return operand;
    }
    return call_expression(
        intrinsic_expression(TargetSymbol::StdMove),
        target_expressions(std::move(operand))
    );
}

auto address_expression(TargetExpr operand) noexcept -> TargetExpr {
    return {
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::AddressOf,
            .operand = target_child(std::move(operand)),
        },
    };
}

auto dereference_expression(TargetExpr operand) noexcept -> TargetExpr {
    return {
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::Dereference,
            .operand = target_child(std::move(operand)),
        },
    };
}

auto bool_expression(bool value) noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = value}};
}

auto integer_expression(std::uint64_t value) noexcept -> TargetExpr {
    return {
        .value = TargetLiteralExpr {
            .value = TargetIntegerLiteral {
                .negative = false,
                .magnitude = value,
                .suffix = TargetIntegerSuffix::None,
            },
        },
    };
}

auto string_expression(std::string value, TargetStringLiteralKind kind) noexcept -> TargetExpr {
    return {
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::move(value),
                .kind = kind,
            },
        },
    };
}

auto generated_statement(TargetStmtValue value) noexcept -> TargetStmt {
    return {
        .value = std::move(value),
        .attribution = TargetGeneratedExpansionAttribution {
            .reason = TargetExpansionReason::LoweringSupport,
        },
    };
}

auto source_statement(
    const SemIRProgram& semantic,
    ProgramOriginID origin,
    TargetStmtValue value
) noexcept -> TargetStmt {
    return {
        .value = std::move(value),
        .attribution = TargetSourceOwnedAttribution {
            .origin = target_source_origin(semantic.provenance(), origin),
        },
    };
}

auto compiler_item(TargetItemValue value, TargetCompilerReason reason) noexcept -> TargetItem {
    return {
        .value = std::move(value),
        .attribution = TargetCompilerOwnedAttribution {.reason = reason},
    };
}

auto source_item(
    const SemIRProgram& semantic,
    ProgramOriginID origin,
    TargetItemValue value
) noexcept -> TargetItem {
    return {
        .value = std::move(value),
        .attribution = TargetSourceOwnedAttribution {
            .origin = target_source_origin(semantic.provenance(), origin),
        },
    };
}

auto expansion_item(
    const SemIRProgram& semantic,
    ProgramOriginID origin,
    TargetItemValue value
) noexcept -> TargetItem {
    return {
        .value = std::move(value),
        .attribution = TargetSourceExpansionAttribution {
            .origin = target_source_origin(semantic.provenance(), origin),
        },
    };
}

auto target_items(TargetItem item) noexcept -> std::vector<TargetItem> {
    auto result = std::vector<TargetItem>();
    result.push_back(std::move(item));
    return result;
}

auto namespace_item(
    std::optional<TargetName> name,
    std::vector<TargetItem> items,
    TargetCompilerReason reason
) noexcept -> TargetItem {
    return compiler_item(
        TargetNamespace {
            .name = std::move(name),
            .items = std::move(items),
        },
        reason
    );
}
