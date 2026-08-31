module carven:backend.lowering.decl.test.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :semantic.hir.decl;
import std;

auto lower_test_declaration(TargetModuleLowerer& context, TestID test_id) noexcept
    -> TargetItemValue {
    const auto& test = context.source().test(test_id);
    const auto& source_body = context.source().body(test.body);
    const auto function_name = context.name_allocator().fresh(TargetTemporaryNameKind::Test);
    const auto test_control = TargetControlDestinations::test();
    auto callable_lowerer =
        context.callable(source_body.scope, context.source().block(source_body.root).scope);
    auto body = lower_block(callable_lowerer, source_body.root, test_control);
    const auto function = context.target().append_item({
        .value = TargetDecl {TargetFunctionDecl {
            .name = TargetName {function_name},
            .parameters = {},
            .result = intrinsic_type(context, TargetSymbol::Void),
            .body = std::move(body),
            .declaration_only = false,
            .inline_specifier = false,
        }},
        .attribution = {
            .kind = TargetAttributionKind::CompilerOwned,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::TestHarness,
        },
    });
    const auto module_name = context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::string(context.source()
                                         .provenance()
                                         .module_record(context.active_module_id())
                                         .path.value()),
                .kind = TargetStringLiteralKind::String,
            },
        },
    });
    const auto case_name = context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::string(context.source().provenance().spelling(test.name)),
                .kind = TargetStringLiteralKind::String,
            },
        },
    });
    const auto registrar_type = intrinsic_type(context, TargetSymbol::TestingRegistrar);
    const auto registration_value = context.target().append_expression({
        .value = TargetConstructionExpr {
            .type = registrar_type,
            .initializer = std::vector<TargetExprID> {
                module_name,
                case_name,
                name_expression(context, TargetName {function_name}),
            },
        },
    });
    const auto registration = context.target().append_item({
        .value = TargetDecl {TargetVariableDecl {
            .type = registrar_type,
            .name =
                TargetName {
                    context.name_allocator().fresh(TargetTemporaryNameKind::TestRegistration),
                },
            .initializer = registration_value,
            .inline_specifier = false,
            .constexpr_specifier = false,
        }},
        .attribution = {
            .kind = TargetAttributionKind::CompilerOwned,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::TestHarness,
        },
    });
    return TargetItemGroup {
        .items = {function, registration},
        .separation = TargetVerticalSeparation::BlankLine,
    };
}
