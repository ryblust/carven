module carven:test.internal.backend.generation.reporting;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
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

struct ReportQuery final {
    const TargetUnit& unit;
    std::size_t branches;
    std::size_t writers;
    std::size_t assertions;
    std::size_t calls;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto ReportQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(
                &template_primary_expression(*call->callee).value
            )) {
            assertions += intrinsic->symbol == TargetSymbol::RuntimeAssertionFailed;
        }
        calls += std::holds_alternative<TargetNameExpr>(
            template_primary_expression(*call->callee).value
        );
    }
    return true;
}

auto ReportQuery::enter_statement(const TargetStmt& statement) noexcept -> bool {
    branches += std::holds_alternative<TargetIfStmt>(statement.value);
    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
        if (const auto* intrinsic =
                std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value)) {
            writers += intrinsic->symbol == TargetSymbol::RuntimeDisplayWriter;
        }
    }
    return true;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: static reports disappear while runtime conditions retain report work",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn touch() -> bool { return false; }
            fn successful() { assert(touch() || true, "skipped"); assert(1 == 1); }
            fn fatal() { assert(false); touch(); }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("reporting")}
            );
            auto branches = 0uz;
            auto writers = 0uz;
            auto assertions = 0uz;
            auto calls = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = ReportQuery {
                    .unit = unit,
                    .branches = 0uz,
                    .writers = 0uz,
                    .assertions = 0uz,
                    .calls = 0uz
                };
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                branches += query.branches;
                writers += query.writers;
                assertions += query.assertions;
                calls += query.calls;
            }
            ct::expect(branches == 1uz);
            ct::expect(writers == 1uz);
            ct::expect(assertions == 2uz);
            ct::expect(calls == 1uz);
        }
    );

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
});

} // namespace
