module carven:test.internal.semantic.analysis.constants;

import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
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
        "Semantic constants: required initializer failures do not enter runtime contracts",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Error {}
            const fn provider() -> i32 throw Error => 2;
            fn use() -> i32 { const value = provider()?; return value; }
        )");
            auto callable = std::optional<CallableID>();
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) == "use") {
                    callable = declaration.callable;
                }
            }
            if (!ct::expect(callable.has_value())) {
                return;
            }
            const auto& signature = program.callable_signatures().signature(
                program.declarations().callable(*callable).signature
            );
            ct::expect(program.failure_sets().failure_set(signature.failures).members.empty());
        }
    );

    ct::test(
        "Semantic constants: static control and argument failures belong to static roots",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Error {}
            const fn enabled() -> bool throw Error => true;
            const fn count() -> i32 throw Error => 2;
            const fn immediate() -> usize throw Error => 1usize;
            fn selected(const value: i32) -> i32 => value;
            fn use(vector: u8x16) -> u8x16 {
                const value = count()?;
                const if enabled()? { selected(count()?); }
                const for index in 0..count()? { selected(index); }
                return vector.shift_left(immediate()?);
            }
        )");
            auto callable = std::optional<CallableID>();
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) == "use") {
                    callable = declaration.callable;
                }
            }
            if (!ct::expect(callable.has_value())) {
                return;
            }
            const auto& signature = program.callable_signatures().signature(
                program.declarations().callable(*callable).signature
            );
            ct::expect(program.failure_sets().failure_set(signature.failures).members.empty());
        }
    );

    ct::test(
        "Static roots: declared values and extents hold their computed values",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const answer = (11 + 22) + 44;
        fn accept(value: [i32; (12 + 23) + 45]) {}
    )");
            ct::require_equal(program.declarations().module_constants().size(), 1uz);
            for (const auto [id, declaration] : program.declarations().module_constants()) {
                static_cast<void>(id);
                const auto* value = std::get_if<IntegerConstant>(
                    &program.constants().constant(declaration.value).value
                );
                if (ct::expect(value != nullptr)) {
                    ct::expect(value->as_signed() == 77);
                }
            }
            ct::require_equal(program.declarations().functions().size(), 1uz);
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                const auto& signature = program.callable_signatures().signature(
                    program.declarations().callable(declaration.callable).signature
                );
                ct::require_equal(signature.parameters.size(), 1uz);
                const auto* array = std::get_if<ArrayTypeValue>(
                    &program.types().type(signature.parameters.front().type).value
                );
                if (ct::expect(array != nullptr)) {
                    ct::expect(array->extent == 80u);
                }
            }
        }
    );
});

} // namespace
