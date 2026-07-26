module carven:backend.lowering.declarations.entry.impl;

import :backend.lowering.program;
import :backend.lowering.declarations;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.decl;
import std;

auto process_entry_arguments(TargetModuleLowerer& context) noexcept -> std::vector<TargetExprID> {
    return {
        name_expression(context, TargetName {TargetNameAllocator::process_argument_count()}),
        name_expression(context, TargetName {TargetNameAllocator::process_argument_vector()}),
    };
}

auto lower_process_entry(
    TargetModuleLowerer& context,
    bool accepts_arguments,
    std::vector<TargetStmtID> body
) noexcept -> TargetItemID {
    auto parameters = std::vector<TargetParameter> {};
    if (accepts_arguments) {
        const auto character = intrinsic_type(context, TargetSymbol::CChar, true);
        const auto character_pointer = context.target().intern_type({
            .value = TargetPointerType {.pointee = character},
            .const_qualified = true,
        });
        const auto argument_vector = context.target().intern_type({
            .value = TargetPointerType {.pointee = character_pointer},
            .const_qualified = false,
        });
        parameters = {
            {.name = TargetNameAllocator::process_argument_count(),
             .type = intrinsic_type(context, TargetSymbol::Int),
             .maybe_unused = false},
            {.name = TargetNameAllocator::process_argument_vector(),
             .type = argument_vector,
             .maybe_unused = false},
        };
    }
    return context.target().append_item({
        .value = TargetDecl {TargetFunctionDecl {
            .name = TargetName {TargetNameAllocator::process_entry()},
            .parameters = std::move(parameters),
            .result = intrinsic_type(context, TargetSymbol::Int),
            .body = std::move(body),
            .declaration_only = false,
            .inline_specifier = false,
            .constexpr_specifier = false,
        }},
        .attribution = {
            .kind = TargetAttributionKind::CompilerOwned,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::ArtifactScaffolding,
        },
    });
}

auto lower_entry_wrapper(
    TargetModuleLowerer& context,
    const HIRFunctionDecl& function,
    const TargetName& namespace_name
) noexcept -> TargetItemID {
    const auto with_arguments = function.entry_point == HIREntryPointKind::WithArguments;
    auto arguments = std::vector<TargetExprID>();
    if (with_arguments) {
        arguments.push_back(call_expression(
            context,
            name_expression(context, TargetSymbol::RuntimeEntryArgs),
            process_entry_arguments(context)
        ));
    }
    auto entry_name = namespace_name;
    entry_name.append(symbol_identifier(context, function.symbol));
    const auto call = call_expression(
        context,
        name_expression(context, std::move(entry_name)),
        std::move(arguments)
    );
    const auto call_statement =
        context.target().append_lowering_statement(TargetExprStmt {.expression = call});
    const auto zero = context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetIntegerLiteral {
                .negative = false,
                .magnitude = 0,
                .suffix = TargetIntegerSuffix::None,
            },
        },
    });
    const auto return_statement =
        context.target().append_lowering_statement(TargetReturnStmt {.expression = zero});
    return lower_process_entry(context, with_arguments, {call_statement, return_statement});
}
