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

using compiler_diagnostics_test::ErrorExpectation;
using compiler_diagnostics_test::check_errors;
using compiler_diagnostics_test::find_diagnostic;

TEST_CASE("Compiler diagnostics: interop failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<ErrorExpectation>({
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
        {
            .name = "C++ keyword API name",
            .source = "export(cpp) fn class() -> i32 { return 1; }",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "class",
        },
    });
    check_errors(cases);
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
    const auto* collision = find_diagnostic(result.error(), "CV-CPP-API-PATH-COLLISION");
    REQUIRE(collision != nullptr);
    REQUIRE(collision->attachment.primary.has_value());
    CHECK_EQ(sources.slice(collision->attachment.primary->span), "export(cpp)");
}


TEST_CASE("Compiler diagnostics: an exported module path is validated once") {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual(
        "class.cv",
        "export(cpp) fn first() -> i32 { return 1; }\n"
        "export(cpp) fn second() -> i32 { return 2; }\n"
    );
    const auto module_path = CanonicalModulePath::from_value("class");
    REQUIRE(module_path.has_value());
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *module_path,
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:cpp-module-path").value(),
        }
    );

    REQUIRE(!result.has_value());
    const auto count = std::ranges::count_if(result.error(), [](const Diagnostic& diagnostic) {
        return diagnostic.finding.code == "CV-CPP-IDENTIFIER";
    });
    CHECK_EQ(count, 1);
    const auto* diagnostic = find_diagnostic(result.error(), "CV-CPP-IDENTIFIER");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->attachment.primary.has_value());
    CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "export(cpp)");
}

TEST_CASE("Compiler diagnostics: external delegation retains Carven access rules") {
    constexpr auto cases = std::to_array<ErrorExpectation>({
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
         .primary_text = "vendor::f"},
        {.name = "known Carven names do not fall back to C++",
         .source = "import <native> using vendor::*; fn f() { let x = 1; x(); }",
         .code = "CV-TYPE-NOT-CALLABLE",
         .primary_text = "x"},
    });
    check_errors(cases);
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
            "import <native> using { native::external }; import <native> using native::*; fn known() {}"
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
        CHECK(find_diagnostic(result.error(), test.code) != nullptr);
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
