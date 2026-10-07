module carven:test.internal.backend.generation.constant_simd;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.traversal;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct ConstantQuery final {
    std::string_view function;
    std::size_t constructors;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto leave_declaration(const TargetDecl&) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto ConstantQuery::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* callable = std::get_if<TargetFunctionDecl>(&declaration);
    if (callable && std::holds_alternative<TargetFreeFunctionDefinition>(callable->form)) {
        function = callable->name.components().back().spelling();
    }
    return true;
}

auto ConstantQuery::leave_declaration(const TargetDecl&) noexcept -> bool {
    function = {};
    return true;
}

auto ConstantQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
    const auto* member = call ? std::get_if<TargetStaticMemberExpr>(&call->callee->value) : nullptr;
    if (!function.starts_with("frozen_") || member == nullptr) {
        return true;
    }
    const auto mask = function.starts_with("frozen_mask");
    const auto mixed = function.starts_with("frozen_mixed");
    expect_equal(member->name.spelling(), mask ? "from_bits" : mixed ? "from_lanes" : "splat");
    const auto count = function == "frozen_mixed_bytes" ? 16uz : mixed ? 4uz : 1uz;
    if (!expect_equal(call->arguments.size(), count)) {
        return false;
    }
    if (mask) {
        const auto* literal = std::get_if<TargetLiteralExpr>(&call->arguments.front().value);
        const auto* bits = literal ? std::get_if<TargetIntegerLiteral>(&literal->value) : nullptr;
        if (!expect(bits != nullptr)) {
            return false;
        }
        const auto expected = function == "frozen_mask4" ? 0u
            : function == "frozen_mask8"                 ? 255u
            : function == "frozen_mask16"                ? 7u
                                                         : 0xffffffffu;
        expect_equal(bits->magnitude, expected);
    }
    if (function == "frozen_mixed_zeros" || function == "frozen_floats4") {
        const auto zeros = function == "frozen_mixed_zeros" ? 2uz : 1uz;
        for (auto index = 0uz; index < zeros; ++index) {
            const auto* literal = std::get_if<TargetLiteralExpr>(&call->arguments[index].value);
            const auto* floating =
                literal ? std::get_if<TargetFloatLiteral>(&literal->value) : nullptr;
            const auto* value = floating ? std::get_if<float>(&floating->value) : nullptr;
            if (!expect(value != nullptr)) {
                return false;
            }
            expect_equal(
                std::bit_cast<std::uint32_t>(*value),
                function == "frozen_mixed_zeros" && index == 0uz ? 0u : 0x80000000u
            );
        }
    }
    ++constructors;
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: SIMD constants compact equal lane bits and retain mixed lanes"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                    fn frozen_bytes16() -> u8x16 { const v = u8x16::from_array([7; 16]); return v; }
                    fn frozen_bytes32() -> u8x32 { const v = u8x32::from_array([255; 32]); return v; }
                    fn frozen_floats4() -> f32x4 { const v = f32x4::from_array([-0.0; 4]); return v; }
                    fn frozen_floats8() -> f32x8 { const v = f32x8::from_array([1.5; 8]); return v; }
                    fn frozen_mixed_bytes() -> u8x16 {
                        const v = u8x16::splat(7).with_lane(0, 9); return v;
                    }
                    fn frozen_mixed_zeros() -> f32x4 {
                        const v = f32x4::from_array([0.0, -0.0, 0.0, 0.0]); return v;
                    }
                    fn frozen_mask4() -> mask4 { const v = mask4::prefix(0); return v; }
                    fn frozen_mask8() -> mask8 { const v = mask8::prefix(8); return v; }
                    fn frozen_mask16() -> mask16 { const v = mask16::prefix(3); return v; }
                    fn frozen_mask32() -> mask32 { const v = mask32::prefix(32); return v; }
                )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("constant_simd")}
            );
            auto constructors = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = ConstantQuery {.function = {}, .constructors = 0uz};
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                constructors += query.constructors;
            }
            expect_equal(constructors, 10uz);
        };
});

} // namespace
