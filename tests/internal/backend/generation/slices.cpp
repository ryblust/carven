module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.slices;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Generation: known array view queries need no runtime view construction") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(
            "fn length(values: [i32; 4]) -> usize => values.as_slice().len(); "
            "fn empty(values: [i32; 0]) -> bool => values.as_slice().is_empty();"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("slice_queries")}
    );

    struct Query final {
        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
            if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                CHECK(intrinsic->symbol != TargetSymbol::RuntimeAsSlice);
            }
            CHECK_FALSE(std::holds_alternative<TargetCallExpr>(expression.value));
            return true;
        }
    };

    auto query = Query();
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        CHECK(traverse_target_unit(unit.sections(), query));
    }
}

TEST_CASE("Generation: formatting a known subslice length preserves its checked slice") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(
            "fn format(values: [i32]) -> String { return f\"{values.slice(0, 2).len()}\"; }"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("slice_checked_format")}
    );

    struct Query final {
        std::size_t slices = 0uz;

        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
            if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                CHECK(intrinsic->symbol != TargetSymbol::RuntimeFormat);
                CHECK(intrinsic->symbol != TargetSymbol::RuntimeFormatValidUTF8);
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
        CHECK(traverse_target_unit(unit.sections(), query));
    }
    CHECK(query.slices == 1uz);
}
