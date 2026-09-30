module carven:test.internal.backend.generation.evaluation;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :semantic.semir.body;
import :semantic.semir.content;
import :semantic.semir.evaluation;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: only potentially failing SIMD controls carry report sites",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view expression;
                std::size_t sites;
            };
            const auto cases = std::array {
                Case {
                    .name = "proven lanes",
                    .expression = "v.with_lane(15, 7).lane(15)",
                    .sites = 0uz
                },
                Case {.name = "dynamic lane", .expression = "v.lane(index)", .sites = 1uz},
                Case {.name = "invalid lane", .expression = "v.lane(16)", .sites = 1uz},
                Case {
                    .name = "invalid update",
                    .expression = "v.with_lane(16, 7).lane(0)",
                    .sites = 1uz
                },
            };
            ct::each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(
                        std::format(
                            "fn probe(v: u8x16, index: usize) -> u8 {{ return {}; }}",
                            input.expression
                        )
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("simd_sites")}
                );
                struct Query final {
                    std::size_t sites;
                    auto enter_expression(
                        const TargetExpr& expression,
                        TargetExpressionRole
                    ) noexcept -> bool {
                        if (const auto* name =
                                std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                            sites += name->symbol == TargetSymbol::RuntimeSourceSite;
                        }
                        return true;
                    }
                };
                auto query = Query {.sites = 0uz};
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    ct::expect(traverse_target_unit(unit.sections(), query));
                }
                ct::expect_equal(query.sites, input.sites);
            });
        }
    );

    ct::test("Generation: precomputed formatting retains effects", [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(
                "fn touch() -> bool { return true; }\n"
                "fn known() -> String { const version = 42; return f\"build-{version:04}\"; }\n"
                "fn effects() { f\"{touch() && false}\"; }\n"
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("precomputed_format")}
        );

        struct Query final {
            std::size_t formats;
            std::size_t effects;

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                if (const auto* name = std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                    formats += name->symbol == TargetSymbol::RuntimeFormat
                        || name->symbol == TargetSymbol::RuntimeFormatValidUTF8;
                }
                if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                    if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                        effects += name->name.components().back().spelling() == "touch";
                    }
                }
                return true;
            }
        };

        auto query = Query {.formats = 0uz, .effects = 0uz};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            ct::expect(traverse_target_unit(unit.sections(), query));
        }
        ct::expect_equal(query.formats, 0uz);
        ct::expect_equal(query.effects, 1uz);
    });

    ct::test(
        "Generation: mixed formatting passes only residual values after required effects",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn touch() -> bool { return true; } "
                    "fn format(value: f64, width: i32, precision: i32) -> String { "
                    "return f\"{42:04}/{touch() && false}/{value:{width}.{precision}f}/{7}\"; }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("mixed_format")}
            );

            struct Query final {
                std::size_t formats;
                std::size_t effects;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call == nullptr) {
                        return true;
                    }
                    if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                        effects += name->name.components().back().spelling() == "touch";
                    }
                    if (const auto* intrinsic =
                            std::get_if<TargetIntrinsicNameExpr>(&call->callee->value);
                        intrinsic != nullptr && intrinsic->symbol == TargetSymbol::RuntimeFormat) {
                        ++formats;
                    }
                    return true;
                }
            };

            auto query = Query {.formats = 0uz, .effects = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.formats == 1uz);
            ct::expect(query.effects == 1uz);
        }
    );

    ct::test(
        "Evaluation: known results retain source operations and execution obligations",
        [] static noexcept {
            const auto semantic = analyze_test_program(
                "fn touch(&n: i32) -> bool { n += 1; return true; }\n"
                "fn probe(&n: i32) {\n"
                "  let _ = 1 < 2;\n"
                "  let _ = false && touch(&n);\n"
                "  let _ = touch(&n) && false;\n"
                "  let _ = 1 / n;\n"
                "}\n"
                "fn floats(a: f32) { let _ = a + 1.0f32; let _ = 1 as f32; }\n"
            );
            auto logic_count = 0uz;
            auto comparison_count = 0uz;
            auto checked_count = 0uz;
            auto floating_count = 0uz;
            for (const auto entry : semantic.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (const auto* binary = std::get_if<SemBinary>(&expression.value)) {
                            if (binary->operation == BinaryOperator::Less) {
                                ++comparison_count;
                                ct::expect(known_boolean(semantic, expression) == true);
                                ct::expect(
                                    evaluation_rule(semantic, expression).action
                                    == EvaluationAction::Operands
                                );
                            }
                            if (binary->operation == BinaryOperator::Divide) {
                                ++checked_count;
                                ct::expect(
                                    evaluation_rule(semantic, expression).action
                                    == EvaluationAction::Required
                                );
                            }
                            if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                                    &semantic.types().type(expression.type.resolved()).value
                                );
                                builtin != nullptr && builtin->kind == BuiltinType::F32) {
                                ++floating_count;
                                ct::expect(
                                    evaluation_rule(semantic, expression).action
                                    == EvaluationAction::Required
                                );
                            }
                        }
                        if (const auto* cast = std::get_if<SemCast>(&expression.value);
                            cast != nullptr && cast->kind == CastKind::IntegerToFloating) {
                            ++floating_count;
                            ct::expect(
                                evaluation_rule(semantic, expression).action
                                == EvaluationAction::Required
                            );
                        }
                        if (const auto* logic = std::get_if<SemShortCircuit>(&expression.value)) {
                            ++logic_count;
                            const auto skipped = known_boolean(semantic, *logic->left) == false;
                            ct::expect(
                                known_boolean(semantic, expression)
                                == (skipped ? std::optional(false) : std::nullopt)
                            );
                            ct::expect(
                                (evaluation_rule(semantic, expression).operands[1] == nullptr)
                                == skipped
                            );
                        }
                    }
                );
            }
            ct::expect(comparison_count == 1uz);
            ct::expect(logic_count == 2uz);
            ct::expect(checked_count == 1uz);
            ct::expect(floating_count == 2uz);
        }
    );

    ct::test(
        "Generation: proven scalar results require no computation or discard scaffolding",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "enum Error { E1, E2, }\n"
                    "fn foo(a: i32) -> i32 throw Error {\n"
                    "  if 1 < 2 { return 3 + a; } else { throw Error::E1; }\n"
                    "}\n"
                    "fn folded() -> i32 { return 1 + 2; }\n"
                    "fn skipped() -> i32 { if false && (folded() == 3) { return 0; } return 2; }\n"
                    "struct Value { field: i32, }\n"
                    "fn skipped_object() -> bool { return false && (Value { field: 1 }.field == 1); }\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("evaluation")}
            );

            struct Query final {
                std::size_t calls;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    ct::expect(!(std::holds_alternative<TargetStaticCastExpr>(expression.value)));
                    ct::expect(!(std::holds_alternative<TargetBinaryExpr>(expression.value)));
                    ct::expect(!(std::holds_alternative<TargetConstructionExpr>(expression.value)));
                    calls += std::holds_alternative<TargetCallExpr>(expression.value);
                    return true;
                }

                auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetBlockStmt>(statement.value)));
                    ct::expect(!(std::holds_alternative<TargetDiscardStmt>(statement.value)));
                    ct::expect(!(std::holds_alternative<TargetIfStmt>(statement.value)));
                    ct::expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                    return true;
                }
            };

            auto query = Query {.calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.calls == 2uz);
        }
    );

    ct::test(
        "Generation: native branches and calls need no enclosing artificial block",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn identity(n: i32) -> i32 { return n; }\n"
                    "fn branch(flag: bool, n: i32) -> i32 {\n"
                    "  if flag { return identity(n); } else { return 0; }\n"
                    "}\n"
                    "fn effect() {}\n"
                    "fn invoke() { effect(); }\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("native_structure")}
            );

            struct Query final {
                std::size_t branches;
                std::size_t calls;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetBlockStmt>(statement.value)));
                    ct::expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                    branches += std::holds_alternative<TargetIfStmt>(statement.value);
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    ct::expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                    calls += std::holds_alternative<TargetCallExpr>(expression.value);
                    return true;
                }
            };

            auto query = Query {.branches = 0uz, .calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.branches == 1uz);
            ct::expect(query.calls == 2uz);
        }
    );

    ct::test(
        "Generation: discarded failing calls check success without projecting a payload",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "enum Error { Failed, }\n"
                    "fn produce(flag: bool) -> i32 throw Error {\n"
                    "  if flag { return 7; } else { throw Error::Failed; }\n"
                    "}\n"
                    "fn discard(flag: bool) throw Error { produce(flag)?; }\n"
                    "fn discard_wrapped(flag: bool) throw Error { (produce(flag)? as i32) == 0; }\n"
                    "fn discard_selected(flag: bool) throw Error { flag && (produce(flag)? == 0); }\n"
                    "fn deliver(flag: bool) -> i32 throw Error { return produce(flag)? + 1; }\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("result_consumption")}
            );

            struct Query final {
                std::size_t payloads;
                std::size_t success_checks;
                std::size_t saved_successes;

                static auto is_success(const TargetExpr& expression) noexcept -> bool {
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call == nullptr) {
                        return false;
                    }
                    const auto* member = std::get_if<TargetMemberExpr>(&call->callee->value);
                    if (member == nullptr) {
                        return false;
                    }
                    const auto* name = std::get_if<TargetIdentifier>(&member->name);
                    return name != nullptr && name->spelling() == "success_if";
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    success_checks += is_success(expression);
                    if (const auto* member = std::get_if<TargetMemberExpr>(&expression.value)) {
                        const auto* name = std::get_if<TargetIdentifier>(&member->name);
                        payloads += name != nullptr && name->spelling() == "value";
                    }
                    return true;
                }

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetDiscardStmt>(statement.value)));
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        saved_successes += is_success(variable->initializer);
                    }
                    return true;
                }
            };

            auto query = Query {.payloads = 0uz, .success_checks = 0uz, .saved_successes = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.success_checks == 4uz);
            ct::expect(query.saved_successes == 1uz);
            ct::expect(query.payloads == 1uz);
        }
    );

    ct::test(
        "Generation: cleanup-free prefixes deliver initializers and conditions directly",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view body;
            };
            constexpr auto cases = std::array {
                Case {.name = "scalar binding", .body = "let value = source(flag)?; return value;"},
                Case {
                    .name = "aggregate binding",
                    .body =
                        "let value = Pair { first: source(flag)?, second: 2 }; return value.first;"
                },
                Case {
                    .name = "native binding",
                    .body = "let value = ::probe::Fixed { source(flag)? }; return 1;"
                },
                Case {
                    .name = "closed inner cleanup",
                    .body =
                        "let value = source(if flag { let text: String = \"owned\"; observe(text); true } else { false })?; return value;"
                },
                Case {.name = "condition", .body = "if source(flag)? > 0 { return 1; } return 0;"},
                Case {
                    .name = "loop condition",
                    .body = "while source(flag)? > 0 { return 1; } return 0;"
                },
            };
            ct::each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(
                        std::format(
                            "struct Failure {{}} "
                            "struct Pair {{ first: i32, second: i32 }} "
                            "fn observe(value: String) {{}} "
                            "fn source(flag: bool) -> i32 throw Failure {{ if flag {{ return 7; }} throw Failure {{}}; }} "
                            "fn probe(flag: bool) -> i32 throw Failure {{ {} }}",
                            input.body
                        )
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("cleanup_free_prefix")}
                );
                struct Query final {
                    const TargetUnit& unit;

                    auto visit_variable(const TargetVariableStmt& variable) const noexcept -> bool {
                        if (const auto* type =
                                std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value)) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                            ct::expect(
                                type->symbol != TargetSymbol::Bool
                                || variable.binding != TargetVariableBinding::MutableValue
                            );
                        }
                        return true;
                    }
                };
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    const auto query = Query {.unit = unit};
                    ct::expect(traverse_target_unit(unit.sections(), query));
                }
            });
        }
    );

    ct::test(
        "Generation: independent root calls initialize Outcomes without deferred storage",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view body;
            };
            constexpr auto cases = std::array {
                Case {.name = "discard", .body = "produce(flag)?;"},
                Case {.name = "binding", .body = "let value = produce(flag)?; observe(value);"},
                Case {
                    .name = "conditional argument",
                    .body = "produce(if flag { true } else { false })?;"
                },
                Case {
                    .name = "statement argument",
                    .body = "produce(if flag { observe(1); true } else { observe(2); false })?;"
                },
            };
            ct::each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(
                        std::format(
                            "enum Error {{ Failed, }} "
                            "fn produce(flag: bool) -> i32 throw Error {{ if flag {{ return 7; }} throw Error::Failed; }} "
                            "fn observe(value: i32) {{}} "
                            "fn probe(flag: bool) throw Error {{ {} }}",
                            input.body
                        )
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("root_outcome")}
                );

                struct Query final {
                    const TargetUnit& unit;
                    std::size_t direct;
                    std::size_t deferred;

                    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                        const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
                        if (variable == nullptr) {
                            return true;
                        }
                        const auto* type =
                            std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value);
                        if (type == nullptr) {
                            return true;
                        }
                        if (type->symbol == TargetSymbol::RuntimeOutcome) {
                            ++direct;
                            ct::expect(
                                std::holds_alternative<TargetCallExpr>(variable->initializer.value)
                            );
                        }
                        if (type->symbol == TargetSymbol::RuntimeDeferredResult) {
                            if (!ct::expect(type->type_argument_ids.size() == 1uz)) {
                                return false;
                            }
                            const auto* result = std::get_if<TargetIntrinsicType>(
                                &unit.type(type->type_argument_ids.front()).value
                            );
                            deferred +=
                                result != nullptr && result->symbol == TargetSymbol::RuntimeOutcome;
                        }
                        return true;
                    }
                };

                auto direct = 0uz;
                auto deferred = 0uz;
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query = Query {.unit = unit, .direct = 0uz, .deferred = 0uz};
                    ct::expect(traverse_target_unit(unit.sections(), query));
                    direct += query.direct;
                    deferred += query.deferred;
                }
                ct::expect_equal(direct, 1uz);
                ct::expect_equal(deferred, 0uz);
            });
        }
    );

    ct::test(
        "Generation: scalar predecessors in a full expression need no deferred storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn first() -> i32 { return 1; } "
                    "fn second() -> i32 { return 2; } "
                    "fn pair(a: i32, b: i32) -> i32 { return a * 10 + b; } "
                    "fn probe() -> i32 { return pair(first(), second()); }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("scalar_predecessors")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
        }
    );

    ct::test(
        "Generation: independent value branches compose without duplicating successors",
        [] static noexcept {
            for (const auto suffix : {false, true}) {
                auto single_nodes = 0uz;
                for (const auto count : {1uz, 2uz, 4uz, 8uz}) {
                    auto parameters = std::string();
                    auto flags = std::string();
                    auto arguments = std::string();
                    auto initializers = std::string();
                    for (auto index = 0uz; index < count; ++index) {
                        if (index != 0uz) {
                            parameters += ", ";
                            flags += ", ";
                            arguments += ", ";
                        }
                        parameters += std::format("a{}: i32", index);
                        flags += std::format("x{}: bool, y{}: bool", index, index);
                        const auto choice = suffix
                            ? std::format(
                                  "if (x{0}) {{ 1 }} else if (y{0}) {{ 2 }} else {{ throw Failure::Stop; }}",
                                  index
                              )
                            : std::format("if (x{}) {{ 1 }} else {{ 2 }}", index);
                        initializers += std::format("let a{}: i32 = {};", index, choice);
                        arguments += suffix ? std::format("a{}", index) : choice;
                    }
                    const auto source = std::format(
                        "enum Failure {{ Stop, }} fn sum({}) -> i32 {{ return a0; }} "
                        "fn choose({}) -> i32 throw Failure {{ {} return sum({}); }}",
                        parameters,
                        flags,
                        suffix ? initializers : "",
                        arguments
                    );
                    const auto compilation = PlannedCompilation::build(
                        analyze_test_program(source),
                        {.test_mode = TestGenerationMode::None,
                         .linkage_domain = *LinkageDomain::explicit_value("linear_composition")}
                    );

                    struct Query final {
                        std::size_t nodes;
                        std::size_t branches;
                        std::size_t calls;

                        auto enter_expression(
                            const TargetExpr& expression,
                            TargetExpressionRole
                        ) noexcept -> bool {
                            ++nodes;
                            branches +=
                                std::holds_alternative<TargetConditionalExpr>(expression.value);
                            if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                                if (const auto* name =
                                        std::get_if<TargetNameExpr>(&call->callee->value)) {
                                    calls += name->name.components().back().spelling() == "sum";
                                }
                            }
                            return true;
                        }

                        auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                            ++nodes;
                            if (const auto* conditional =
                                    std::get_if<TargetIfStmt>(&statement.value)) {
                                branches += conditional->branches.size();
                            }
                            return true;
                        }
                    };

                    auto query = Query {.nodes = 0uz, .branches = 0uz, .calls = 0uz};
                    for (const auto artifact : compilation.target().artifacts()) {
                        const auto unit = lower_artifact(compilation, artifact.id);
                        ct::expect(traverse_target_unit(unit.sections(), query));
                    }
                    ct::expect(query.calls == 1uz);
                    ct::expect(query.branches == count * (suffix ? 2uz : 1uz));
                    if (count == 1uz) {
                        single_nodes = query.nodes;
                    }
                    ct::expect(query.nodes <= count * single_nodes)
                        .note("query.nodes: ", query.nodes);
                }
            }
        }
    );

    ct::test(
        "Generation: structured values deliver into their destination without factories",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn pick(v: i32, flag: bool) -> i32 { "
                    "var r = 0; r = match v { 0 => 1, 1 => 2, _ => 3 }; "
                    "let s = if flag { r } else { v }; "
                    "return match v { 0 => s, _ => r }; }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("structured_delivery")}
            );

            struct Query final {
                std::size_t transfers;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    transfers += std::holds_alternative<TargetGotoStmt>(statement.value)
                        || std::holds_alternative<TargetLabelStmt>(statement.value);
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    ct::expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                    return true;
                }
            };

            auto query = Query {.transfers = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::require(traverse_target_unit(unit.sections(), query));
            }
            ct::expect_equal(query.transfers, 0uz);
        }
    );

    ct::test("Generation: void calls remain return expressions", [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(
                "fn action() {}\n"
                "fn forward() { return action(); }\n"
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("void_expression_delivery")}
        );

        struct Query final {
            std::size_t returned_calls;

            auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                if (const auto* returned = std::get_if<TargetReturnStmt>(&statement.value);
                    returned != nullptr && returned->expression) {
                    returned_calls +=
                        std::holds_alternative<TargetCallExpr>(returned->expression->value);
                }
                return true;
            }

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) const noexcept
                -> bool {
                ct::expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                return true;
            }
        };

        auto query = Query {.returned_calls = 0uz};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            ct::expect(traverse_target_unit(unit.sections(), query));
        }
        ct::expect(query.returned_calls == 1uz);
    });

    ct::test(
        "Generation: builtin pointer observations need no temporary storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn present(p: ptr<i32>) -> bool { return p != nullptr; }\n"
                    "fn read(p: ptr<i32>) -> i32 {\n"
                    "  if p == nullptr { return 0; }\n"
                    "  return *p;\n"
                    "}\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("pointer_observation")}
            );

            struct Query final {
                std::size_t comparisons;
                std::size_t dereferences;

                auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    ct::expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                    if (const auto* binary = std::get_if<TargetBinaryExpr>(&expression.value)) {
                        comparisons += binary->op == TargetBinaryOperator::Equal
                            || binary->op == TargetBinaryOperator::NotEqual;
                    }
                    if (const auto* prefix = std::get_if<TargetPrefixExpr>(&expression.value)) {
                        dereferences += prefix->op == TargetPrefixOperator::Dereference;
                    }
                    return true;
                }
            };

            auto query = Query {.comparisons = 0uz, .dereferences = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.comparisons == 2uz);
            ct::expect(query.dereferences == 1uz);
        }
    );

    ct::test(
        "Generation: explicit writable source pointers retain their access contract",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn writable(p: ptr<&i32>) -> ptr<i32> { let saved = p; return saved; }\n"
                    "fn readonly(p: ptr<i32>) -> ptr<i32> { let saved = p; return saved; }\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("source_pointer_access")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t writable;
                std::size_t readonly;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        const auto* pointer =
                            std::get_if<TargetPointerType>(&unit.type(variable->type).value);
                        if (!ct::expect(pointer != nullptr)) {
                            return false;
                        }
                        const auto& pointee = unit.type(pointer->pointee);
                        const auto* intrinsic = std::get_if<TargetIntrinsicType>(&pointee.value);
                        const auto constant = pointee.const_qualified
                            || (intrinsic != nullptr
                                && intrinsic->symbol == TargetSymbol::StdAddConst);
                        writable += !constant;
                        readonly += constant;
                        ct::expect(variable->binding == TargetVariableBinding::ConstValue);
                    }
                    return true;
                }
            };

            auto writable = 0uz;
            auto readonly = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .writable = 0uz, .readonly = 0uz};
                ct::expect(traverse_target_unit(unit.sections(), query));
                writable += query.writable;
                readonly += query.readonly;
            }
            ct::expect(writable == 1uz);
            ct::expect(readonly == 1uz);
        }
    );

    ct::test(
        "Generation: independent nested pattern alternatives keep target size proportional",
        [] static noexcept {
            auto counts = std::vector<std::size_t>();
            for (const auto width : {2uz, 4uz, 8uz}) {
                auto fields = std::string();
                auto patterns = std::string();
                for (auto index = 0uz; index < width; ++index) {
                    if (index != 0uz) {
                        fields += ", ";
                        patterns += ", ";
                    }
                    fields += "i32";
                    patterns += "0 | 1";
                }
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(
                        std::format(
                            "enum Choices {{ Fields({}), }} "
                            "fn select(value: Choices) -> i32 {{ return match value {{ "
                            ".Fields({}) => 1, _ => 0, }}; }}",
                            fields,
                            patterns
                        )
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("pattern_size")}
                );

                struct Query final {
                    std::size_t nodes;

                    auto enter_expression(const TargetExpr&, TargetExpressionRole) noexcept
                        -> bool {
                        ++nodes;
                        return true;
                    }

                    auto enter_statement(const TargetStmt&) noexcept -> bool {
                        ++nodes;
                        return true;
                    }
                };

                auto query = Query {.nodes = 0uz};
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    ct::expect(traverse_target_unit(unit.sections(), query));
                }
                counts.push_back(query.nodes);
            }
            if (!ct::expect(counts.front() > 0uz)) {
                return;
            }
            for (auto index = 1uz; index < counts.size(); ++index) {
                ct::expect(counts[index] > counts[index - 1uz]);
                // Doubling independent choices must not expand their Cartesian product.
                // Leave room for target scaffolding without fixing names or exact counts.
                ct::expect(counts[index] <= 3uz * counts[index - 1uz]);
            }
        }
    );

    ct::test(
        "Generation: builtin Read snapshots use unqualified value factory results",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn advance(&value: i32) -> i32 { value += 1; return value; }
            fn format(&value: i32) -> String {
                var output = String {};
                return f"{if true {
                    output.append_format(f"{value}/{advance(&value)}");
                    value
                } else { 0 }}";
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("read_snapshot")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t scalar_factories;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value);
                    if (lambda != nullptr) {
                        const auto& result = unit.type(lambda->result);
                        const auto* type = std::get_if<TargetIntrinsicType>(&result.value);
                        if (type != nullptr && type->symbol == TargetSymbol::StdInt32) {
                            ++scalar_factories;
                            ct::expect(!(result.const_qualified));
                        }
                    }
                    return true;
                }
            };

            auto scalar_factories = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .scalar_factories = 0uz};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                scalar_factories += query.scalar_factories;
            }
            ct::expect(scalar_factories > 0uz);
        }
    );

    ct::test(
        "Generation: local storage follows retained access rather than source write permission",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                TargetVariableBinding expected;
            };

            const auto cases = std::array {
                Case {
                    .name = "read-only var",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; return storage; }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "assignment",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; storage += 1; return storage; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "Write argument",
                    .source = "fn write(&value: i32) { value = 2; } "
                              "fn probe(value: i32) { var storage = value; write(&storage); }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "Write capture",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; "
                        "let read = [&storage]() -> i32 { return storage; }; return read(); }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "let transfer",
                    .source =
                        "fn probe(value: String) -> String { let storage = value; return &&storage; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "read field",
                    .source =
                        "struct Box { value: i32 } "
                        "fn probe(value: i32) -> i32 { var storage = Box { value }; return storage.value; }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "write field",
                    .source =
                        "struct Box { value: i32 } "
                        "fn probe(value: i32) { var storage = Box { value }; storage.value = 2; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "read element",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = [value]; return storage[0]; }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "write element",
                    .source = "fn probe(value: i32) { var storage = [value]; storage[0] = 2; }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "write iteration",
                    .source =
                        "fn probe(value: i32) { var storage = [value]; for &element in storage { element += 1; } }",
                    .expected = TargetVariableBinding::MutableValue
                },
                Case {
                    .name = "write pointee",
                    .source =
                        "fn probe(pointer: ptr<&i32>) { var storage = pointer; if storage != nullptr { *storage = 2; } }",
                    .expected = TargetVariableBinding::ConstValue
                },
                Case {
                    .name = "unreachable write does not require mutable storage",
                    .source =
                        "fn probe(value: i32) -> i32 { var storage = value; if false { storage = 2; } return storage; }",
                    .expected = TargetVariableBinding::ConstValue
                },
            };
            ct::each(cases, &Case::name, [](const Case& scenario) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(std::string(scenario.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("local_storage")}
                );

                struct Query final {
                    const TargetUnit& unit;
                    TargetVariableBinding expected;
                    std::size_t owners;

                    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                        const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
                        if (variable != nullptr
                            && unit.local_name(variable->local).spelling() == "storage") {
                            ++owners;
                            ct::expect(variable->binding == expected);
                        }
                        return true;
                    }
                };

                auto owners = 0uz;
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query = Query {
                        .unit = unit,
                        .expected = scenario.expected,
                        .owners = 0uz,
                    };
                    if (!(ct::expect(traverse_target_unit(unit.sections(), query)))) {
                        return;
                    }
                    owners += query.owners;
                }
                ct::expect(owners == 1uz);
            });
        }
    );

    ct::test(
        "Generation: discarded operations use their native result contract",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Record { value: i32, }
            enum Tag { First, Second, }
            fn scalar() -> i32 => 1;
            fn record() -> Record => { value: 1 };
            fn tag() -> Tag => Tag::First;
            fn effect() -> bool => true;
            fn discard(value: f64, divisor: i32) {
                scalar();
                record();
                tag();
                1 / divisor;
                value + 1.0;
                if effect() && false { scalar(); }
                if false && effect() { scalar(); }
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("discarded_operations")}
            );

            struct Query final {
                std::flat_map<std::string, std::size_t> calls;
                std::size_t checked_divisions;
                std::size_t explicit_discards;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* discard = std::get_if<TargetDiscardStmt>(&statement.value)) {
                        ++explicit_discards;
                        ct::expect(
                            std::holds_alternative<TargetBinaryExpr>(discard->expression.value)
                        );
                    }
                    if (const auto* expression = std::get_if<TargetExprStmt>(&statement.value)) {
                        ct::expect(
                            std::holds_alternative<TargetCallExpr>(expression->expression.value)
                        );
                    }
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                        if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                            ++calls[std::string(name->name.components().back().spelling())];
                        } else if (const auto* intrinsic =
                                       std::get_if<TargetIntrinsicNameExpr>(&call->callee->value)) {
                            checked_divisions +=
                                intrinsic->symbol == TargetSymbol::RuntimeIntegerDivide;
                        }
                    }
                    return true;
                }
            };

            auto query = Query {.calls = {}, .checked_divisions = 0uz, .explicit_discards = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect(query.calls["scalar"] >= 1uz);
            ct::expect_equal(query.calls["record"], 1uz);
            ct::expect_equal(query.calls["tag"], 1uz);
            ct::expect_equal(query.calls["effect"], 1uz);
            ct::expect(query.checked_divisions == 1uz);
            ct::expect(query.explicit_discards == 1uz);
        }
    );

    ct::test(
        "Generation: boolean expressions use native short circuit without result storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn both(left: bool, right: bool) -> bool => left && right;
            fn either(left: bool, right: bool) -> bool => left || right;
            fn nested(a: bool, b: bool, c: bool) -> bool => a && (b || c);
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("native_short_circuit")}
            );

            struct Query final {
                std::size_t logical;

                auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                    ct::expect(!(std::holds_alternative<TargetIfStmt>(statement.value)));
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    if (const auto* binary = std::get_if<TargetBinaryExpr>(&expression.value)) {
                        logical += binary->op == TargetBinaryOperator::LogicalAnd
                            || binary->op == TargetBinaryOperator::LogicalOr;
                    }
                    return true;
                }
            };

            auto query = Query {.logical = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect(query.logical == 4uz);
        }
    );

    ct::test(
        "Generation: loop conditions use native expressions after required sequencing",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn direct(&remaining: i32) { while remaining > 0 { remaining -= 1; } }
            struct Failure {}
            fn predicate() -> bool throw Failure { return false; }
            fn ordered() throw Failure { while predicate()? {} }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("loop_conditions")}
            );

            struct Query final {
                std::size_t direct;
                std::size_t sequenced;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* loop = std::get_if<TargetWhileStmt>(&statement.value)) {
                        const auto* literal =
                            std::get_if<TargetLiteralExpr>(&loop->condition.value);
                        if (literal != nullptr) {
                            ++sequenced;
                            ct::expect(!(loop->body.empty()));
                        } else {
                            ++direct;
                        }
                    }
                    return true;
                }
            };

            auto query = Query {.direct = 0uz, .sequenced = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect(query.direct == 1uz);
            ct::expect(query.sequenced == 1uz);
        }
    );

    ct::test(
        "Generation: native for steps retain continue without a transfer label",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                fn count(limit: i32) -> i32 {
                    var result: i32 = 0;
                    for var index: i32 = 0; index < limit; ++index {
                        if index == 1 { continue; }
                        result += index;
                    }
                    return result;
                }
            )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("native_steps")}
            );
            struct Query final {
                std::size_t loops;
                std::size_t continuations;
                std::size_t transfers;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* loop = std::get_if<TargetForStmt>(&statement.value)) {
                        ++loops;
                        ct::expect_equal(loop->steps.size(), 1uz);
                        ct::expect(loop->condition.has_value());
                    }
                    continuations += std::holds_alternative<TargetContinueStmt>(statement.value);
                    transfers += std::holds_alternative<TargetGotoStmt>(statement.value);
                    return true;
                }
            };
            auto query = Query {.loops = 0uz, .continuations = 0uz, .transfers = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect_equal(query.loops, 1uz);
            ct::expect_equal(query.continuations, 1uz);
            ct::expect_equal(query.transfers, 0uz);
        }
    );

    ct::test("Generation: callable results use executable test-stop effects", [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(R"(
                    fn selected() -> i32 {
                        return const if false { require(false); 1 } else { 2 };
                    }
                    fn wrapper() -> i32 => selected();
                    fn ordinary() -> i32 {
                        return if false { require(false); 1 } else { 2 };
                    }
                )"),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("executable_effects")}
        );
        struct Query final {
            const TargetUnit& unit;
            std::flat_set<std::string> checked;

            auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
                const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
                if (function == nullptr
                    || !std::holds_alternative<TargetFreeFunctionDefinition>(function->form)) {
                    return true;
                }
                const auto name = function->name.components().back().spelling();
                if (name != "selected" && name != "wrapper" && name != "ordinary") {
                    return true;
                }
                checked.insert(std::string(name));
                const auto* result =
                    std::get_if<TargetIntrinsicType>(&unit.type(function->result).value);
                if (!ct::expect(result != nullptr)) {
                    return false;
                }
                if (name != "ordinary") {
                    ct::expect_equal(result->symbol, TargetSymbol::StdInt32);
                    return true;
                }
                ct::expect_equal(result->symbol, TargetSymbol::RuntimeOutcome);
                ct::expect(std::ranges::any_of(result->type_argument_ids, [&](auto id) noexcept {
                    const auto* member = std::get_if<TargetIntrinsicType>(&unit.type(id).value);
                    return member != nullptr && member->symbol == TargetSymbol::RuntimeTestStopped;
                }));
                return true;
            }
        };
        auto checked = std::flat_set<std::string>();
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            auto query = Query {.unit = unit, .checked = {}};
            if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                return;
            }
            for (const auto& name : query.checked) {
                checked.insert(name);
            }
        }
        ct::expect(checked.contains("selected"));
        ct::expect(checked.contains("wrapper"));
        ct::expect(checked.contains("ordinary"));
    });

    ct::test(
        "Generation: closed loop steps use native continue without auxiliary transfers",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                import <probe.hpp> using probe::{make};
                fn ordered(&index: i32) {
                    for ; index < 2; make(), ++index { continue; }
                }
            )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("step_cleanup")}
            );
            struct Query final {
                bool header_steps;
                bool native_continue;
                bool auxiliary_transfer;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* loop = std::get_if<TargetForStmt>(&statement.value)) {
                        header_steps |= !loop->steps.empty();
                    }
                    native_continue |= std::holds_alternative<TargetContinueStmt>(statement.value);
                    auxiliary_transfer |= std::holds_alternative<TargetGotoStmt>(statement.value);
                    return true;
                }
            };
            auto query = Query {
                .header_steps = false,
                .native_continue = false,
                .auxiliary_transfer = false,
            };
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect(query.header_steps);
            ct::expect(query.native_continue);
            ct::expect(!query.auxiliary_transfer);
        }
    );

    ct::test(
        "Generation: shared native query types have bounded expanded target syntax",
        [] static noexcept {
            constexpr auto depth = 12uz;
            auto source = std::string(
                "import <probe.hpp> using probe::{seed};\nfn grow() { let x0 = seed();\n"
            );
            for (auto index = 1uz; index <= depth; ++index) {
                source += std::format("let x{} = x{} + x{};\n", index, index - 1, index - 1);
            }
            source += std::format("return x{}; }}\n", depth);
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(std::move(source)),
                {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = *LinkageDomain::explicit_value("query_graph"),
                }
            );

            auto largest_key = 0uz;
            for (const auto type : compilation.semantic().types().entries()) {
                largest_key =
                    std::max(largest_key, type_content_key(compilation.semantic(), type.id).size());
            }
            ct::expect_less(largest_key, 256uz * (depth + 1uz));

            struct Query final {
                const TargetUnit& unit;
                std::size_t expanded_types;

                auto visit_type(TargetTypeID id) noexcept -> bool {
                    ++expanded_types;
                    return visit_target_type_children(unit.type(id).value, *this);
                }
            };

            auto expanded_types = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .expanded_types = 0uz};
                ct::expect(traverse_target_unit(unit.sections(), query));
                expanded_types += query.expanded_types;
            }
            ct::expect_less(expanded_types, 128uz * (depth + 1uz));
        }
    );

    ct::test("Generation: nested expressions retain each runtime operand once", [] static noexcept {
        constexpr auto count = 128uz;
        auto source = std::string(
            "fn operand(n: i32) -> i32 { return n; } fn chain(n: i32) -> i32 { return "
        );
        for (auto index = 0uz; index < count; ++index) {
            if (index != 0uz) {
                source += '+';
            }
            source += "operand(n)";
        }
        source += "; }";
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(std::move(source)),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("long_expression")}
        );

        struct Query final {
            std::size_t calls;

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                    if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                        calls += name->name.components().back().spelling() == "operand";
                    }
                }
                return true;
            }
        };

        auto query = Query {.calls = 0uz};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            ct::expect(traverse_target_unit(unit.sections(), query));
        }
        ct::expect(query.calls == count);
    });

    ct::test(
        "Generation: stable scalar parameters need no snapshot before later effects",
        [] static noexcept {
            const auto parameter_access = std::array {false, true};
            for (const auto writable : parameter_access) {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(
                        std::string(
                            "fn effect() -> i32 { println(2); return 2; } "
                            "fn consume(first: i32, second: i32) -> i32 => first + second; "
                            "fn probe("
                        )
                        + (writable ? "&" : "") + "first: i32) -> i32 => consume(first, effect());"
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("stable_scalar")}
                );

                struct Query final {
                    std::size_t locals;

                    auto visit_variable(const TargetVariableStmt&) noexcept -> bool {
                        ++locals;
                        return true;
                    }
                };

                auto query = Query {.locals = 0uz};
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    if (!(ct::expect(traverse_target_unit(unit.sections(), query))
                              .note("writable: ", writable))) {
                        return;
                    }
                }
                ct::expect(query.locals == (writable ? 1uz : 0uz)).note("writable: ", writable);
            }
        }
    );

    ct::test(
        "Generation: simple patterns use predicates without mutable match state",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn classify(value: i32) -> i32 => match value {
                0 | 1 => 3,
                2..=4 => 5,
                _ => 6,
            };
            struct Failure {}
            fn failing() -> i32 throw Failure { throw Failure {}; }
            fn recover() -> i32 => try { failing()? } catch { _ => 7, };
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("pattern_predicates")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t mutable_booleans;

                auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
                    if (const auto* type =
                            std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value);
                        type != nullptr
                        && type->symbol == TargetSymbol::Bool
                        && variable.binding == TargetVariableBinding::MutableValue) {
                        ++mutable_booleans;
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .mutable_booleans = 0uz};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                ct::expect(query.mutable_booleans == 0uz);
            }
        }
    );

    ct::test(
        "Generation: payload patterns retain only used immutable selection storage",
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
            };
            constexpr auto cases = std::array {
                Case {.name = "payload field", .source = R"(
            enum Value { Number(i32), Pair(i32, i32), Empty, }
            fn select(value: Value) -> i32 => match value {
                .Number(number) => number,
                .Pair(0 | 1, right) => right,
                _ => 0,
            };
        )"},
                Case {.name = "nested payload field", .source = R"(
            enum Inner { Number(i32), Empty, }
            enum Outer { Nested(Inner), Empty, }
            fn select(value: Outer) -> i32 => match value {
                .Nested(.Number(number)) => number,
                .Nested(.Empty) => 0,
                .Empty => -1,
            };
        )"},
                Case {.name = "wildcard payload", .source = R"(
            enum Value { Pair(i32, i32) }
            fn select(value: Value) -> i32 => match value {
                .Pair(_, _) => 0,
            };
        )"},
                Case {.name = "nested wildcard payload", .source = R"(
            enum Inner { Pair(i32, i32) }
            enum Outer { Nested(Inner) }
            fn select(value: Outer) -> i32 => match value {
                .Nested(.Pair(_, _)) => 0,
            };
        )"},
                Case {.name = "bound exits before payload read", .source = R"(
            enum Value { Pair(i32, i32) }
            enum Failure { Stop }
            fn select(value: Value) -> i32 throw Failure {
                return match value {
                    .Pair((const if true { throw Failure::Stop; } else { 0 }).., _) => 1,
                    _ => 0,
                };
            }
        )"},
                Case {.name = "catch wildcard payload", .source = R"(
            enum Failure { Pair(i32, i32) }
            fn select() -> i32 {
                return try { throw Failure::Pair(1, 2); } catch {
                    Failure(.Pair(_, _)) => 0,
                };
            }
        )"},
            };
            ct::each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(std::string(input.source)),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("fixed_payload_binding")}
                );

                struct Query final {
                    const TargetUnit& unit;
                    std::vector<TargetLocalID> pointers;
                    std::set<TargetLocalID> references;
                    std::size_t mutable_pointers;
                    std::size_t mutable_booleans;

                    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
                        const auto& type = unit.type(variable.type).value;
                        if (std::holds_alternative<TargetPointerType>(type)) {
                            pointers.push_back(variable.local);
                        }
                        if (variable.binding != TargetVariableBinding::MutableValue) {
                            return true;
                        }
                        mutable_pointers += std::holds_alternative<TargetPointerType>(type);
                        if (const auto* intrinsic = std::get_if<TargetIntrinsicType>(&type)) {
                            mutable_booleans += intrinsic->symbol == TargetSymbol::Bool;
                        }
                        return true;
                    }

                    auto enter_expression(
                        const TargetExpr& expression,
                        TargetExpressionRole
                    ) noexcept -> bool {
                        if (const auto* local = std::get_if<TargetLocalExpr>(&expression.value)) {
                            references.insert(local->local);
                        }
                        return true;
                    }
                };
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    auto query = Query {
                        .unit = unit,
                        .pointers = {},
                        .references = {},
                        .mutable_pointers = 0uz,
                        .mutable_booleans = 0uz
                    };
                    if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                        return;
                    }
                    // Each retained projection serves a tag test, payload read, or binding.
                    for (const auto local : query.pointers) {
                        ct::expect(query.references.contains(local));
                    }
                    ct::expect_equal(query.mutable_pointers, 0uz);
                    ct::expect_equal(query.mutable_booleans, 0uz);
                }
            });
        }
    );

    ct::test(
        "Generation: linear native printing uses automatic operand storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "import <vector> using std::vector; "
                    "fn show() { let v = vector {1, 2, 3}; println(v[0]); }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("linear_native_print")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t variables = 0;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        ++variables;
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            auto variables = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                ct::expect(traverse_target_unit(unit.sections(), query));
                variables += query.variables;
            }
            ct::expect(variables > 0);
        }
    );

    ct::test(
        "Generation: a folded short circuit does not defer later argument storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "import <string> using std::string; "
                    "fn probe() -> bool { return true; } "
                    "fn show() { println(false && probe(), "
                    "string { c\"first\" }, string { c\"second\" }); }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("folded_short_circuit")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
        }
    );

    ct::test(
        "Generation: integer facts select direct operations and preserve required operands",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn effect() -> i32 => 8;
            fn quotient(value: i32) -> i32 => value / 2;
            fn remainder(value: u8) -> u8 => value % 3;
            fn shift(value: i16) -> i16 => value >> 2;
            fn wrap(value: u32) -> u32 => value * 3 + 1;
            fn discard() { effect() / 2; effect() / -1; effect() >> 2; }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("integer_facts")}
            );

            struct Query final {
                std::size_t operations = 0;
                std::size_t effects = 0;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    operations += std::holds_alternative<TargetBinaryExpr>(expression.value);
                    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                        const auto* name = std::get_if<TargetNameExpr>(&call->callee->value);
                        if (!ct::expect(name != nullptr)) {
                            return false;
                        }
                        ct::expect(name->name.components().back().spelling() == "effect");
                        ++effects;
                    }
                    return true;
                }
            };

            auto query = Query();
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect(query.operations == 5uz);
            ct::expect(query.effects == 3uz);
        }
    );

    ct::test(
        "Generation: explicit native construction targets need no deduction query",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "import <vector> using std::vector; "
                    "fn count() { let values = vector<i32> { 1, 2 }; return values.size(); }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("explicit_construction")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t constructions = 0;

                auto visit_type(TargetTypeID id) noexcept -> bool {
                    const auto& type = unit.type(id);
                    if (const auto* deduced = std::get_if<TargetDecltypeType>(&type.value)) {
                        ct::expect(!(std::holds_alternative<TargetConstructionExpr>(
                            deduced->expression().value
                        )));
                    }
                    return visit_target_type_children(type.value, *this);
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    if (const auto* construction =
                            std::get_if<TargetConstructionExpr>(&expression.value)) {
                        constructions += std::holds_alternative<TargetNamedType>(
                            unit.type(construction->type).value
                        );
                    }
                    return true;
                }
            };

            auto constructions = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                constructions += query.constructions;
            }
            ct::expect(constructions == 1uz);
        }
    );

    ct::test(
        "Generation: unconditional pattern binding uses direct value initialization",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn copy(value: i32) -> i32 => match value { bound => bound, };"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("direct_pattern_binding")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetAssignmentStmt>(statement.value)));
                    ct::expect(!(std::holds_alternative<TargetIfStmt>(statement.value)));
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        ct::expect(!(std::holds_alternative<TargetPointerType>(
                            unit.type(variable->type).value
                        )));
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                const auto query = Query {.unit = unit};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
        }
    );

    ct::test(
        "Generation: independent expression and region results use automatic outcome storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Failure {}
            fn source(flag: bool) -> i32 throw Failure {
                if flag { throw Failure {}; }
                return 7;
            }
            fn recover(flag: bool) -> i32 => try {
                let value = source(flag)?;
                value
            } catch { Failure(_) => 0, };
            fn tail(flag: bool) -> i32 => try { source(flag)? } catch { Failure(_) => 0, };
            fn selected(flag: bool) -> i32 => if flag {
                (try { source(flag)? } catch { Failure(_) => 0, })
            } else { 0 };
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("nested_full_expression")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t outcomes = 0;

                auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
                    const auto* type =
                        std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value);
                    if (type != nullptr && type->symbol == TargetSymbol::RuntimeOutcome) {
                        ++outcomes;
                    }
                    if (type != nullptr && type->symbol == TargetSymbol::RuntimeDeferredResult) {
                        const auto* contained = std::get_if<TargetIntrinsicType>(
                            &unit.type(type->type_argument_ids.front()).value
                        );
                        if (!ct::expect(contained != nullptr)) {
                            return false;
                        }
                        ct::expect(contained->symbol != TargetSymbol::RuntimeOutcome);
                    }
                    return true;
                }
            };

            auto outcomes = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                outcomes += query.outcomes;
            }
            ct::expect(outcomes == 3uz);
        }
    );

    ct::test("Generation: runtime match predicates retain required calls", [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(R"(
            fn effect() -> bool => true;
            fn choose() -> i32 => match effect() && false {
                true => 0,
                false if effect() && false => 1,
                false => 2,
            };
        )"),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("known_match")}
        );

        struct Query final {
            std::size_t effects = 0;

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                    if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                        ct::expect(name->name.components().back().spelling() == "effect");
                        ++effects;
                    }
                }
                return true;
            }
        };

        auto query = Query();
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                return;
            }
        }
        ct::expect(query.effects == 2uz);
    });

    ct::test(
        "Generation: a sole failure needs no type selection in its handler or dispatch",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Failure {}
            fn source() -> i32 throw Failure { throw Failure {}; }
            fn recover() -> i32 => try { source()? } catch { Failure(_) => 7, };
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("sole_failure")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t branches = 0;

                auto visit_type(TargetTypeID id) const noexcept -> bool {
                    if (const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(id).value)) {
                        ct::expect(type->symbol != TargetSymbol::StdVariant);
                    }
                    return visit_target_type_children(unit.type(id).value, *this);
                }

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    branches += std::holds_alternative<TargetIfStmt>(statement.value);
                    return true;
                }
            };

            auto branches = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                branches += query.branches;
            }
            ct::expect(branches == 1uz); // The call's success/failure distinction remains.
        }
    );

    ct::test(
        "Generation: explicit pure owner returns permit native return construction",
        [] static noexcept {
            struct Scenario final {
                const char* name;
                const char* source;
                bool returns_name;
            };

            const auto scenarios = std::array {
                Scenario {
                    .name = "local Take",
                    .source = R"(fn f() -> String { let value: String = "text"; return &&value; })",
                    .returns_name = true
                },
                Scenario {
                    .name = "parameter Take",
                    .source = "fn f(&&value: String) -> String => &&value;",
                    .returns_name = true
                },
                Scenario {
                    .name = "local copy",
                    .source = R"(fn f() -> String { let value: String = "text"; return value; })",
                    .returns_name = false
                },
                Scenario {
                    .name = "native Take",
                    .source =
                        "import <string> using std::string; fn f(&&value: string) -> string => &&value;",
                    .returns_name = false
                },
            };
            ct::each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                const auto compilation = PlannedCompilation::build(
                    analyze_test_program(scenario.source),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("return_construction")}
                );

                struct Query final {
                    std::size_t returns;
                    std::size_t named_returns;

                    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                        if (const auto* returned = std::get_if<TargetReturnStmt>(&statement.value);
                            returned != nullptr && returned->expression) {
                            ++returns;
                            named_returns += std::holds_alternative<TargetLocalExpr>(
                                returned->expression->value
                            );
                        }
                        return true;
                    }
                };

                auto query = Query {.returns = 0uz, .named_returns = 0uz};
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    if (!(ct::expect(traverse_target_unit(unit.sections(), query)))) {
                        return;
                    }
                }
                if (!(ct::expect_equal(query.returns, 1uz))) {
                    return;
                }
                ct::expect_equal(query.named_returns, scenario.returns_name ? 1uz : 0uz);
            });
        }
    );

    ct::test(
        "Generation: pure Read arguments need no borrowed temporary storage in match arms",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Record { value: i32 }
            enum Kind { Invalid }
            struct Failure { record: Record, kind: Kind }
            fn failure(record: Record, kind: Kind) -> Failure => { record: record, kind: kind };
            fn probe(value: i32) throw Failure {
                match value {
                    0 if value >= 0 => throw failure({ value: value }, .Invalid),
                    _ => {},
                }
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("read_value_temporaries")}
            );

            struct Query final {
                const TargetUnit& unit;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        if (const auto* type = std::get_if<TargetIntrinsicType>(
                                &unit.type(variable->type).value
                            )) {
                            ct::expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                        }
                    }
                    return true;
                }
            };

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
        }
    );
});

} // namespace
