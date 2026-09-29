module carven:test.internal.backend.generation.slices;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: known array view queries need no runtime view construction",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn length(values: [i32; 4]) -> usize => values.as_slice().len(); "
                    "fn empty(values: [i32; 0]) -> bool => values.as_slice().is_empty();"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("slice_queries")}
            );

            struct Query final {
                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    if (const auto* intrinsic =
                            std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                        ct::expect(intrinsic->symbol != TargetSymbol::RuntimeAsSlice);
                    }
                    ct::expect(!(std::holds_alternative<TargetCallExpr>(expression.value)));
                    return true;
                }
            };

            auto query = Query();
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
        }
    );

    ct::test(
        "Generation: formatting a known subslice length preserves its checked slice",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn format(values: [i32]) -> String { return f\"{values.slice(0, 2).len()}\"; }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("slice_checked_format")}
            );

            struct Query final {
                std::size_t slices = 0uz;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    if (const auto* intrinsic =
                            std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                        ct::expect(intrinsic->symbol != TargetSymbol::RuntimeFormat);
                        ct::expect(intrinsic->symbol != TargetSymbol::RuntimeFormatValidUTF8);
                    }
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call == nullptr) {
                        return true;
                    }
                    const auto* member = std::get_if<TargetMemberExpr>(&call->callee->value);
                    if (member == nullptr) {
                        return true;
                    }
                    const auto* name = std::get_if<TargetIdentifier>(&member->name);
                    slices += name != nullptr && name->spelling() == "slice";
                    return true;
                }
            };

            auto query = Query();
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.slices == 1uz);
        }
    );

    ct::test(
        "Generation: known slice results retain checks without result storage or queries",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn length(values: [i32]) -> usize => values.slice(0, 2).len(); "
                    "fn empty(values: [i32]) -> bool => values.slice(2, 2).is_empty();"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("slice_effectful_results")}
            );

            struct Query final {
                std::size_t checks;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    ct::expect(!(std::holds_alternative<TargetVariableStmt>(statement.value)));
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* member = std::get_if<TargetMemberExpr>(&expression.value);
                    if (member == nullptr) {
                        return true;
                    }
                    const auto* name = std::get_if<TargetIdentifier>(&member->name);
                    if (name != nullptr) {
                        ct::expect(name->spelling() != "size");
                        ct::expect(name->spelling() != "empty");
                        checks += name->spelling() == "slice";
                    }
                    return true;
                }
            };

            auto query = Query {.checks = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect(query.checks == 2uz);
        }
    );
});

} // namespace
