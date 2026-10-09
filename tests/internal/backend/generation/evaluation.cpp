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
import :semantic.semir.simd;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generation: known slice results retain their range check"_test = [] static noexcept {
        for (const auto query : {".length()", "[1] as usize"}) {
            const auto semantic_owner = analyze_test_program(
                std::format(
                    "fn probe(values: [i32; 2]) -> usize => values.as_slice().slice(0, 3){};",
                    query
                )
            );
            const auto compilation = PlannedCompilation::build(
                semantic_owner,
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("slice_range"),
                 .library = std::nullopt}
            );
            struct Query final {
                std::size_t ranges = 0uz;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* member = std::get_if<TargetMemberExpr>(&expression.value);
                    const auto* name =
                        member ? std::get_if<TargetIdentifier>(&member->name) : nullptr;
                    ranges += name && name->spelling() == "slice";
                    return true;
                }
            };
            auto calls = Query {};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), calls));
            }
            expect_equal(calls.ranges, 1uz);
        }
    };

    "Generation: static projections publish values without rebuilding their aggregate"_test =
        [] static noexcept {
            const auto semantic_owner = analyze_test_program(R"(
                    struct Plan { lanes: u8x16, nested: (i32, [i32; 2]) }
                    fn vector() -> u8x16 {
                        const plan = Plan { u8x16::splat(7), (1, [3, 9]) };
                        return plan.lanes;
                    }
                    fn scalar() -> i32 {
                        const plan = Plan { u8x16::splat(7), (1, [3, 9]) };
                        return plan.nested.1[1];
                    }
                )");
            const auto compilation = PlannedCompilation::build(
                semantic_owner,
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("static_projection"),
                 .library = std::nullopt}
            );
            struct Query final {
                std::size_t splats;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    expect(!std::holds_alternative<TargetConstructionExpr>(expression.value));
                    expect(!std::holds_alternative<TargetMemberExpr>(expression.value));
                    if (const auto* member =
                            std::get_if<TargetStaticMemberExpr>(&expression.value)) {
                        splats += member->name.spelling() == "splat";
                    }
                    return true;
                }
            };
            auto query = Query {.splats = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect_equal(query.splats, 1uz);
        };

    "Evaluation: specialization keeps runtime failures in their executable operations"_test =
        [] static noexcept {
            const auto semantic = analyze_test_program(R"(
                fn divide(const divisor: i32) -> i32 => 1 / divisor;
                fn lane(const index: usize) -> u8 => u8x16::splat(7).lane(index);
                fn runtime_divide() -> i32 => divide(0);
                fn runtime_lane() -> u8 => lane(16);
                const fn skipped() -> bool {
                    const zero = 0;
                    return false && (1 / zero > 0);
                }
                const accepted = skipped();
            )");
            auto divisions = 0uz;
            auto lanes = 0uz;
            for (const auto entry : semantic.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (const auto* binary = std::get_if<SemBinary>(&expression.value); binary
                            && binary->operation == BinaryOperator::Divide
                            && binary->right->constant) {
                            expect(!expression.constant);
                            expect_equal(
                                evaluation_rule(semantic, expression).action,
                                EvaluationAction::Required
                            );
                            ++divisions;
                        }
                        const auto* intrinsic = std::get_if<SemIntrinsic>(&expression.value);
                        const auto* simd =
                            intrinsic ? std::get_if<SIMDIntrinsic>(&intrinsic->operation) : nullptr;
                        if (simd
                            && *simd == SIMDIntrinsic::Lane
                            && intrinsic->operands[1].expression.constant) {
                            expect(!expression.constant);
                            expect_equal(
                                evaluation_rule(semantic, expression).action,
                                EvaluationAction::Required
                            );
                            ++lanes;
                        }
                    }
                );
            }
            expect(divisions != 0uz);
            expect(lanes != 0uz);
        };

    "Evaluation: known results retain source operations and execution obligations"_test =
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
                                expect(known_boolean(semantic, expression) == true);
                                expect(
                                    evaluation_rule(semantic, expression).action
                                    == EvaluationAction::Operands
                                );
                            }
                            if (binary->operation == BinaryOperator::Divide) {
                                ++checked_count;
                                expect(
                                    evaluation_rule(semantic, expression).action
                                    == EvaluationAction::Required
                                );
                            }
                            if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                                    &semantic.types().type(expression.type.resolved()).value
                                );
                                builtin != nullptr && builtin->kind == BuiltinType::F32) {
                                ++floating_count;
                                expect(
                                    evaluation_rule(semantic, expression).action
                                    == EvaluationAction::Required
                                );
                            }
                        }
                        if (const auto* cast = std::get_if<SemCast>(&expression.value);
                            cast != nullptr && cast->kind == CastKind::IntegerToFloating) {
                            ++floating_count;
                            expect(
                                evaluation_rule(semantic, expression).action
                                == EvaluationAction::Required
                            );
                        }
                        if (const auto* logic = std::get_if<SemShortCircuit>(&expression.value)) {
                            ++logic_count;
                            const auto skipped = known_boolean(semantic, *logic->left) == false;
                            expect(
                                known_boolean(semantic, expression)
                                == (skipped ? std::optional(false) : std::nullopt)
                            );
                            expect(
                                (evaluation_rule(semantic, expression).operands[1] == nullptr)
                                == skipped
                            );
                        }
                    }
                );
            }
            expect(comparison_count == 1uz);
            expect(logic_count == 2uz);
            expect(checked_count == 1uz);
            expect(floating_count == 2uz);
        };

    "Generation: discarded results retain calls and checked arithmetic"_test = [] static noexcept {
        const auto semantic_owner = analyze_test_program(R"(
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
        )");
        const auto compilation = PlannedCompilation::build(
            semantic_owner,
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("discarded_operations"),
             .library = std::nullopt}
        );

        struct Query final {
            std::flat_map<std::string, std::size_t> calls;
            std::size_t checked_divisions;
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

        auto query = Query {.calls = {}, .checked_divisions = 0uz};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            if (!expect(traverse_target_unit(unit.sections(), query))) {
                return;
            }
        }
        expect(query.calls["scalar"] >= 1uz);
        expect_equal(query.calls["record"], 1uz);
        expect_equal(query.calls["tag"], 1uz);
        expect_equal(query.calls["effect"], 1uz);
        expect(query.checked_divisions == 1uz);
    };

    "Generation: nested expressions retain each runtime operand once"_test = [] static noexcept {
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
        const auto semantic_owner = analyze_test_program(std::move(source));
        const auto compilation = PlannedCompilation::build(
            semantic_owner,
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("long_expression"),
             .library = std::nullopt}
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
            expect(traverse_target_unit(unit.sections(), query));
        }
        expect(query.calls == count);
    };

    "Generation: integer facts select direct operations and preserve required operands"_test =
        [] static noexcept {
            const auto semantic_owner = analyze_test_program(R"(
            fn effect() -> i32 => 8;
            fn quotient(value: i32) -> i32 => value / 2;
            fn remainder(value: u8) -> u8 => value % 3;
            fn shift(value: i16) -> i16 => value >> 2;
            fn wrap(value: u32) -> u32 => value * 3 + 1;
            fn discard() { effect() / 2; effect() / -1; effect() >> 2; }
        )");
            const auto compilation = PlannedCompilation::build(
                semantic_owner,
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("integer_facts"),
                 .library = std::nullopt}
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
                        if (!expect(name != nullptr)) {
                            return false;
                        }
                        expect(name->name.components().back().spelling() == "effect");
                        ++effects;
                    }
                    return true;
                }
            };

            auto query = Query();
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            expect(query.operations == 5uz);
            expect(query.effects == 3uz);
        };
});

} // namespace
