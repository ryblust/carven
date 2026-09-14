module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.constant_store;

import :test.internal.semantic.evaluation.fixture;
import std;

TEST_CASE(
    "SemIR constants: interning growth preserves borrowed facts spellings and floating identity"
) {
    auto fixture = ConstantEvaluationFixture();
    auto& draft = fixture.compilation;
    const auto integer = draft.intern_builtin_type(BuiltinType::I32);
    const auto first = draft.intern_constant({.type = integer, .value = IntegerConstant::zero()});
    const auto* fact = &draft.constant(first);
    const auto spelling =
        draft.intern_spelling("a stable spelling longer than small-string storage");
    const auto text = draft.spelling(spelling);
    for (auto value = 1; value < 65536; ++value) {
        static_cast<void>(
            draft.intern_constant({.type = integer, .value = IntegerConstant::from_signed(value)})
        );
        if (value % 64 == 0) {
            static_cast<void>(draft.intern_spelling(std::to_string(value)));
        }
    }
    CHECK(&draft.constant(first) == fact);
    CHECK(draft.spelling(spelling).data() == text.data());
    CHECK(draft.intern_constant({.type = integer, .value = IntegerConstant::zero()}) == first);
    const auto floating = draft.intern_builtin_type(BuiltinType::F64);
    const auto bits =
        std::array {0ull, 0x8000000000000000ull, 0x7ff8000000000001ull, 0x7ff8000000000002ull};
    auto identities = std::set<ConstantID>();
    for (const auto pattern : bits) {
        const auto value = ConstantFact {
            .type = floating,
            .value = F64Constant {.value = std::bit_cast<double>(pattern)}
        };
        const auto id = draft.intern_constant(value);
        CHECK(draft.intern_constant(value) == id);
        identities.insert(id);
    }
    CHECK(identities.size() == bits.size());
}
