module carven:test.internal.semantic.semir.constant_store;

import :test.harness.framework;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "SemIR constants: interning growth preserves borrowed facts spellings and floating identity"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& draft = fixture.compilation;
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto first =
                draft.intern_constant({.type = integer, .value = IntegerConstant::zero()});
            const auto* fact = &draft.constant(first);
            const auto spelling =
                draft.intern_spelling("a stable spelling longer than small-string storage");
            const auto text = draft.spelling(spelling);
            for (auto value = 1; value < 65536; ++value) {
                static_cast<void>(draft.intern_constant(
                    {.type = integer, .value = IntegerConstant::from_signed(value)}
                ));
                if (value % 64 == 0) {
                    static_cast<void>(draft.intern_spelling(std::to_string(value)));
                }
            }
            expect(&draft.constant(first) == fact);
            expect(draft.spelling(spelling).data() == text.data());
            expect(
                draft.intern_constant({.type = integer, .value = IntegerConstant::zero()}) == first
            );
            const auto floating = draft.builtin_type(BuiltinType::F64);
            const auto bits = std::array {
                0ull,
                0x8000000000000000ull,
                0x7ff8000000000001ull,
                0x7ff8000000000002ull
            };
            auto identities = std::set<ConstantID>();
            for (const auto pattern : bits) {
                const auto value = ConstantFact {
                    .type = floating,
                    .value = F64Constant {.value = std::bit_cast<double>(pattern)}
                };
                const auto id = draft.intern_constant(value);
                expect(draft.intern_constant(value) == id);
                identities.insert(id);
            }
            expect(identities.size() == bits.size());
        };
    "SemIR constants: floating SIMD identity preserves each lane bit pattern"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& draft = fixture.compilation;
            const auto type = draft.builtin_type(BuiltinType::F32x4);
            const auto patterns =
                std::array<std::uint32_t, 4> {0u, 0x80000000u, 0x7fc00001u, 0x7fc00002u};
            auto identities = std::set<ConstantID>();
            for (const auto pattern : patterns) {
                auto value = SIMDConstant {.lanes = std::vector<std::uint32_t>(4uz)};
                value.lanes[2] = pattern;
                const auto fact = ConstantFact {.type = type, .value = value};
                const auto id = draft.intern_constant(fact);
                expect(draft.intern_constant(fact) == id);
                identities.insert(id);
            }
            expect(identities.size() == patterns.size());
        };
    "SemIR constants: SIMD lane encodings follow the owning type"_test = [] static noexcept {
        constexpr auto vectors = std::array {
            BuiltinType::U8x16,
            BuiltinType::U8x32,
            BuiltinType::F32x4,
            BuiltinType::F32x8
        };
        each(
            vectors,
            [](auto type) static noexcept -> std::string_view {
                switch (type) {
                    case BuiltinType::U8x16: return "u8x16";
                    case BuiltinType::U8x32: return "u8x32";
                    case BuiltinType::F32x4: return "f32x4";
                    case BuiltinType::F32x8: return "f32x8";
                    default:                 std::unreachable();
                }
            },
            [](auto type) static noexcept {
                const auto layout = *simd_layout(type);
                auto vector = SIMDConstant {.lanes = std::vector<std::uint32_t>(layout.width)};
                expect(matches_simd_constant(type, vector));
                vector.lanes.back() = 0xffffffffu;
                expect_equal(
                    matches_simd_constant(type, vector),
                    layout.element == BuiltinType::F32
                );
                vector.lanes.pop_back();
                expect(!matches_simd_constant(type, vector));
                auto mask = SIMDConstant {.lanes = std::vector<std::uint32_t>(layout.width, 255u)};
                expect(matches_simd_constant(layout.mask, mask));
                mask.lanes.back() = 1u;
                expect(!matches_simd_constant(layout.mask, mask));
            }
        );
    };
});

} // namespace
