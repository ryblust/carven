module carven:test.internal.semantic.analysis.duration_literals;

import :diagnostics.code;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

auto check_duration(
    std::string_view source,
    std::optional<std::int64_t> ticks,
    std::optional<DiagnosticCode> rejection = std::nullopt,
    std::string_view standard = "export struct Duration { nanoseconds: i64 }"
) noexcept -> void {
    auto sources = SourceManager();
    const auto application = sources.append_virtual("duration.cv", std::string(source));
    const auto duration = sources.append_virtual("std_duration.cv", std::string(standard));
    require(application.has_value() && duration.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *application,
            .module_path = *CanonicalModulePath::from_value("duration")
        },
        SourceModuleInput {
            .source_id = *duration,
            .module_path = *CanonicalModulePath::from_value("crafts.carven.std.time.duration")
        }
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value()).note("source = ", source);
    auto result = analyze(std::move(*syntax));
    if (!expect_equal(result.has_value(), !rejection).note("source = ", source)) {
        return;
    }
    if (rejection) {
        expect_diagnostic(result.error(), *rejection).note("source = ", source);
        return;
    }
    if (!ticks) {
        return;
    }
    const auto& program = result->value;
    auto count = 0uz;
    for (const auto [id, declaration] : program.declarations().module_constants()) {
        static_cast<void>(id);
        ++count;
        const auto& value = program.constants().constant(declaration.value);
        const auto* fields = std::get_if<StructConstant>(&value.value);
        if (fields && !expect_equal(fields->fields.size(), 1uz)) {
            continue;
        }
        const auto& scalar = fields ? program.constants().constant(fields->fields.front()) : value;
        const auto* integer = std::get_if<IntegerConstant>(&scalar.value);
        if (expect(integer != nullptr)) {
            expect_equal(integer->as_signed(), *ticks).note("source = ", source);
        }
    }
    expect_equal(count, 1uz);
}

const TestSuite suite([] static noexcept {
    "Duration literals: exact scaling and signed bounds form ordinary records"_test =
        [] static noexcept {
            struct Case final {
                std::string_view literal;
                std::int64_t ticks;
            };
            const auto cases = std::array {
                Case {"0ns", 0},
                Case {"12ns", 12},
                Case {"7us", 7'000},
                Case {"250ms", 250'000'000},
                Case {"3s", 3'000'000'000},
                Case {"-5ms", -5'000'000},
                Case {"0xffns", 255},
                Case {"0b11us", 3'000},
                Case {"0o7ms", 7'000'000},
                Case {"9223372036854775807ns", std::numeric_limits<std::int64_t>::max()},
                Case {"-9223372036854775808ns", std::numeric_limits<std::int64_t>::min()},
                Case {"-0x8000000000000000ns", std::numeric_limits<std::int64_t>::min()},
                Case {"9223372036s", 9'223'372'036'000'000'000}
            };
            for (const auto& input : cases) {
                check_duration(
                    std::format(
                        "import std::time.duration using Duration; const value = {}; "
                        "fn runtime() -> Duration {{ return {}; }}",
                        input.literal,
                        input.literal
                    ),
                    input.ticks
                );
            }
        };

    "Duration literals: wildcard imports and const functions share the ordinary value"_test =
        [] static noexcept {
            check_duration(
                "import std::time.duration using *; "
                "const fn delay() -> Duration { return 2ms; } "
                "const ticks = delay().nanoseconds;",
                2'000'000
            );
        };

    "Duration literals: overflow is rejected before runtime"_test = [] static noexcept {
        for (const auto literal :
             {"9223372036854775808ns",
              "-9223372036854775809ns",
              "9223372036854776us",
              "-9223372036854776us",
              "9223372036855ms",
              "9223372037s",
              "18446744073709551616ns"}) {
            check_duration(
                std::format("const value = {};", literal),
                std::nullopt,
                DiagnosticCode::ConstLiteralRange
            );
            check_duration(
                std::format(
                    "import std::time.duration using Duration; fn value() -> Duration {{ return {}; }}",
                    literal
                ),
                std::nullopt,
                DiagnosticCode::ConstLiteralRange
            );
        }
    };

    "Duration literals: fixed standard identity is independent of source bindings"_test =
        [] static noexcept {
            check_duration("const value = 1ms; fn runtime() { let value = 2us; }", 1'000'000);
            check_duration("struct Duration { nanoseconds: i64 } const value = 1ms;", 1'000'000);
            check_duration(
                "const value: Duration = 1ms;",
                std::nullopt,
                DiagnosticCode::TypeUnresolved
            );
            check_duration(
                "fn runtime() { let value: Duration = 1ms; }",
                std::nullopt,
                DiagnosticCode::TypeUnresolved
            );
            check_duration(
                "struct Duration { nanoseconds: i64 } const value: Duration = 1ms;",
                std::nullopt,
                DiagnosticCode::TypeMismatch
            );
            check_duration(
                "const value = 1ms;",
                std::nullopt,
                DiagnosticCode::TypeMismatch,
                "export struct Duration { nanoseconds: i32 }"
            );
            check_duration("const value: i64 = 1ms;", std::nullopt, DiagnosticCode::TypeMismatch);
        };
});

} // namespace
