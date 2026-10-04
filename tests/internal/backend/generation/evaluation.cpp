module carven:test.internal.backend.generation.evaluation;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :semantic.semir.body;
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
                        if (const auto* name = std::get_if<TargetNameExpr>(
                                &template_primary_expression(*call->callee).value
                            )) {
                            ++calls[std::string(name->name.components().back().spelling())];
                        } else if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(
                                       &template_primary_expression(*call->callee).value
                                   )) {
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
                    if (const auto* name = std::get_if<TargetNameExpr>(
                            &template_primary_expression(*call->callee).value
                        )) {
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
                        const auto* name = std::get_if<TargetNameExpr>(
                            &template_primary_expression(*call->callee).value
                        );
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
});

} // namespace
