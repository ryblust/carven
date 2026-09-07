module carven:backend.lowering.decl.testing.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.lowering.decl;
import :backend.lowering.decl.lowerer;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace decl_lowering {

auto context_member_call(std::string_view member, std::vector<TargetExpr> arguments) noexcept
    -> TargetExpr {
    return call_expression(
        member_expression(
            name_expression(TargetNameAllocator::test_context()),
            TargetIdentifier::from_spelling(member)
        ),
        std::move(arguments)
    );
}

auto lower_test(ModuleLowering& context, TestID id) noexcept -> TargetItem {
    const auto& test = context.semantic().tests().test(id);
    const auto& body = context.semantic().bodies().body(test.body);
    if (!body.inputs().parameters.empty() || !body.inputs().captures.empty()) {
        invariant_violation("test body unexpectedly has callable inputs");
    }
    auto parameters = target_parameters({
        .name = TargetNameAllocator::test_context(),
        .type = context.reference_type(context.intrinsic_type(TargetSymbol::TestingContext)),
    });
    auto lowered = lower_body(
        context,
        test.body,
        TargetBodyInputs {
            .parameters = {},
            .captures = {},
            .exit = TargetTestBodyExit {},
        }
    );
    if (!lowered.uses_test_context) {
        parameters.front().name.reset();
    }
    return source_item(
        context.semantic(),
        test.origin,
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {context.names().test_function(id)},
            .parameters = std::move(parameters),
            .result = context.intrinsic_type(TargetSymbol::Void),
            .form =
                TargetFreeFunctionDefinition {
                    .body = std::move(lowered.statements),
                },
            .static_specifier = false,
            .inline_specifier = false,
        }}
    );
}

auto lower_module_test_runner(ModuleLowering& context, std::span<const TestID> tests) noexcept
    -> TargetItem {
    auto body = std::vector<TargetStmt>();
    const auto provenance_module =
        context.semantic().declarations().module_decl(context.active_module()).provenance_module;
    const auto module_name =
        std::string(context.semantic().provenance().module_record(provenance_module).path.value());
    for (const auto id : tests) {
        const auto& test = context.semantic().tests().test(id);
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = context_member_call(
                    "begin_case",
                    target_expressions(
                        string_expression(module_name, TargetStringLiteralKind::String),
                        string_expression(
                            std::string(context.semantic().provenance().spelling(test.name)),
                            TargetStringLiteralKind::String
                        )
                    )
                ),
            }
        ));
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = call_expression(
                    name_expression(context.names().test_function(id)),
                    target_expressions(name_expression(TargetNameAllocator::test_context()))
                ),
            }
        ));
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = context_member_call("end_case", {}),
            }
        ));
    }
    return compiler_item(
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {context.names().module_runner(context.active_module())},
            .parameters = target_parameters({
                .name = TargetNameAllocator::test_context(),
                .type =
                    context.reference_type(context.intrinsic_type(TargetSymbol::TestingContext)),
            }),
            .result = context.intrinsic_type(TargetSymbol::Void),
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .static_specifier = false,
            .inline_specifier = false,
        }},
        TargetCompilerReason::TestHarness
    );
}

auto first_module(const SemIRProgram& semantic) noexcept -> ModuleID {
    for (const auto module_record : semantic.declarations().modules()) {
        return module_record.id;
    }
    invariant_violation("target artifact lowering requires at least one semantic module");
}

auto process_entry(
    ModuleLowering& context,
    bool accepts_arguments,
    std::vector<TargetStmt> body
) noexcept -> TargetItem {
    auto parameters = std::vector<TargetParameter>();
    if (accepts_arguments) {
        const auto character = context.intrinsic_type(TargetSymbol::CChar, true);
        const auto character_pointer = context.pointer_type(character, true);
        const auto argument_vector = context.pointer_type(character_pointer);
        parameters = target_parameters(
            {.name = TargetNameAllocator::process_argument_count(),
             .type = context.intrinsic_type(TargetSymbol::Int)},
            {.name = TargetNameAllocator::process_argument_vector(), .type = argument_vector}
        );
    }
    return compiler_item(
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {TargetNameAllocator::process_entry()},
            .parameters = std::move(parameters),
            .result = context.intrinsic_type(TargetSymbol::Int),
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .static_specifier = false,
            .inline_specifier = false,
        }},
        TargetCompilerReason::ArtifactScaffolding
    );
}

} // namespace decl_lowering

using decl_lowering::context_member_call;
using decl_lowering::first_module;
using decl_lowering::process_entry;

auto lower_test_runner_header(
    ArtifactLowering& artifact,
    const TargetTestRunnerHeaderArtifact& schedule
) noexcept -> TargetUnitSections {
    auto context = artifact.module_context(first_module(artifact.semantic()));
    const auto testing_context = context.intrinsic_type(TargetSymbol::TestingContext);
    auto declarations = std::vector<TargetItem>();
    for (const auto module_id : schedule.module_runners) {
        auto module_items = std::vector<TargetItem>();
        module_items.push_back(compiler_item(
            TargetDecl {TargetFunctionDecl {
                .name = TargetName {artifact.plan().names().module_runner(module_id)},
                .parameters = target_parameters({
                    .name = TargetNameAllocator::test_context(),
                    .type = context.reference_type(testing_context),
                }),
                .result = context.intrinsic_type(TargetSymbol::Void),
                .form = TargetFreeFunctionDeclaration {},
                .static_specifier = false,
                .inline_specifier = false,
            }},
            TargetCompilerReason::TestHarness
        ));
        declarations.push_back(namespace_item(
            artifact.plan().names().module_names(module_id).qualified_namespace_name,
            std::move(module_items),
            TargetCompilerReason::TestHarness,
            false
        ));
    }
    const auto reporter = TargetIdentifier::from_spelling("reporter");
    auto body = std::vector<TargetStmt>();
    body.push_back(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .name = TargetNameAllocator::test_context(),
            .type = testing_context,
            .initializer = TargetExpr {
                .value = TargetConstructionExpr {
                    .type = testing_context,
                    .initializer = target_expressions(name_expression(reporter)),
                },
            },
        }
    ));
    for (const auto module_id : schedule.module_runners) {
        auto name = artifact.plan().names().module_names(module_id).qualified_namespace_name;
        name.append(artifact.plan().names().module_runner(module_id));
        body.push_back(generated_statement(
            TargetExprStmt {
                .expression = call_expression(
                    name_expression(std::move(name)),
                    target_expressions(name_expression(TargetNameAllocator::test_context()))
                ),
            }
        ));
    }
    body.push_back(generated_statement(
        TargetReturnStmt {
            .expression = context_member_call("result", {}),
        }
    ));
    auto testing_items = std::vector<TargetItem>();
    testing_items.push_back(compiler_item(
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {TargetIdentifier::from_spelling("run_generated_tests")},
            .parameters = target_parameters({
                .name = reporter,
                .type = context.intrinsic_type(TargetSymbol::TestingReporter),
                .default_value = intrinsic_expression(TargetSymbol::StdNullptr),
            }),
            .result = context.intrinsic_type(TargetSymbol::Int),
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .static_specifier = true,
            .inline_specifier = false,
        }},
        TargetCompilerReason::TestHarness
    ));
    declarations.push_back(namespace_item(
        TargetName::from_components({
            TargetIdentifier::from_spelling("carven"),
            TargetIdentifier::from_spelling("testing"),
        }),
        std::move(testing_items),
        TargetCompilerReason::TestHarness
    ));
    return {
        .preamble = {},
        .body = std::move(declarations),
        .epilogue = {},
    };
}

auto lower_test_entry(ArtifactLowering& artifact) noexcept -> TargetUnitSections {
    auto context = artifact.module_context(first_module(artifact.semantic()));
    auto body = std::vector<TargetStmt>();
    body.push_back(generated_statement(
        TargetReturnStmt {
            .expression = call_expression(
                name_expression(
                    TargetName::from_components({
                        TargetIdentifier::from_spelling("carven"),
                        TargetIdentifier::from_spelling("testing"),
                        TargetIdentifier::from_spelling("run_generated_tests"),
                    })
                ),
                {}
            ),
        }
    ));
    return {
        .preamble = {},
        .body = target_items(process_entry(context, false, std::move(body))),
        .epilogue = {},
    };
}
