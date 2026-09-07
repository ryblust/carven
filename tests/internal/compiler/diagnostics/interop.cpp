module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.interop;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: interop failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {
            .name = "unsupported import(cpp) boundary type",
            .source = "private import(cpp) fn invalid(value: str);",
            .code = "CV-CPP-CARRIER",
            .primary_text = "value: str",
        },
        {
            .name = "unsupported export(cpp) boundary type",
            .source = "export(cpp) fn invalid(value: str) {}",
            .code = "CV-CPP-CARRIER",
            .primary_text = "value: str",
        },
        {
            .name = "C++ boundary access mode",
            .source = "private import(cpp) fn invalid(&value: i32);",
            .code = "CV-CPP-BOUNDARY",
            .primary_text = "&",
        },
        {
            .name = "C++ boundary failure contract",
            .source = "struct Failure {} private import(cpp) fn invalid() throw Failure;",
            .code = "CV-CPP-BOUNDARY",
            .primary_text = "throw Failure",
        },
        {
            .name = "unrepresentable std provider name",
            .source = "private import(cpp) fn std() -> i32;",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "std",
        },
        {
            .name = "unrepresentable carven provider name",
            .source = "private import(cpp) fn carven() -> i32;",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "carven",
        },
        {
            .name = "unrepresentable main provider name",
            .source = "private import(cpp) fn main() -> i32;",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "main",
        },
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: C++ API namespace collisions are Carven-owned") {
    auto sources = SourceManager();
    const auto parent_source =
        *sources.append_virtual("surface.cv", "export(cpp) fn nested() -> i32 { return 1; }");
    const auto child_source =
        *sources.append_virtual("nested.cv", "export(cpp) fn value() -> i32 { return 2; }");
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = parent_source,
            .module_path = *CanonicalModulePath::from_value("surface"),
        },
        CompilationModuleInput {
            .source_id = child_source,
            .module_path = *CanonicalModulePath::from_value("surface.nested"),
        },
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:cpp-collision").value(),
        }
    );

    REQUIRE(!result.has_value());
    const auto* collision = find_compiler_diagnostic(result.error(), "CV-CPP-API-PATH-COLLISION");
    REQUIRE(collision != nullptr);
    REQUIRE(collision->attachment.primary.has_value());
    CHECK_EQ(sources.slice(collision->attachment.primary->span), "export(cpp)");
}

TEST_CASE("Compiler diagnostics: external delegation retains Carven access rules") {
    constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "global keyword expression",
         .source = "fn f() { ::native::class(); }",
         .code = "CV-CPP-IDENTIFIER",
         .primary_text = "class"},
        {.name = "global keyword type",
         .source = "fn f(value: ::native::class) {}",
         .code = "CV-CPP-IDENTIFIER",
         .primary_text = "class"},
        {.name = "external result is not a Carven constant",
         .source = "import <native> using value; fn f() { const x = value(); }",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = "const x = value()"},
        {.name = "external value cannot create a callable borrow",
         .source = "import <native> using value; fn f() { let callback: fn() -> i32 = value(); }",
         .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
         .primary_text = "value()"},
        {.name = "external spelling must be representable",
         .source = "import <native> using native::class;",
         .code = "CV-CPP-IDENTIFIER",
         .primary_text = "class"},
        {.name = "external Write cannot mutate let",
         .source = "import <native> using change; fn f() { let x = 1; change(&x); }",
         .code = "CV-ACCESS-IMMUTABLE",
         .primary_text = "x"},
        {.name = "external call cannot read taken owner",
         .source = "import <native> using consume; fn f() { let x = 1; consume(&&x); consume(x); }",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "x"},
        {.name = "external call retains unfinished argument access",
         .source = "import <native> using consume; fn f() { let x = 1; consume(x, &&x); }",
         .code = "CV-ACCESS-OPERATION-CONFLICT",
         .primary_text = "x"},
        {.name = "external import conflicts with declaration",
         .source = "import <native> using vendor::f; fn f() {}",
         .code = "CV-CATALOG",
         .primary_text = "f"},
        {.name = "known Carven names do not fall back to C++",
         .source = "import <native> using vendor::*; fn f() { let x = 1; x(); }",
         .code = "CV-TYPE-NOT-CALLABLE",
         .primary_text = "x"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: C++ imports are confined to their owning module") {
    struct Case final {
        std::string_view consumer;
        std::string_view code;
    };

    constexpr auto cases = std::to_array<Case>({
        {"fn f() { unknown(); }", "CV-NAME-UNRESOLVED"},
        {"import .provider using external; fn f() {}", "CV-IMPORT-RESOLUTION"},
        {"import .provider using known; import <native> using known; fn f() {}", "CV-CATALOG"},
    });
    for (const auto& test : cases) {
        CAPTURE(test.consumer);
        auto sources = SourceManager();
        const auto provider = *sources.append_virtual(
            "provider.cv",
            "import <native> using native::{ external }; import <native> using native::*; fn known() {}"
        );
        const auto consumer = *sources.append_virtual("consumer.cv", std::string(test.consumer));
        const auto inputs = std::array {
            CompilationModuleInput {
                .source_id = provider,
                .module_path = *CanonicalModulePath::from_value("provider")
            },
            CompilationModuleInput {
                .source_id = consumer,
                .module_path = *CanonicalModulePath::from_value("consumer")
            },
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = inputs},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:cpp-isolation").value()
            }
        );
        REQUIRE(!result.has_value());
        CHECK(find_compiler_diagnostic(result.error(), test.code) != nullptr);
    }
}

TEST_CASE("Compiler: external validity is delegated to C++") {
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
         .source = "import <native> using native::increment; fn f() { var x = 1; increment(x); }"},
    });
    for (const auto& test : cases) {
        CAPTURE(test.name);
        auto sources = SourceManager();
        const auto source_id = *sources.append_virtual("delegation.cv", std::string(test.source));
        const auto input = CompilationModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("delegation"),
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:cpp-delegation").value(),
            }
        );
        CHECK(result.has_value());
    }
}

TEST_CASE("Compiler diagnostics: explicit selections and C strings enforce their contracts") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {"different paths",
         "import <a> using first::f; import <b> using second::f; fn main() {}",
         "CV-CATALOG",
         "f"},
        {"C string NUL", R"(fn main() { let p = c"a\0b"; })", "CV-LEXICAL", R"(\0)"},
        {"C string Unicode NUL", R"(fn main() { let p = c"a\u{0}b"; })", "CV-LEXICAL", R"(\u{0})"},
        {"C string pattern",
         R"(fn f() { match 1 { c"abc" => {}, _ => {} } })",
         "CV-TYPE-MATCH-PATTERN",
         R"(c"abc")"},
        {"C string constant", R"(const p = c"abc";)", "CV-CONST-INITIALIZER", R"(c"abc")"},
    });
    check_compiler_errors(cases);
}
