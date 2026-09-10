module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.evaluation;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target;
import :semantic.semir.traversal;
import :semantic.semir;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Evaluation: known results retain source operations and execution obligations") {
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
                        CHECK(known_boolean(semantic, expression) == true);
                        CHECK(
                            evaluation_rule(semantic, expression).action
                            == EvaluationAction::Operands
                        );
                    }
                    if (binary->operation == BinaryOperator::Divide) {
                        ++checked_count;
                        CHECK(
                            evaluation_rule(semantic, expression).action
                            == EvaluationAction::Required
                        );
                    }
                    if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                            &semantic.types().type(expression.type.resolved()).value
                        );
                        builtin != nullptr && builtin->kind == BuiltinType::F32) {
                        ++floating_count;
                        CHECK(
                            evaluation_rule(semantic, expression).action
                            == EvaluationAction::Required
                        );
                    }
                }
                if (const auto* cast = std::get_if<SemCast>(&expression.value);
                    cast != nullptr && cast->kind == CastKind::IntegerToFloating) {
                    ++floating_count;
                    CHECK(
                        evaluation_rule(semantic, expression).action == EvaluationAction::Required
                    );
                }
                if (const auto* logic = std::get_if<SemShortCircuit>(&expression.value)) {
                    ++logic_count;
                    CHECK(known_boolean(semantic, expression) == false);
                    const auto skipped = known_boolean(semantic, *logic->left) == false;
                    CHECK(
                        (evaluation_rule(semantic, expression).operands[1] == nullptr) == skipped
                    );
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
        std::size_t calls = 0;

        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetStaticCastExpr>(expression.value));
            CHECK_FALSE(std::holds_alternative<TargetBinaryExpr>(expression.value));
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
        std::size_t branches = 0;
        std::size_t calls = 0;

        auto enter_statement(const TargetStmt& statement) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetBlockStmt>(statement.value));
            CHECK_FALSE(std::holds_alternative<TargetVariableStmt>(statement.value));
            branches += std::holds_alternative<TargetIfStmt>(statement.value);
            return true;
        }

        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
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
        analyze_test_program(
            "enum Error { Failed, }\n"
            "fn produce(flag: bool) -> i32 throw Error {\n"
            "  if flag { return 7; } else { throw Error::Failed; }\n"
            "}\n"
            "fn discard(flag: bool) throw Error { produce(flag)?; }\n"
            "fn discard_wrapped(flag: bool) throw Error { (produce(flag)? as i32) == 0; }\n"
            "fn discard_selected(flag: bool) throw Error { flag && (produce(flag)? == 0); }\n"
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

        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
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
    CHECK(query.success_checks == 4uz);
    CHECK(query.saved_successes == 1uz);
    CHECK(query.payloads == 1uz);
}

TEST_CASE("Generation: independent root calls initialize Outcomes without deferred storage") {
    constexpr auto bodies = std::array<std::string_view, 2uz> {
        "produce(flag)?;",
        "let value = produce(flag)?; observe(value);"
    };
    for (const auto body : bodies) {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(
                std::format(
                    "enum Error {{ Failed, }} "
                    "fn produce(flag: bool) -> i32 throw Error {{ if flag {{ return 7; }} throw Error::Failed; }} "
                    "fn observe(value: i32) {{}} "
                    "fn probe(flag: bool) throw Error {{ {} }}",
                    body
                )
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("root_outcome")}
        );

        struct Query final {
            const TargetUnit& unit;
            std::size_t direct = 0;
            std::size_t deferred = 0;

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
                    CHECK(std::holds_alternative<TargetCallExpr>(variable->initializer.value));
                }
                if (type->symbol == TargetSymbol::RuntimeDeferredResult) {
                    REQUIRE(type->type_argument_ids.size() == 1uz);
                    const auto* result = std::get_if<TargetIntrinsicType>(
                        &unit.type(type->type_argument_ids.front()).value
                    );
                    deferred += result != nullptr && result->symbol == TargetSymbol::RuntimeOutcome;
                }
                return true;
            }
        };

        auto direct = 0uz;
        auto deferred = 0uz;
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            auto query = Query {unit};
            CHECK(traverse_target_unit(unit.sections(), query));
            direct += query.direct;
            deferred += query.deferred;
        }
        CAPTURE(body);
        CHECK(direct == 1uz);
        CHECK(deferred == 0uz);
    }
}

TEST_CASE("Generation: scalar predecessors in a full expression need no deferred storage") {
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
                if (const auto* type =
                        std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value)) {
                    CHECK(type->symbol != TargetSymbol::RuntimeDeferredResult);
                }
            }
            return true;
        }
    };

    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto query = Query {unit};
        CHECK(traverse_target_unit(unit.sections(), query));
    }
}

TEST_CASE("Generation: independent value branches compose without duplicating successors") {
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
                std::size_t nodes = 0uz;
                std::size_t branches = 0uz;
                std::size_t calls = 0uz;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    ++nodes;
                    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                        if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                            calls += name->name.components().back().spelling() == "sum";
                        }
                    }
                    return true;
                }

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    ++nodes;
                    branches += std::holds_alternative<TargetIfStmt>(statement.value);
                    return true;
                }
            };

            auto query = Query {};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                CHECK(traverse_target_unit(unit.sections(), query));
            }
            CHECK(query.calls == 1uz);
            CHECK(query.branches == count * (suffix ? 2uz : 1uz));
            if (count == 1uz) {
                single_nodes = query.nodes;
            }
            CAPTURE(query.nodes);
            CHECK(query.nodes <= count * single_nodes);
        }
    }
}

TEST_CASE("Generation: void calls remain return expressions") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(
            "fn action() {}\n"
            "fn forward() { return action(); }\n"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("void_expression_delivery")}
    );

    struct Query final {
        std::size_t returned_calls = 0;

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
            CHECK_FALSE(std::holds_alternative<TargetLambdaExpr>(expression.value));
            return true;
        }
    };

    auto query = Query {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.returned_calls == 1uz);
}

TEST_CASE("Generation: builtin pointer observations need no temporary storage") {
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
        std::size_t comparisons = 0uz;
        std::size_t dereferences = 0uz;

        auto enter_statement(const TargetStmt& statement) const noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetVariableStmt>(statement.value));
            return true;
        }

        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
            CHECK_FALSE(std::holds_alternative<TargetLambdaExpr>(expression.value));
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

    auto query = Query {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.comparisons == 2uz);
    CHECK(query.dereferences == 1uz);
}

TEST_CASE("Generation: explicit writable source pointers retain their access contract") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(
            "fn writable(p: ptr<&i32>) -> ptr<i32> { let saved = p; return saved; }\n"
            "fn readonly(p: ptr<i32>) -> ptr<i32> { let saved = p; return saved; }\n"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("source_pointer_access")}
    );

    struct Query final {
        std::size_t declarations = 0uz;
        std::size_t contracts = 0uz;

        auto enter_statement(const TargetStmt& statement) noexcept -> bool {
            if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                ++declarations;
                contracts += variable->preserve_pointer_access;
                CHECK(variable->binding == TargetVariableBinding::ConstValue);
            }
            return true;
        }
    };

    auto query = Query {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.declarations == 2uz);
    CHECK(query.contracts == 1uz);
}

TEST_CASE("Generation: independent nested pattern alternatives keep target size proportional") {
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
            std::size_t nodes = 0uz;

            auto enter_expression(const TargetExpr&, TargetExpressionRole) noexcept -> bool {
                ++nodes;
                return true;
            }

            auto enter_statement(const TargetStmt&) noexcept -> bool {
                ++nodes;
                return true;
            }
        };

        auto query = Query {};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            CHECK(traverse_target_unit(unit.sections(), query));
        }
        counts.push_back(query.nodes);
    }
    REQUIRE(counts.front() > 0uz);
    for (auto index = 1uz; index < counts.size(); ++index) {
        CHECK(counts[index] > counts[index - 1uz]);
        // Doubling independent choices must not expand their Cartesian product.
        // Leave room for target scaffolding without fixing names or exact counts.
        CHECK(counts[index] <= 3uz * counts[index - 1uz]);
    }
}
