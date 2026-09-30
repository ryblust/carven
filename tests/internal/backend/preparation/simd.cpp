module carven:test.internal.backend.preparation.simd;

import :backend.preparation;
import :semantic.semir.simd;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "SIMD preparation: only proven in-range controls omit runtime checks",
        [] static noexcept {
            const auto program = analyze_test_program(
                "fn probe(v: u8x16, f: f32x8, index: usize) {"
                "v.lane(0); v.lane(15); v.lane(16); v.lane(index);"
                "v.with_lane(15, 7); v.with_lane(16, 7);"
                "f.lane(7); f.lane(8); f.with_lane(index, 1.0);"
                "}"
            );
            const auto expected = std::array<std::optional<std::uint64_t>, 9> {
                0,
                15,
                std::nullopt,
                std::nullopt,
                15,
                std::nullopt,
                7,
                std::nullopt,
                std::nullopt
            };
            auto count = 0uz;
            for (const auto body : program.bodies().entries()) {
                visit_semantic_nodes(
                    body.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* intrinsic = std::get_if<SemIntrinsic>(&expression.value);
                        if (intrinsic == nullptr
                            || !std::holds_alternative<SIMDIntrinsic>(intrinsic->operation)) {
                            return;
                        }
                        if (!ct::expect(count < expected.size())) {
                            return;
                        }
                        const auto prepared = prepare_operation(program, expression);
                        const auto* control = std::get_if<PreparedSIMDLane>(prepared.get());
                        ct::expect_equal(control != nullptr, expected[count].has_value())
                            .note("operation: ", count);
                        if (control != nullptr && expected[count]) {
                            ct::expect_equal(control->index, *expected[count])
                                .note("operation: ", count);
                        }
                        ++count;
                    }
                );
            }
            ct::expect_equal(count, expected.size());
        }
    );
});

} // namespace
