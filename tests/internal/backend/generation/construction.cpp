module carven:test.internal.backend.generation.construction;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
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
});

} // namespace
