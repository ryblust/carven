module carven:test.internal.backend.generation.delivery;

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

const TestSuite suite([] static noexcept {
    "Generation: native branches and calls need no enclosing artificial block"_test =
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
                    expect(!(std::holds_alternative<TargetBlockStmt>(statement.value)));
                    expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                    branches += std::holds_alternative<TargetIfStmt>(statement.value);
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                    calls += std::holds_alternative<TargetCallExpr>(expression.value);
                    return true;
                }
            };

            auto query = Query {.branches = 0uz, .calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect(query.branches == 1uz);
            expect(query.calls == 2uz);
        };

    "Generation: cleanup-free prefixes deliver initializers and conditions directly"_test =
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
            each(cases, &Case::name, [](const Case& input) static noexcept {
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
                            expect(type->symbol != TargetSymbol::RuntimeDeferredResult);
                            expect(
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
                    expect(traverse_target_unit(unit.sections(), query));
                }
            });
        };

    "Generation: independent value branches compose without duplicating successors"_test =
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
                        std::size_t calls;

                        auto enter_expression(
                            const TargetExpr& expression,
                            TargetExpressionRole
                        ) noexcept -> bool {
                            ++nodes;
                            if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                                if (const auto* name = std::get_if<TargetNameExpr>(
                                        &template_primary_expression(*call->callee).value
                                    )) {
                                    calls += name->name.components().back().spelling() == "sum";
                                }
                            }
                            return true;
                        }

                        auto enter_statement(const TargetStmt&) noexcept -> bool {
                            ++nodes;
                            return true;
                        }
                    };

                    auto query = Query {.nodes = 0uz, .calls = 0uz};
                    for (const auto artifact : compilation.target().artifacts()) {
                        const auto unit = lower_artifact(compilation, artifact.id);
                        expect(traverse_target_unit(unit.sections(), query));
                    }
                    expect(query.calls == 1uz);
                    if (count == 1uz) {
                        single_nodes = query.nodes;
                    }
                    expect(query.nodes <= count * single_nodes).note("query.nodes: ", query.nodes);
                }
            }
        };

    "Generation: structured values deliver into their destination without factories"_test =
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
                    expect(!(std::holds_alternative<TargetLambdaExpr>(expression.value)));
                    return true;
                }
            };

            auto query = Query {.transfers = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                require(traverse_target_unit(unit.sections(), query));
            }
            expect_equal(query.transfers, 0uz);
        };
});

} // namespace
