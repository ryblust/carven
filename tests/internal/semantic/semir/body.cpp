module carven:test.internal.semantic.semir.body;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.semir.async;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.death;
import :test.harness.framework;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    require(result.has_value());
    return std::move(*result);
}

struct PreparedFunction final {
    ProgramDraft builder;
    CallableID callable;
    ProgramOriginID origin;
    ProgramSpellingID parameter_name;
    TypeID boolean_type;
};

auto prepare_function(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> PreparedFunction {
    const auto source = sources.append_virtual("semir-function-body.cv", "");
    require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = path("semir.function_body"),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value());
    auto builder = ProgramDraft::begin(std::move(*syntax), diagnostics, HostEffects::Deny);
    const auto provenance_module = builder.provenance_module_at(0uz);
    const auto source_id = builder.module_source(provenance_module);
    const auto origin = builder.append_source_origin(source_id, Span::at(0u));
    const auto parameter_name = builder.intern_spelling("value");
    const auto boolean = builder.builtin_type(BuiltinType::Bool);
    const auto module_id = builder.reserve_module_declaration();
    const auto function = builder.reserve_function_declaration();
    const auto callable = builder.reserve_callable_declaration();
    const auto failures = builder.add_empty_failure_term();
    builder.define_callable_contract(
        callable,
        ConstructionCallableContract {
            .execution = CallableExecutionKind::Synchronous,
            .parameters =
                {
                    ConstructionCallableParameter {
                        .stage = ParameterStage::Runtime,
                        .access = AccessMode::Read,
                        .type = boolean,
                    },
                },
            .result = boolean,
            .failures = failures,
            .policy = FailureContractPolicy::Declared,
        }
    );
    builder.define_declaration(
        function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("identity"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .callable = callable,
            .entry_point = std::nullopt,
            .cpp_export_origin = std::nullopt,
            .is_const = false,
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {ModuleItem {function}},
        }
    );
    builder.finish_declaration_heads();
    return PreparedFunction {
        .builder = std::move(builder),
        .callable = callable,
        .origin = origin,
        .parameter_name = parameter_name,
        .boolean_type = boolean,
    };
}

struct BodyFixture final {
    BodyBuilder builder;
    LifetimeRegionID lifetime;
    LocalBindingID parameter;
    FailureTermID failures;
};

auto body_fixture(PreparedFunction& prepared) noexcept -> BodyFixture {
    auto reservation = prepared.builder.reserve_body(BodyKind::Function);
    prepared.builder.complete_callable(
        prepared.callable,
        FunctionBodyImplementation {.body = reservation.id()}
    );
    auto body = BodyBuilder(std::move(reservation), prepared.builder);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, prepared.origin);
    const auto parameter = body.add_parameter(
        prepared.parameter_name,
        prepared.boolean_type,
        lifetime,
        AccessMode::Read,
        prepared.origin
    );
    return BodyFixture {
        .builder = std::move(body),
        .lifetime = lifetime,
        .parameter = parameter.binding,
        .failures = prepared.builder.add_empty_failure_term()
    };
}

auto boolean_expression(PreparedFunction& prepared, const BodyFixture& body) noexcept
    -> SemanticExpression {
    return SemanticExpression {
        .type = BodyType(prepared.boolean_type),
        .lifetime = body.lifetime,
        .origin = prepared.origin,
        .constant = std::nullopt,
        .failures = BodyFailures(body.failures),
        .exits_call = false,
        .operation_reachable = true,
        .category = SemanticValueCategory::Value,
        .value = SemConstant {
            .constant = prepared.builder.intern_constant(
                {.type = prepared.boolean_type, .value = BooleanConstant {.value = true}}
            ),
            .retained_storage = false
        },
    };
}

auto finish_body(
    PreparedFunction& prepared,
    BodyFixture&& body,
    std::vector<SemanticStatement> statements,
    std::optional<SemanticExpression> result
) noexcept -> BodyID {
    const auto result_reachable = result.has_value();
    auto draft = std::move(body.builder)
                     .finish(
                         SemanticRegion {
                             .lifetime = body.lifetime,
                             .origin = prepared.origin,
                             .statements = std::move(statements),
                             .result = std::move(result),
                             .result_reachable = result_reachable,
                             .failures = BodyFailures(body.failures),
                             .exits_call = false,
                         }
                     );
    const auto id = draft.id;
    prepared.builder.add_body_draft(std::move(draft));
    return id;
}

enum class ResidualConstructionContract { AddedFact, ChangedWitness, ChangedTarget, ChangedAccess };

auto check_residual_construction(ResidualConstructionContract contract) noexcept -> void {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto argument = boolean_expression(prepared, body);
    const auto original = std::get<SemConstant>(argument.value).constant;
    if (contract == ResidualConstructionContract::ChangedWitness) {
        argument.constant = original;
    }
    const auto query_type = prepared.builder.intern_type({
        .value = CppTypeValue {
            .form = CppQueryType {
                .expression = CppConstructQuery {
                    .target = prepared.boolean_type,
                    .arguments = {{
                        .operand = {.type = prepared.boolean_type, .access = AccessMode::Read},
                        .constant = argument.constant,
                    }},
                },
            },
        },
    });
    auto construction = body.builder.make_expression(
        query_type,
        body.lifetime,
        prepared.origin,
        SemCpp {
            .operation = CppConstructOperation {.target = prepared.boolean_type},
            .operands = {{
                .access = AccessMode::Read,
                .expression = std::move(argument),
            }},
        }
    );
    auto graph =
        std::move(body.builder)
            .finish(
                SemanticRegion {
                    .lifetime = body.lifetime,
                    .origin = prepared.origin,
                    .statements = {{
                        .origin = prepared.origin,
                        .lifetime = body.lifetime,
                        .reachable = true,
                        .value = SemExpressionStatement {.expression = std::move(construction)},


                    }},
                    .result = boolean_expression(prepared, body),
                    .result_reachable = true,
                    .failures = BodyFailures(body.failures),
                    .exits_call = false,


                }
            );
    auto residual = graph.region;
    auto& operation = std::get<SemCpp>(
        std::get<SemExpressionStatement>(residual.statements.front().value).expression.value
    );
    auto& operand = operation.operands.front();
    operand.expression.constant = original;
    if (contract == ResidualConstructionContract::ChangedWitness) {
        const auto replacement = prepared.builder.intern_constant({
            .type = prepared.boolean_type,
            .value = BooleanConstant {.value = false},
        });
        operand.expression.constant = replacement;
        operand.expression.value = SemConstant {.constant = replacement, .retained_storage = false};
    } else if (contract == ResidualConstructionContract::ChangedTarget) {
        std::get<CppConstructOperation>(operation.operation).target =
            prepared.builder.builtin_type(BuiltinType::I32);
    } else if (contract == ResidualConstructionContract::ChangedAccess) {
        operand.access = AccessMode::Write;
    }
    graph.residual = std::move(residual);
    prepared.builder.add_body_draft(std::move(graph));
    if (contract == ResidualConstructionContract::AddedFact) {
        expect(std::move(prepared.builder).finish().has_value());
        expect(diagnostics.empty());
    } else {
        expect_termination([&] noexcept {
            static_cast<void>(std::move(prepared.builder).finish());
        });
    }
}

const TestSuite suite([] static noexcept {
    "SemIR body: residual C++ construction can establish additional scalar facts"_test =
        [] static noexcept {
            check_residual_construction(ResidualConstructionContract::AddedFact);
        };
    "SemIR body: residual C++ construction preserves its checked scalar witness"_test =
        termination_test([] static noexcept {
            check_residual_construction(ResidualConstructionContract::ChangedWitness);
        });
    "SemIR body: residual C++ construction preserves its checked target"_test =
        termination_test([] static noexcept {
            check_residual_construction(ResidualConstructionContract::ChangedTarget);
        });
    "SemIR body: residual C++ construction preserves its checked operand access"_test =
        termination_test([] static noexcept {
            check_residual_construction(ResidualConstructionContract::ChangedAccess);
        });
    "SemIR body: publication preserves structured parameters and result"_test = [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto prepared = prepare_function(sources, diagnostics);
        auto body = body_fixture(prepared);
        const auto parameter = body.parameter;
        auto result = boolean_expression(prepared, body);
        result.category = SemanticValueCategory::Place;
        result.value = SemBinding {.binding = parameter};
        const auto body_id = finish_body(prepared, std::move(body), {}, std::move(result));
        auto finished = std::move(prepared.builder).finish();
        if (!expect(finished.has_value())) {
            return;
        }
        const auto program = std::move(*finished);
        const auto& published = program.bodies().body(body_id);
        expect(published.inputs().parameters == std::vector {parameter});
        if (!expect(published.region().result.has_value())) {
            return;
        }
        const auto* binding = std::get_if<SemBinding>(&published.region().result->value);
        if (!expect(binding != nullptr)) {
            return;
        }
        expect(((binding->binding) == (parameter))).note("binding->binding == parameter");
        expect(((program.declarations().body_for_callable(prepared.callable)) == (body_id)))
            .note("program.declarations().body_for_callable(prepared.callable) == body_id");
        expect(diagnostics.empty());
    };

    "SemIR body: test operations carry an internal exit through ordinary functions"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto prepared = prepare_function(sources, diagnostics);
            auto body = body_fixture(prepared);
            auto operation = boolean_expression(prepared, body);
            operation.constant.reset();
            operation.exits_call = true;
            operation.value = SemReport {
                .kind = ReportKind::Fail,
                .condition = std::nullopt,
                .message = std::nullopt,
                .condition_source = std::nullopt,
                .operand_sources = std::nullopt
            };
            auto statements = std::vector<SemanticStatement>();
            statements.push_back({
                .origin = prepared.origin,
                .lifetime = body.lifetime,
                .reachable = true,
                .value = SemExpressionStatement {std::move(operation)},
            });
            auto result = boolean_expression(prepared, body);
            [[maybe_unused]] const auto resolved =
                finish_body(prepared, std::move(body), std::move(statements), std::move(result));
            const auto program = std::move(prepared.builder).finish();
            if (!expect(program.has_value())) {
                return;
            }
            expect(program->may_stop(prepared.callable));
        };

    "SemIR body: resolved nested regions can be delivered"_test = [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto prepared = prepare_function(sources, diagnostics);
        auto body = body_fixture(prepared);
        const auto completed = prepared.builder.intern_failure_set({});
        auto region = SemanticRegion {
            .lifetime = body.lifetime,
            .origin = prepared.origin,
            .statements = {},
            .result = std::nullopt,
            .result_reachable = false,
            .failures = BodyFailures(completed),
            .exits_call = false,
        };
        auto attempt = boolean_expression(prepared, body);
        attempt.failures = BodyFailures(completed);
        attempt.value = SemTry {
            .body = OwnedSemanticRegion(std::move(region)),
            .protected_failures = BodyFailures(completed),
            .residual_failures = BodyFailures(completed),
            .arms = {},
        };
        auto draft = std::move(body.builder)
                         .finish(
                             SemanticRegion {
                                 .lifetime = body.lifetime,
                                 .origin = prepared.origin,
                                 .statements = {},
                                 .result = std::move(attempt),
                                 .result_reachable = true,
                                 .failures = BodyFailures(completed),
                                 .exits_call = false,
                             }
                         );
        const auto identity = draft.lifetime_regions.owner();
        const auto finalized = SemIRBody({
            .id = draft.id,
            .kind = draft.kind,
            .provenance_identity = draft.provenance_identity,
            .inputs = {},
            .lifetime_regions = std::move(draft.lifetime_regions),
            .bindings = MutableBodyTable<LocalBinding, LocalBindingID>(identity).seal(),
            .patterns = MutableBodyTable<Pattern, PatternID>(identity).seal(),
            .region = std::move(draft.region),
            .residual = std::nullopt,
            .specialized = std::nullopt,
            .generic_definition = std::nullopt,
        });
        expect(finalized.identity() == identity);
    };

    "SemIR body: publication accepts values with or without constant facts"_test =
        [] static noexcept {
            for (const auto has_fact : {false, true}) {
                auto sources = SourceManager();
                auto diagnostics = DiagnosticSink();
                auto prepared = prepare_function(sources, diagnostics);
                auto body = body_fixture(prepared);
                auto expression = boolean_expression(prepared, body);
                if (has_fact) {
                    expression.constant = std::get<SemConstant>(expression.value).constant;
                }
                static_cast<void>(
                    finish_body(prepared, std::move(body), {}, std::move(expression))
                );
                expect(std::move(prepared.builder).finish().has_value())
                    .note("constant fact: ", has_fact);
                expect(diagnostics.empty());
            }
        };
});

} // namespace
