module carven:test.internal.semantic.semir.body;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
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
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    ct::require(result.has_value());
    return std::move(*result);
}

struct PreparedFunction final {
    ProgramDraft builder;
    CallableID callable;
    ModuleID module_id;
    ProgramOriginID origin;
    ProgramSpellingID parameter_name;
    TypeID boolean_type;
};

auto prepare_function(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> PreparedFunction {
    const auto source = sources.append_virtual("semir-function-body.cv", "");
    ct::require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = path("semir.function_body"),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    ct::require(syntax.has_value());
    auto builder = ProgramDraft::begin(std::move(*syntax), diagnostics);
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
        .module_id = module_id,
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
        .exits_test = false,
        .operation_reachable = true,
        .category = SemanticValueCategory::Value,
        .value = SemConstant {
            .constant = prepared.builder.intern_constant(
                {.type = prepared.boolean_type, .value = BooleanConstant {.value = true}}
            )
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
                             .exits_test = false,
                         }
                     );
    const auto id = draft.id;
    prepared.builder.add_body_draft(std::move(draft));
    return id;
}

template<typename MakeExpression>
auto rejects_expression(std::string_view scenario, MakeExpression make_expression) noexcept
    -> bool {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto prepared = prepare_function(sources, diagnostics);
    auto body = body_fixture(prepared);
    auto invalid = std::invoke(make_expression, prepared, body);
    auto statements = std::vector<SemanticStatement>();
    statements.push_back(
        SemanticStatement {
            .origin = prepared.origin,
            .lifetime = body.lifetime,
            .reachable = true,
            .value = SemExpressionStatement {.expression = std::move(invalid)},
        }
    );
    auto result = boolean_expression(prepared, body);
    [[maybe_unused]] const auto resolved =
        finish_body(prepared, std::move(body), std::move(statements), std::move(result));
    return expect_termination(scenario, [&] noexcept {
        static_cast<void>(std::move(prepared.builder).finish());
    });
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
            .operands = {{.access = AccessMode::Read, .expression = std::move(argument)}},
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
                    .exits_test = false,
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
        operand.expression.value = SemConstant {.constant = replacement};
    } else if (contract == ResidualConstructionContract::ChangedTarget) {
        std::get<CppConstructOperation>(operation.operation).target =
            prepared.builder.builtin_type(BuiltinType::I32);
    } else if (contract == ResidualConstructionContract::ChangedAccess) {
        operand.access = AccessMode::Write;
    }
    graph.residual = std::move(residual);
    prepared.builder.add_body_draft(std::move(graph));
    if (contract == ResidualConstructionContract::AddedFact) {
        ct::expect(std::move(prepared.builder).finish().has_value());
        ct::expect(diagnostics.empty());
    } else {
        ct::expect(expect_termination(
            std::format("semir-residual-construction-{}", std::to_underlying(contract)),
            [&] noexcept { static_cast<void>(std::move(prepared.builder).finish()); }
        ));
    }
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "SemIR body: residual C++ construction can establish additional scalar facts",
        [] static noexcept { check_residual_construction(ResidualConstructionContract::AddedFact); }
    );
    ct::test(
        "SemIR body: residual C++ construction preserves checked scalar witnesses",
        [] static noexcept {
            check_residual_construction(ResidualConstructionContract::ChangedWitness);
        }
    );
    ct::test(
        "SemIR body: residual C++ construction preserves its checked target",
        [] static noexcept {
            check_residual_construction(ResidualConstructionContract::ChangedTarget);
        }
    );
    ct::test("SemIR body: residual C++ construction preserves operand access", [] static noexcept {
        check_residual_construction(ResidualConstructionContract::ChangedAccess);
    });
    ct::test(
        "SemIR body: publication preserves structured parameters and result",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto prepared = prepare_function(sources, diagnostics);
            auto body = body_fixture(prepared);
            const auto parameter = body.parameter;
            auto result = boolean_expression(prepared, body);
            result.category = SemanticValueCategory::Place;
            result.value = SemBinding {.binding = parameter};
            [[maybe_unused]] const auto resolved =
                finish_body(prepared, std::move(body), {}, std::move(result));
            const auto body_id = resolved;
            auto finished = std::move(prepared.builder).finish();
            if (!ct::expect(finished.has_value())) {
                return;
            }
            const auto program = std::move(*finished);
            const auto& published = program.bodies().body(body_id);
            ct::expect(published.inputs().parameters == std::vector {parameter});
            if (!ct::expect(published.region().result.has_value())) {
                return;
            }
            const auto* binding = std::get_if<SemBinding>(&published.region().result->value);
            if (!ct::expect(binding != nullptr)) {
                return;
            }
            ct::expect(((binding->binding) == (parameter))).note("binding->binding == parameter");
            ct::expect(((program.declarations().body_for_callable(prepared.callable)) == (body_id)))
                .note("program.declarations().body_for_callable(prepared.callable) == body_id");
            ct::expect(diagnostics.empty());
        }
    );

    ct::test("SemIR body: result agrees with callable contract", [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto prepared = prepare_function(sources, diagnostics);
        auto body = body_fixture(prepared);
        [[maybe_unused]] const auto resolved =
            finish_body(prepared, std::move(body), {}, std::nullopt);
        ct::expect(expect_termination("structured-result-contract", [&] noexcept {
            static_cast<void>(std::move(prepared.builder).finish());
        }));
    });

    ct::test("SemIR body: unary operations reject incompatible result types", [] static noexcept {
        ct::expect(rejects_expression(
            "structured-unary-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto operand = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemUnary {
                    .operation = UnaryOperator::Negate,
                    .operand = OwnedSemanticExpression(std::move(operand))
                };
                return result;
            }
        ));
    });

    ct::test("SemIR body: binary operations reject incompatible operand types", [] static noexcept {
        ct::expect(rejects_expression(
            "structured-binary-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto left = boolean_expression(prepared, body);
                auto right = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemBinary {
                    .left = OwnedSemanticExpression(std::move(left)),
                    .operation = BinaryOperator::Add,
                    .right = OwnedSemanticExpression(std::move(right))
                };
                return result;
            }
        ));
    });

    ct::test("SemIR body: casts reject incompatible operand types", [] static noexcept {
        ct::expect(rejects_expression(
            "structured-cast-contract",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto operand = boolean_expression(prepared, body);
                auto result = boolean_expression(prepared, body);
                result.value = SemCast {
                    .operand = OwnedSemanticExpression(std::move(operand)),
                    .kind = CastKind::IntegerToInteger
                };
                return result;
            }
        ));
    });

    ct::test("SemIR body: body-local references reject foreign owners", [] static noexcept {
        auto first_sources = SourceManager();
        auto second_sources = SourceManager();
        auto first_diagnostics = DiagnosticSink();
        auto second_diagnostics = DiagnosticSink();
        auto first = prepare_function(first_sources, first_diagnostics);
        auto second = prepare_function(second_sources, second_diagnostics);
        auto first_body = body_fixture(first);
        const auto second_body = body_fixture(second);
        auto result = boolean_expression(first, first_body);
        result.value = SemBinding {.binding = second_body.parameter};
        result.category = SemanticValueCategory::Place;
        [[maybe_unused]] const auto resolved =
            finish_body(first, std::move(first_body), {}, std::move(result));
        ct::expect(expect_termination("structured-foreign-binding", [&] noexcept {
            static_cast<void>(std::move(first.builder).finish());
        }));
    });

    ct::test(
        "SemIR body: test operations carry an internal exit through ordinary functions",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto prepared = prepare_function(sources, diagnostics);
            auto body = body_fixture(prepared);
            auto operation = boolean_expression(prepared, body);
            operation.constant.reset();
            operation.exits_test = true;
            operation.value = SemReport {
                .kind = ReportKind::Fail,
                .condition = std::nullopt,
                .message = std::nullopt,
                .condition_source = std::nullopt,
                .operand_sources = std::nullopt
            };
            auto statements = std::vector<SemanticStatement>();
            statements.push_back(
                {.origin = prepared.origin,
                 .lifetime = body.lifetime,
                 .reachable = true,
                 .value = SemExpressionStatement {std::move(operation)}}
            );
            auto result = boolean_expression(prepared, body);
            [[maybe_unused]] const auto resolved =
                finish_body(prepared, std::move(body), std::move(statements), std::move(result));
            const auto program = std::move(prepared.builder).finish();
            if (!ct::expect(program.has_value())) {
                return;
            }
            ct::expect(program->may_stop_test(prepared.callable));
        }
    );

    ct::test("SemIR body: external calls require their declared result query", [] static noexcept {
        ct::expect(rejects_expression(
            "semir-cpp-call-result",
            [](PreparedFunction& prepared, const BodyFixture& body) static noexcept {
                auto expression = boolean_expression(prepared, body);
                expression.value = SemCppCall {
                    .callee =
                        CppNameReference {
                            .context_module = prepared.module_id,
                            .lookup = CppNameLookup::Global,
                            .components = {"native", "value"}
                        },
                    .arguments = {}
                };
                return expression;
            }
        ));
    });

    ct::test("SemIR body: every nested fact is resolved before delivery", [] static noexcept {
        const auto scenarios = std::array<std::string_view, 4> {
            "completed tree",
            "nested region",
            "catch metadata",
            "expression type"
        };
        ct::each(scenarios, std::identity {}, [](const auto& scenario) static noexcept {
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
                .exits_test = false,
            };
            auto attempt = boolean_expression(prepared, body);
            attempt.failures = BodyFailures(completed);
            attempt.value = SemTry {
                .body = OwnedSemanticRegion(std::move(region)),
                .protected_failures = BodyFailures(completed),
                .residual_failures = BodyFailures(completed),
                .arms = {},
            };
            auto* nested = std::get_if<SemTry>(&attempt.value);
            if (!ct::expect(nested != nullptr)) {
                return;
            }
            if (scenario == "nested region") {
                nested->body->failures = BodyFailures(body.failures);
            }
            if (scenario == "catch metadata") {
                nested->residual_failures = BodyFailures(body.failures);
            }
            if (scenario == "expression type") {
                attempt.type = BodyType(prepared.builder.append_construction_type(
                    {.value = ConstructionArrayTypeValue {
                         .element = prepared.boolean_type,
                         .extent = 1u
                     }}
                ));
            }
            auto draft = std::move(body.builder)
                             .finish(
                                 SemanticRegion {
                                     .lifetime = body.lifetime,
                                     .origin = prepared.origin,
                                     .statements = {},
                                     .result = std::move(attempt),
                                     .result_reachable = true,
                                     .failures = BodyFailures(completed),
                                     .exits_test = false,
                                 }
                             );
            const auto identity = draft.lifetime_regions.owner();
            auto deliver = [&] noexcept {
                return SemIRBody({
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
                });
            };
            if (scenario != "completed tree") {
                ct::expect(expect_termination(scenario, [&] noexcept {
                    static_cast<void>(deliver());
                })).note(scenario);
            } else {
                const auto finalized = deliver();
                ct::expect(((finalized.identity()) == (identity)))
                    .note("finalized.identity() == identity", scenario);
            }
        });
    });

    ct::test(
        "SemIR body: normal-completion facts match the expression type and program",
        [] static noexcept {
            enum class Fact { Missing, Boolean, WrongType, Foreign };
            const auto cases =
                std::array {Fact::Missing, Fact::Boolean, Fact::WrongType, Fact::Foreign};
            ct::each(
                cases,
                [](Fact fact) static noexcept {
                    return std::format("fact {}", std::to_underlying(fact));
                },
                [&](const auto& scenario) noexcept {
                    auto sources = SourceManager();
                    auto diagnostics = DiagnosticSink();
                    auto prepared = prepare_function(sources, diagnostics);
                    auto body = body_fixture(prepared);
                    auto expression = boolean_expression(prepared, body);
                    auto foreign_sources = SourceManager();
                    auto foreign_diagnostics = DiagnosticSink();
                    auto foreign = prepare_function(foreign_sources, foreign_diagnostics);
                    if (scenario == Fact::Boolean) {
                        expression.constant = std::get<SemConstant>(expression.value).constant;
                    } else if (scenario == Fact::WrongType) {
                        expression.constant = prepared.builder.intern_constant(
                            {.type = prepared.builder.builtin_type(BuiltinType::I32),
                             .value = IntegerConstant::zero()}
                        );
                    } else if (scenario == Fact::Foreign) {
                        expression.constant = foreign.builder.intern_constant(
                            {.type = foreign.boolean_type, .value = BooleanConstant {.value = true}}
                        );
                    }
                    const auto publish = [&]() noexcept {
                        static_cast<void>(
                            finish_body(prepared, std::move(body), {}, std::move(expression))
                        );
                        return std::move(prepared.builder).finish();
                    };
                    if (scenario == Fact::Missing || scenario == Fact::Boolean) {
                        ct::expect(publish().has_value())
                            .note("scenario = ", static_cast<int>(scenario));
                    } else {
                        const auto death_scenario = scenario == Fact::WrongType
                            ? "expression-fact-wrong-type"
                            : "expression-fact-foreign-program";
                        ct::expect(expect_termination(death_scenario, publish))
                            .note("scenario = ", static_cast<int>(scenario));
                    }
                }
            );
        }
    );

    ct::test("SemIR body: match rejection facts require coverage", [] static noexcept {
        ct::expect(rejects_expression(
            "match-selection-fact",
            [](PreparedFunction& prepared, BodyFixture& body) static noexcept {
                auto subject = boolean_expression(prepared, body);
                const auto pattern = body.builder.add_pattern({
                    .type = prepared.boolean_type,
                    .value =
                        LiteralPattern {
                            .constant = prepared.builder.intern_constant(
                                {.type = prepared.boolean_type,
                                 .value = BooleanConstant {.value = false}}
                            )
                        },
                    .origin = prepared.origin,
                });
                auto arms = std::vector<SemMatchArm>();
                arms.push_back({
                    .pattern = pattern,
                    .bindings = {},
                    .guard = std::nullopt,
                    .body =
                        SemanticRegion {
                            .lifetime = body.lifetime,
                            .origin = prepared.origin,
                            .statements = {},
                            .result = boolean_expression(prepared, body),
                            .result_reachable = true,
                            .failures = BodyFailures(body.failures),
                            .exits_test = false,
                        },
                    .reachable = true,
                    .pattern_may_reject = false,
                    .pattern_bounds = {},
                });
                auto result = boolean_expression(prepared, body);
                result.value = SemMatch {
                    .subject = OwnedSemanticExpression(std::move(subject)),
                    .subject_is_place = false,
                    .arms = std::move(arms),
                };
                return result;
            }
        ));
    });
});

} // namespace
