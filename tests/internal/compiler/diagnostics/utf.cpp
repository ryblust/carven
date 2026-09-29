module carven:test.internal.compiler.diagnostics.utf;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

class UTFCompilation final {
public:
    explicit UTFCompilation(std::string_view application) noexcept {
        for (const auto name : {"error", "scan", "validation", "text"}) {
            const auto filename = std::format("crafts/carven/std/utf/{}.cv", name);
            auto input = std::ifstream(filename);
            ct::require(input.is_open());
            auto text = std::string(std::istreambuf_iterator<char>(input), {});
            const auto source = sources.append_virtual(filename, std::move(text));
            ct::require(source.has_value());
            inputs.push_back(
                {.source_id = *source,
                 .module_path = *CanonicalModulePath::from_value(
                     std::format("crafts.carven.std.utf.{}", name)
                 )}
            );
        }
        const auto application_source =
            sources.append_virtual("application.cv", std::string(application));
        ct::require(application_source.has_value());
        inputs.push_back(
            {.source_id = *application_source,
             .module_path = *CanonicalModulePath::from_value("application")}
        );
    }

    auto run() noexcept {
        return compile(
            sources,
            SourceBatch {.modules = inputs},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = *LinkageDomain::explicit_value("test:utf"),
            }
        );
    }

    SourceManager sources;
    std::vector<SourceModuleInput> inputs;
};

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("UTF craft: returned text retains input storage", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "returned text cannot borrow a local array",
             .source =
                 "fn bad() -> str throw UTF8Error { let a: [u8; 1] = [65]; return from_utf8(a)?; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "a"},
            {.name = "returned text retains an otherwise unnamed input view",
             .source =
                 "fn bad() throw UTF8Error { var a: [u8; 1] = [65]; let text = from_utf8(a)?; a[0] = 66; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "a[0] = 66"},
            {.name = "returned text cannot retain a temporary array",
             .source = "fn bad() throw UTF8Error { let text = from_utf8([65])?; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "let text = from_utf8([65])?"},

        });
        ct::each(cases, &CompilerErrorExpectation::name, [&](const auto& item) noexcept {
            auto fixture = UTFCompilation(
                std::string(
                    "import std::utf.text using from_utf8; import std::utf.error using UTF8Error; "
                )
                + std::string(item.source)
            );
            const auto result = fixture.run();
            if (!(ct::expect(!(result.has_value())))) {
                return;
            }
            if (!ct::expect_diagnostic(result.error(), item.code)) {
                return;
            }
            const auto* diagnostic = ct::find_diagnostic(result.error(), item.code);
            if (!(ct::expect(diagnostic->attachment.primary.has_value()))) {
                return;
            }
            const auto* expected = std::get_if<std::string_view>(&item.primary_text);
            if (!(ct::expect(expected != nullptr))) {
                return;
            }
            ct::expect_equal(
                fixture.sources.slice(diagnostic->attachment.primary->span),
                *expected
            );
        });
    });

    ct::test(
        "Compiler diagnostics: unchecked text construction checks types and backing",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "scalar input must be u32",
                 .source = "fn bad(v: i32) { let c = char::from_u32_unchecked(v); }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "v"},
                {.name = "text input must contain bytes",
                 .source = "fn bad(v: [i32]) { let s = str::from_utf8_unchecked(v); }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "v"},
                {.name = "text construction requires one argument",
                 .source = "fn bad() { let s = str::from_utf8_unchecked(); }",
                 .code = DiagnosticCode::TypeMethodCallArity,
                 .primary_text = "str::from_utf8_unchecked()"},
                {.name = "text construction cannot return local storage",
                 .source =
                     "fn bad() -> str { let a: [u8; 1] = [65]; return str::from_utf8_unchecked(a); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "a"},
                {.name = "borrowed text prevents String mutation",
                 .source =
                     "fn bad() { var s: String = \"hello\"; let text = str::from_utf8_unchecked(s.bytes); s.clear(); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "s.clear()"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Character construction: constant execution diagnoses invalid scalar preconditions",
        [] static noexcept {
            static constexpr auto cases = std::array {0xd800u, 0xdfffu, 0x110000u, 0xffffffffu};
            ct::each(
                cases,
                [](auto value) static noexcept { return std::format("value: {}", value); },
                [](auto value) static noexcept {
                    const auto call = std::format("char::from_u32_unchecked({}u32)", value);
                    check_compiler_error(
                        std::format("const invalid = {};", call),
                        DiagnosticCode::ConstEvaluation,
                        std::string_view(call)
                    );
                    check_compiler_error(
                        std::format(
                            "const fn make(value: u32) -> char => char::from_u32_unchecked(value); "
                            "const invalid = make({}u32);",
                            value
                        ),
                        DiagnosticCode::ConstEvaluation,
                        "char::from_u32_unchecked(value)"
                    );
                }
            );
        }
    );
});

} // namespace
