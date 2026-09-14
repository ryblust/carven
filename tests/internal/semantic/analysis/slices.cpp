module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.slices;

import :semantic.semir.evaluation;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic slices: array extents survive views and immutable local copies") {
    struct Scenario final {
        std::string_view body;
        std::optional<std::uint64_t> length;
    };

    const auto scenarios = std::to_array<Scenario>({
        {"return values.as_slice().len();", 4u},
        {"let view = values.as_slice(); return view.len();", 4u},
        {"let view: [i32] = values; let copy = view; return copy.len();", 4u},
        {"let view = values.as_slice(); let copy = &&view; return copy.len();", 4u},
        {"return values.as_slice().slice(1, 3).len();", 2u},
        {"return dynamic.slice(1, 3).len();", 2u},
        {"let first: usize = 1; let last: usize = 3; return dynamic.slice(first, last).len();", 2u},
        {"return dynamic.slice(9, 9).len();", 0u},
        {"return dynamic.slice(3, 1).len();", std::nullopt},
        {"return dynamic.slice(start, end).len();", std::nullopt},
        {"return dynamic.len();", std::nullopt},
        {"var view: [i32] = values; view = dynamic; return view.len();", std::nullopt},
        {"var first: usize = 1; return dynamic.slice(first, 3).len();", std::nullopt},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.body);
        const auto program = analyze_test_program(
            std::format(
                "fn probe(values: [i32; 4], dynamic: [i32], start: usize, end: usize) -> usize {{ {} }}",
                scenario.body
            )
        );
        auto queries = 0uz;
        for (const auto entry : program.bodies().entries()) {
            visit_semantic_nodes(
                entry.value.region(),
                [&](const SemanticExpression& expression) noexcept {
                    const auto* slice = std::get_if<SemSliceIntrinsic>(&expression.value);
                    if (slice == nullptr || slice->intrinsic != SliceIntrinsic::Len) {
                        return;
                    }
                    ++queries;
                    REQUIRE(expression.constant.has_value() == scenario.length.has_value());
                    CHECK_FALSE(slice->result_extent.has_value());
                    if (expression.constant) {
                        const auto* integer = std::get_if<IntegerConstant>(
                            &program.constants().constant(*expression.constant).value
                        );
                        REQUIRE(integer != nullptr);
                        CHECK(integer->as_unsigned() == scenario.length);
                    }
                }
            );
        }
        CHECK(queries == 1uz);
    }
}

TEST_CASE("Semantic slices: known lengths retain checked operations and receiver evaluation") {
    const auto program = analyze_test_program(
        "fn create() -> [i32; 4] => [1, 2, 3, 4]; "
        "fn direct() -> usize => create().as_slice().len(); "
        "fn checked(values: [i32]) -> bool => values.slice(9, 9).is_empty(); "
        "fn indexed(values: [i32; 4], index: usize) -> i32 => values.as_slice()[index];"
    );
    auto known_queries = 0uz;
    auto calls = 0uz;
    auto checked_slices = 0uz;
    auto checked_indices = 0uz;
    for (const auto entry : program.bodies().entries()) {
        visit_semantic_nodes(
            entry.value.region(),
            [&](const SemanticExpression& expression) noexcept {
                calls += std::holds_alternative<SemCall>(expression.value);
                if (const auto* index = std::get_if<SemIndex>(&expression.value)) {
                    CHECK(std::holds_alternative<RuntimeCheckedBounds>(index->bounds));
                    ++checked_indices;
                }
                const auto* slice = std::get_if<SemSliceIntrinsic>(&expression.value);
                if (slice == nullptr) {
                    return;
                }
                if (slice->intrinsic == SliceIntrinsic::Slice) {
                    ++checked_slices;
                    CHECK(slice->result_extent == 0u);
                    CHECK(
                        evaluation_rule(program, expression).action == EvaluationAction::Required
                    );
                } else if (slice->intrinsic == SliceIntrinsic::Len
                           || slice->intrinsic == SliceIntrinsic::IsEmpty) {
                    ++known_queries;
                    CHECK(expression.constant.has_value());
                    CHECK(
                        evaluation_rule(program, expression).action == EvaluationAction::Operands
                    );
                    if (slice->intrinsic == SliceIntrinsic::IsEmpty) {
                        CHECK(known_boolean(program, expression) == true);
                    }
                }
            }
        );
    }
    CHECK(known_queries == 2uz);
    CHECK(calls == 1uz);
    CHECK(checked_slices == 1uz);
    CHECK(checked_indices == 1uz);
}

TEST_CASE("Semantic slices: zero extent is a known fact for array views") {
    const auto program = analyze_test_program(
        "fn probe(values: [i32; 0]) -> bool { let view: [i32] = values; return view.is_empty(); }"
    );
    auto views = 0uz;
    auto queries = 0uz;
    for (const auto entry : program.bodies().entries()) {
        visit_semantic_nodes(
            entry.value.region(),
            [&](const SemanticExpression& expression) noexcept {
                const auto* slice = std::get_if<SemSliceIntrinsic>(&expression.value);
                if (slice == nullptr) {
                    return;
                }
                if (slice->intrinsic == SliceIntrinsic::FromArray) {
                    ++views;
                    CHECK(slice->result_extent == 0u);
                } else if (slice->intrinsic == SliceIntrinsic::IsEmpty) {
                    ++queries;
                    CHECK(known_boolean(program, expression) == true);
                }
            }
        );
    }
    CHECK(views == 1uz);
    CHECK(queries == 1uz);
}
