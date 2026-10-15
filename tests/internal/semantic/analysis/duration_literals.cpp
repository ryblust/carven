module carven:test.internal.semantic.analysis.duration_literals;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

auto check_duration(
    std::string_view source,
    std::optional<DiagnosticCode> rejection = std::nullopt
) noexcept -> void {
    constexpr auto modules = std::to_array<std::string_view>({"time"});
    with_compiled_source(
        source,
        [&](const auto&, const auto& result) noexcept {
            if (!expect_equal(result.has_value(), !rejection).note("source = ", source)) {
                return;
            }
            if (rejection) {
                expect_diagnostic(result.error(), *rejection).note("source = ", source);
            }
        },
        modules
    );
}

const TestSuite suite([] static noexcept {
    "Duration literals: exact scaling and signed bounds preserve nanoseconds"_test =
        [] static noexcept {
            struct Case final {
                std::string_view literal;
                std::int64_t ticks;
            };
            constexpr auto cases = std::array {
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
                        "import std::time using Duration; const value: Duration = {}; "
                        "const {{ assert(value.nanoseconds == {}i64); }}",
                        input.literal,
                        input.ticks
                    )
                );
            }
        };

    "Duration literals: wildcard imports and const functions share the ordinary value"_test =
        [] static noexcept {
            check_duration(
                "import std::time using *; "
                "const fn delay() -> Duration { return 2ms; } "
                "const { assert(delay().nanoseconds == 2000000); }"
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
                DiagnosticCode::ConstLiteralRange
            );
        }
        check_duration(
            "import std::time using Duration; fn value() -> Duration { return 9223372037s; }",
            DiagnosticCode::ConstLiteralRange
        );
    };

    "Duration literals: fixed standard identity is independent of source bindings"_test =
        [] static noexcept {
            check_duration(
                "const value = 1ms; const { assert(value.nanoseconds == 1000000); } "
                "fn runtime() { let value = 2us; }"
            );
            check_duration(
                "struct Duration { nanoseconds: i64 } const value = 1ms; "
                "const { assert(value.nanoseconds == 1000000); }"
            );
            check_duration("const value: Duration = 1ms;", DiagnosticCode::TypeUnresolved);
            check_duration(
                "fn runtime() { let value: Duration = 1ms; }",
                DiagnosticCode::TypeUnresolved
            );
            check_duration(
                "struct Duration { nanoseconds: i64 } const value: Duration = 1ms;",
                DiagnosticCode::TypeMismatch
            );
            check_duration("const value: i64 = 1ms;", DiagnosticCode::TypeMismatch);
        };
});

} // namespace
