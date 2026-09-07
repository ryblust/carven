module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.evaluation;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.traversal;
import :semantic.semir;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Evaluation: known results retain source operations and execution obligations") {
    const auto semantic = semantic_analysis_test::analyze_program(
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
                        CHECK(known_boolean(semantic, expression) == true);
                        CHECK_FALSE(evaluation_requires_execution(semantic, expression));
                    }
                    if (binary->operation == BinaryOperator::Divide) {
                        ++checked_count;
                        CHECK(evaluation_requires_execution(semantic, expression));
                    }
                    if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                            &semantic.types().type(expression.type.resolved()).value
                        );
                        builtin != nullptr && builtin->kind == BuiltinType::F32) {
                        ++floating_count;
                        CHECK(evaluation_requires_execution(semantic, expression));
                    }
                }
                if (const auto* cast = std::get_if<SemCast>(&expression.value);
                    cast != nullptr && cast->kind == CastKind::IntegerToFloating) {
                    ++floating_count;
                    CHECK(evaluation_requires_execution(semantic, expression));
                }
                if (const auto* logic = std::get_if<SemShortCircuit>(&expression.value)) {
                    ++logic_count;
                    CHECK(known_boolean(semantic, expression) == false);
                    const auto skipped = known_boolean(semantic, *logic->left) == false;
                    CHECK(evaluation_requires_execution(semantic, expression) == !skipped);
                }
            }
        );
    }
    CHECK(comparison_count == 1uz);
    CHECK(logic_count == 2uz);
    CHECK(checked_count == 1uz);
    CHECK(floating_count == 2uz);
}

TEST_CASE("Generation: proven scalar results require no computation or discard scaffolding") {
    const auto compilation = PlannedCompilation::build(
        semantic_analysis_test::analyze_program(
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
        std::size_t calls = 0;
        auto enter_expression(const TargetExpr& expression) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetStaticCastExpr>(expression.value));
            CHECK_FALSE(std::holds_alternative<TargetBinaryExpr>(expression.value));
            CHECK_FALSE(std::holds_alternative<TargetLambdaExpr>(expression.value));
            CHECK_FALSE(std::holds_alternative<TargetConstructionExpr>(expression.value));
            calls += std::holds_alternative<TargetCallExpr>(expression.value);
            return true;
        }
        auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetBlockStmt>(statement.value));
            CHECK_FALSE(std::holds_alternative<TargetDiscardStmt>(statement.value));
            CHECK_FALSE(std::holds_alternative<TargetIfStmt>(statement.value));
            CHECK_FALSE(std::holds_alternative<TargetVariableStmt>(statement.value));
            return true;
        }
    };
    auto query = Query();
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.calls == 2uz);
}

TEST_CASE("Generation: native branches and calls need no enclosing artificial block") {
    const auto compilation = PlannedCompilation::build(
        semantic_analysis_test::analyze_program(
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
        std::size_t branches = 0;
        std::size_t calls = 0;
        auto enter_statement(const TargetStmt& statement) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetBlockStmt>(statement.value));
            CHECK_FALSE(std::holds_alternative<TargetVariableStmt>(statement.value));
            branches += std::holds_alternative<TargetIfStmt>(statement.value);
            return true;
        }
        auto enter_expression(const TargetExpr& expression) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetLambdaExpr>(expression.value));
            calls += std::holds_alternative<TargetCallExpr>(expression.value);
            return true;
        }
    };
    auto query = Query {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.branches == 1uz);
    CHECK(query.calls == 2uz);
}

TEST_CASE("Generation: discarded failing calls check success without projecting a payload") {
    const auto compilation = PlannedCompilation::build(
        semantic_analysis_test::analyze_program(
            "enum Error { Failed, }\n"
            "fn produce(flag: bool) -> i32 throw Error {\n"
            "  if flag { return 7; } else { throw Error::Failed; }\n"
            "}\n"
            "fn discard(flag: bool) throw Error { produce(flag)?; }\n"
            "fn deliver(flag: bool) -> i32 throw Error { return produce(flag)?; }\n"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("result_consumption")}
    );
    struct Query final {
        std::size_t payloads = 0;
        std::size_t success_checks = 0;
        std::size_t saved_successes = 0;
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
        auto enter_expression(const TargetExpr& expression) noexcept -> bool {
            success_checks += is_success(expression);
            if (const auto* member = std::get_if<TargetMemberExpr>(&expression.value)) {
                const auto* name = std::get_if<TargetIdentifier>(&member->name);
                payloads += name != nullptr && name->spelling() == "value";
            }
            return true;
        }
        auto enter_statement(const TargetStmt& statement) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetDiscardStmt>(statement.value));
            if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                saved_successes += is_success(variable->initializer);
            }
            return true;
        }
    };
    auto query = Query {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.success_checks == 2uz);
    CHECK(query.saved_successes == 1uz);
    CHECK(query.payloads == 1uz);
}

TEST_CASE("Generation: consumers use native branches and direct delivery") {
    const auto compilation = PlannedCompilation::build(
        semantic_analysis_test::analyze_program(
            "enum Failure { Stop, }\n"
            "fn checked(n: i32) -> i32 throw Failure {\n"
            "  if n < 0 { throw Failure::Stop; } return n;\n"
            "}\n"
            "fn initialized(flag: bool, n: i32) -> i32 throw Failure {\n"
            "  let value = if flag { checked(n)? } else { checked(0)? };\n"
            "  return value + 1;\n"
            "}\n"
            "fn effect(&n: i32) -> i32 { n += 1; return n; }\n"
            "fn ordered(&n: i32, flag: bool) -> bool {\n"
            "  return flag && (effect(&n) == effect(&n));\n"
            "}\n"
            "fn assigned(&out: bool, &n: i32, flag: bool) {\n"
            "  out = flag && (effect(&n) == effect(&n));\n"
            "}\n"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("consumer_delivery")}
    );
    struct Query final {
        auto enter_expression(const TargetExpr& expression) const noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetLambdaExpr>(expression.value));
            CHECK_FALSE(std::holds_alternative<TargetPlacementNewExpr>(expression.value));
            return true;
        }
    };
    auto query = Query {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
}
