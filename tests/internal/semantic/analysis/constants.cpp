module carven:test.internal.semantic.analysis.constants;

import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Semantic constants: declarations publish values without executable bodies",
        [] static noexcept {
            const auto program = analyze_test_program(
                "enum Choice { Value(i32), Empty, }\n"
                "enum State: u8 { Ready = 4, Done, }\n"
                "const arithmetic: i64 = -2i64 + 5;\n"
                "const raw: i32 = State::Done as i32;\n"
                "const text_size: usize = \"abc\".len();\n"
                "const same = Choice::Value(2) == .Value(2);\n"
            );
            ct::expect_equal(program.bodies().size(), 0uz);
            auto values = std::vector<ConstantID>();
            for (const auto [id, declaration] : program.declarations().module_constants()) {
                static_cast<void>(id);
                values.push_back(declaration.value);
            }
            if (!ct::expect_equal(values.size(), 4uz)) {
                return;
            }
            const auto expected_integers = std::array {3ll, 5ll, 3ll};
            for (auto index = 0uz; index < expected_integers.size(); ++index) {
                const auto* integer = std::get_if<IntegerConstant>(
                    &program.constants().constant(values[index]).value
                );
                if (!ct::expect(integer != nullptr)) {
                    return;
                }
                ct::expect(((integer->as_signed()) == (expected_integers[index])))
                    .note("integer->as_signed() == expected_integers[index]");
            }
            const auto* equality =
                std::get_if<BooleanConstant>(&program.constants().constant(values.back()).value);
            if (!ct::expect(equality != nullptr)) {
                return;
            }
            ct::expect(equality->value);
        }
    );

    ct::test(
        "Semantic constants: const calls execute in local constant initializers",
        [] static noexcept {
            const auto program = analyze_test_program(
                "const fn source() -> i32 { return 1; } "
                "fn use() { const _ = (source() == 1) && false; }"
            );
            ct::expect_equal(program.declarations().functions().size(), 2uz);
        }
    );

    ct::test(
        "Constant roots: arithmetic intermediates and extent results are not retained",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const answer = (11 + 22) + 44;
        fn accept(value: [i32; (12 + 23) + 45]) {}
    )");
            for (const auto [id, fact] : program.constants().entries()) {
                static_cast<void>(id);
                if (const auto* integer = std::get_if<IntegerConstant>(&fact.value)) {
                    ct::expect(integer->as_signed() != 33);
                    ct::expect(integer->as_signed() != 35);
                    ct::expect(integer->as_signed() != 80);
                }
            }
        }
    );
});

} // namespace
