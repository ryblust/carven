module carven:test.internal.backend.generation.patterns;

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
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

struct PatternQuery final {
    std::size_t comparisons;
    std::size_t unreachable;
    std::size_t bound_calls;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto PatternQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* binary = std::get_if<TargetBinaryExpr>(&expression.value)) {
        comparisons += binary->op == TargetBinaryOperator::Equal
            || binary->op == TargetBinaryOperator::GreaterEqual
            || binary->op == TargetBinaryOperator::Less
            || binary->op == TargetBinaryOperator::LessEqual;
    }
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* name =
                std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value)) {
            bound_calls += name->name.components().back().spelling() == "bound";
        }
    }
    return true;
}

auto PatternQuery::enter_statement(const TargetStmt& statement) noexcept -> bool {
    unreachable += std::holds_alternative<TargetUnreachableStmt>(statement.value);
    return true;
}

const ct::Suite tests([] static noexcept {
    ct::test("Generation: matches omit proven residual comparisons", [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            std::size_t comparisons;
        };
        const auto cases = std::array {
            Case {
                .name = "boolean complement",
                .source = "fn select(value: bool) -> i32 => match value { "
                          "true => 1, false => 2, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "numeric enum complement",
                .source = "enum Choice { First, Second, } "
                          "fn select(value: Choice) -> i32 => match value { "
                          ".First => 1, .Second => 2, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "integer interval complement",
                .source = "fn select(value: i32) -> i32 => match value { "
                          "..0 => 1, 0.. => 2, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "guarded current pattern",
                .source = "fn select(value: bool, accept: bool) -> i32 => match value { "
                          "true => 1, false if accept => 2, false => 3, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "guarded prefix retains the complement test",
                .source = "fn select(value: bool, accept: bool) -> i32 => match value { "
                          "true if accept => 1, false => 2, true => 3, };",
                .comparisons = 2uz,
            },
            Case {
                .name = "pure accepting alternatives need no selection",
                .source = "fn select(value: bool) -> i32 => match value { "
                          "true | false => 1, };",
                .comparisons = 0uz,
            },
        };
        ct::each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(std::string(input.source)),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("pattern_selection")}
            );
            auto query = PatternQuery {.comparisons = 0uz, .unreachable = 0uz, .bound_calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect_equal(query.comparisons, input.comparisons);
            ct::expect_equal(query.unreachable, 0uz);
        });
    });

    ct::test(
        "Generation: exhaustive selection retains dynamic bound evaluation",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                fn bound(&calls: i32) -> i32 { calls += 1; return 0; }
                fn select(value: i32, &calls: i32) -> i32 => match value {
                    (bound(&calls))..10 => 1,
                    _ => 2,
                };
            )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("pattern_bound")}
            );
            auto query = PatternQuery {.comparisons = 0uz, .unreachable = 0uz, .bound_calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect_equal(query.bound_calls, 1uz);
            ct::expect_equal(query.unreachable, 0uz);
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
                // Doubling independent choices must not expand their Cartesian product.
                // Leave room for target scaffolding without fixing names or exact counts.
                ct::expect(counts[index] <= 3uz * counts[index - 1uz]);
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
                    if (const auto* name = std::get_if<TargetNameExpr>(
                            &template_primary_expression(*call->callee).value
                        )) {
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
});

} // namespace
