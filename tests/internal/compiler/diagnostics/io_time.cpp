module carven:test.internal.compiler.diagnostics.io_time;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

constexpr auto standard_modules = std::to_array<std::string_view>({
    "io",
    "time",
    "utf",
    "utf.scan",
    "utf.block",
    "simd.bytes",
});

const TestSuite suite([] static noexcept {
    "Duration literals: the standard declaration must provide signed nanosecond storage"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto application = sources.append_virtual("app.cv", "const delay = 1ms;");
            const auto time =
                sources.append_virtual("time.cv", "export struct Duration { nanoseconds: i32 }");
            require(application.has_value() && time.has_value());
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = *application,
                    .module_path = *CanonicalModulePath::from_value("app")
                },
                SourceModuleInput {
                    .source_id = *time,
                    .module_path = *CanonicalModulePath::from_value("crafts.carven.std.time")
                }
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = *LinkageDomain::explicit_value("test:duration-contract"),
                    .libraries = {},
                }
            );
            if (expect(!result.has_value())) {
                expect_diagnostic(result.error(), DiagnosticCode::TypeMismatch);
            }
        };

    "IO and Time: failure propagation and generic storage stay ordinary"_test = [] static noexcept {
        check_compiler_error(
            "import std::time using TimePoint; fn run() { TimePoint::after(1ns); }",
            DiagnosticCode::EffectUnmarked,
            "TimePoint::after(1ns)",
            standard_modules
        );
        with_compiled_source(
            "import std::io using ReadResult; struct Cycle { value: ReadResult<Cycle> }",
            [](const auto&, const auto& result) static noexcept {
                if (expect(!result.has_value())) {
                    expect_diagnostic(result.error(), DiagnosticCode::TypeRecursiveStorage);
                }
            },
            standard_modules
        );
    };
});

} // namespace
