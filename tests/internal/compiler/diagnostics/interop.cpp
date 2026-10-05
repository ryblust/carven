module carven:test.internal.compiler.diagnostics.interop;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: interop failures preserve code and precise span"_test = [] static noexcept {
        static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
            {
                .name = "import parameters require value types",
                .source = "import(cpp) fn invalid(value: void);",
                .code = DiagnosticCode::TypeValueRequired,
                .primary_text = "void",
            },
            {
                .name = "import Write requires mutable storage",
                .source = "import(cpp) fn change(&value: String); "
                          "fn f() { let value: String = \"text\"; change(&value); }",
                .code = DiagnosticCode::AccessImmutable,
                .primary_text = "&value",
            },
            {
                .name = "import Take consumes the source",
                .source =
                    "import(cpp) fn consume(&&value: String); "
                    "fn f() { let value: String = \"text\"; consume(&&value); consume(&&value); }",
                .code = DiagnosticCode::AccessUnavailable,
                .primary_text = "value",
            },
            {
                .name = "export signatures obey nominal visibility",
                .source = "private struct Hidden {} export(cpp) fn leak() => Hidden {};",
                .code = DiagnosticCode::TypeVisibilityLeak,
                .primary_text = "export(cpp) fn leak() => Hidden {};",
            },
            {
                .name = "import failures require propagation",
                .source = "struct Failure {} import(cpp) fn native() throw Failure; "
                          "private fn f() { native(); }",
                .code = DiagnosticCode::EffectUnmarked,
                .primary_text = "native()",
            },
            {
                .name = "unrepresentable std provider name",
                .source = "private import(cpp) fn std() -> i32;",
                .code = DiagnosticCode::CppIdentifier,
                .primary_text = "std",
            },
            {
                .name = "unrepresentable carven provider name",
                .source = "private import(cpp) fn carven() -> i32;",
                .code = DiagnosticCode::CppIdentifier,
                .primary_text = "carven",
            },
            {
                .name = "unrepresentable main provider name",
                .source = "private import(cpp) fn main() -> i32;",
                .code = DiagnosticCode::CppIdentifier,
                .primary_text = "main",
            },
        });
        check_compiler_errors(cases);
    };

    "Compiler diagnostics: C++ API namespace collisions are Carven-owned"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto parent_source = *sources.append_virtual(
                "surface.cv",
                "export(cpp) fn nested() -> i32 { return 1; }"
            );
            const auto child_source =
                *sources.append_virtual("nested.cv", "export(cpp) fn value() -> i32 { return 2; }");
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = parent_source,
                    .module_path = *CanonicalModulePath::from_value("surface"),
                },
                SourceModuleInput {
                    .source_id = child_source,
                    .module_path = *CanonicalModulePath::from_value("surface.nested"),
                },
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:cpp-collision").value(),
                }
            );

            if (!expect(!result.has_value())) {
                return;
            }
            const auto* collision =
                find_diagnostic(result.error(), DiagnosticCode::CppAPIPathCollision);
            if (!expect(collision != nullptr)) {
                return;
            }
            if (!expect(collision->attachment.primary.has_value())) {
                return;
            }
            expect_equal(
                sources.slice(collision->attachment.primary->span),
                std::string_view("export(cpp)")
            );
        };

    "Compiler diagnostics: external delegation retains Carven access rules"_test =
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "global keyword expression",
                 .source = "fn f() { ::native::union(); }",
                 .code = DiagnosticCode::CppIdentifier,
                 .primary_text = "union"},
                {.name = "global keyword type",
                 .source = "fn f(value: ::native::union) {}",
                 .code = DiagnosticCode::CppIdentifier,
                 .primary_text = "union"},
                {.name = "external result is not a Carven constant",
                 .source = "import <native> using value; fn f() { const x = value(); }",
                 .code = DiagnosticCode::ConstInitializer,
                 .primary_text = "const x = value()"},
                {.name = "external value cannot create a callable borrow",
                 .source =
                     "import <native> using value; fn f() { let callback: fn() -> i32 = value(); }",
                 .code = DiagnosticCode::TypeCallableViewEscape,
                 .primary_text = "value()"},
                {.name = "external spelling must be representable",
                 .source = "import <native> using native::union;",
                 .code = DiagnosticCode::CppIdentifier,
                 .primary_text = "union"},
                {.name = "external Write cannot mutate let",
                 .source = "import <native> using change; fn f() { let x = 1; change(&x); }",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = "x"},
                {.name = "external call cannot read taken owner",
                 .source =
                     "import <native> using consume; fn f() { let x = 1; consume(&&x); consume(x); }",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "x"},
                {.name = "external call retains unfinished argument access",
                 .source = "import <native> using consume; fn f() { let x = 1; consume(x, &&x); }",
                 .code = DiagnosticCode::AccessOperationConflict,
                 .primary_text = "x"},
                {.name = "external import conflicts with declaration",
                 .source = "import <native> using vendor::f; fn f() {}",
                 .code = DiagnosticCode::Catalog,
                 .primary_text = "f"},
                {.name = "known Carven names do not fall back to C++",
                 .source = "import <native> using vendor::*; fn f() { let x = 1; x(); }",
                 .code = DiagnosticCode::TypeNotCallable,
                 .primary_text = "x"},
            });
            check_compiler_errors(cases);
        };

    "Compiler diagnostics: C++ imports are confined to their owning module"_test = [] static noexcept {
        struct Case final {
            std::string_view consumer;
            DiagnosticCode code;
        };

        constexpr auto cases = std::to_array<Case>({
            {"fn f() { unknown(); }", DiagnosticCode::NameUnresolved},
            {"import .provider using external; fn f() {}", DiagnosticCode::ImportResolution},
            {"import .provider using known; import <native> using known; fn f() {}",
             DiagnosticCode::Catalog},
        });
        each(cases, &Case::consumer, [&](const auto& test) noexcept {
            auto sources = SourceManager();
            const auto provider = *sources.append_virtual(
                "provider.cv",
                "import <native> using native::{ external }; import <native> using native::*; fn known() {}"
            );
            const auto consumer =
                *sources.append_virtual("consumer.cv", std::string(test.consumer));
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = provider,
                    .module_path = *CanonicalModulePath::from_value("provider")
                },
                SourceModuleInput {
                    .source_id = consumer,
                    .module_path = *CanonicalModulePath::from_value("consumer")
                },
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:cpp-isolation").value()
                }
            );
            if (!(expect(!result.has_value()))) {
                return;
            }
            expect_diagnostic(result.error(), test.code);
        });
    };

    "Compiler: external validity is delegated to C++"_test = [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
        };

        constexpr auto cases = std::to_array<Case>({
            {.name = "bad_argument",
             .source =
                 "import <vector> using std::vector; fn f() { var v = vector<i32> {}; v.push_back(v); }"},
            {.name = "bad_template",
             .source =
                 "import <vector> using std::vector; fn f() { let v = vector<i32, i32> {}; v.size(); }"},
            {.name = "immutable_receiver",
             .source =
                 "import <vector> using std::vector; fn f() { let v = vector<i32> {}; v.push_back(1); }"},
            {.name = "missing_header", .source = "import <carven_nonexistent_header>; fn f() {}"},
            {.name = "missing_member",
             .source =
                 "import <vector> using std::vector; fn f() { let v = vector<i32> {}; v.carven_nonexistent(); }"},
            {.name = "missing_name",
             .source =
                 "import <vector> using std::carven_nonexistent; fn f() { carven_nonexistent(); }"},
            {.name = "global_without_header", .source = "fn f() { ::missing::function(); }"},
            {.name = "global_type_without_header",
             .source = "fn f(value: ::Missing) -> ::Missing { return value; }"},
            {.name = "global_ignores_builtin", .source = "fn f(value: ::i32) {}"},
            {.name = "global_ignores_local", .source = "fn f() { let target = 1; ::target(); }"},
            {.name = "global_ignores_module", .source = "fn target() { ::target(1); }"},
            {.name = "read_argument",
             .source =
                 "import <native> using native::increment; fn f() { var x = 1; increment(x); }"},
        });
        each(cases, &Case::name, [&](const auto& test) noexcept {
            auto sources = SourceManager();
            const auto source_id =
                *sources.append_virtual("delegation.cv", std::string(test.source));
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("delegation"),
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:cpp-delegation").value(),
                }
            );
            expect(result.has_value());
        });
    };

    "Compiler diagnostics: explicit selections and C strings enforce their contracts"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {"different paths",
                 "import <a> using first::f; import <b> using second::f; fn main() {}",
                 DiagnosticCode::Catalog,
                 "f"},
                {"C string NUL",
                 R"(fn main() { let p = c"a\0b"; })",
                 DiagnosticCode::Lexical,
                 R"(\0)"},
                {"C string Unicode NUL",
                 R"(fn main() { let p = c"a\u{0}b"; })",
                 DiagnosticCode::Lexical,
                 R"(\u{0})"},
                {"C string pattern",
                 R"(fn f() { match 1 { c"abc" => {}, _ => {} } })",
                 DiagnosticCode::TypeMatchPattern,
                 R"(c"abc")"},
            });
            check_compiler_errors(cases);
        };
});

} // namespace
