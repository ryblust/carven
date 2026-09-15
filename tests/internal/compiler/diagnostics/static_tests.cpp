module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.static_tests;

import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.code;
import :semantic.evaluation.output;
import :source.manager;
import :source.module_path;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

auto compile_static(std::string source, std::string& output, std::string& errors) noexcept {
    auto sources = SourceManager();
    const auto id = *sources.append_virtual("static.cv", std::move(source));
    const auto input = CompilationModuleInput {
        .source_id = id,
        .module_path = *CanonicalModulePath::from_value("static")
    };
    return compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("static-tests")
        },
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            (stream == ExecutionOutputStream::Standard ? output : errors) += bytes;
        }
    );
}

} // namespace

TEST_CASE("Compiler: print executes only in required constant evaluations and const tests") {
    auto output = std::string();
    auto errors = std::string();
    const auto result = compile_static(
        R"(
        const fn observe(value: i32) {
            println("value", value);
            return value;
        }
        const initial = observe(2);
        fn runtime_only() { observe(9); }
        test "runtime body" { observe(8); }
        const test "static body" {
            check(initial == 2);
            for index in 0..2 { check(observe(index) == index); }
            if false { println("unexecuted"); }
            print("left"); println("right", true, '我');
            println();
            eprint("error"); eprintln(" stream");
            println(f"{12:04}");
            print("a\0b");
        }
    )",
        output,
        errors
    );
    if (!result) {
        for (const auto& diagnostic : result.error()) {
            INFO(diagnostic.finding.message);
        }
    }
    REQUIRE(result.has_value());
    CHECK(
        output
        == std::string("value 2\nvalue 0\nvalue 1\nleftright true 我\n\n0012\na")
            + std::string("\0b", 2)
    );
    CHECK(errors == "error stream\n");
}

TEST_CASE("Compiler: static checks continue and requirements stop nested calls") {
    auto output = std::string();
    auto errors = std::string();
    const auto result = compile_static(
        R"(
        const fn message() { print("message;"); return "detail"; }
        const fn stop() { require(false, "stop"); print("unreachable"); }
        const test "checks" {
            check(true, message());
            check(false, message());
            print("continued;");
            stop();
            print("unreachable");
        }
        const test "next" { print("next;"); fail("last"); }
    )",
        output,
        errors
    );
    REQUIRE(!result.has_value());
    CHECK(output == "message;message;continued;next;");
    CHECK(
        std::ranges::count_if(
            result.error(),
            [](const auto& diagnostic) static noexcept {
                return diagnostic.finding.code == DiagnosticCode::ConstTest;
            }
        )
        == 3
    );
    CHECK(errors.empty());
}

TEST_CASE("Compiler: const test rejects unsupported operations even on untaken paths") {
    const auto cases = std::array {
        CompilerErrorExpectation {
            .name = "ordinary call",
            .source = "fn ordinary() {} const test \"t\" { ordinary(); }",
            .code = "CV-CONST-ADMISSION",
            .primary_text = "ordinary()"
        },
        CompilerErrorExpectation {
            .name = "dead native call",
            .source = "import(cpp) fn native(); const test \"t\" { if false { native(); } }",
            .code = "CV-CONST-ADMISSION",
            .primary_text = "native()"
        },
    };
    check_compiler_errors(cases);
}

TEST_CASE("Compiler: test reporting needs an active static test") {
    auto output = std::string();
    auto errors = std::string();
    const auto result = compile_static(
        R"(
        const fn checked() { check(true); return 1; }
        const value = checked();
    )",
        output,
        errors
    );
    REQUIRE(!result.has_value());
    CHECK(find_compiler_diagnostic(result.error(), "CV-CONST-TEST") != nullptr);
}

TEST_CASE("Compiler: static print completes arguments before observing borrowed text") {
    auto output = std::string();
    auto errors = std::string();
    const auto result = compile_static(
        R"(
        const fn observe(value: i32) { print("argument;"); return value; }
        const test "arguments" {
            var number = 1;
            var text: String = "a";
            println(number, text, if true {
                number = 2;
                text.append("b");
                observe(number)
            } else { 0 });
        }
    )",
        output,
        errors
    );
    REQUIRE(result.has_value());
    CHECK(output == "argument;1 ab 2\n");
}

TEST_CASE("Compiler: static failure diagnostics consume the root text budget") {
    auto output = std::string();
    auto errors = std::string();
    const auto source = std::string("const test \"bounded\" { for index in 0..16 { check(false, \"")
        + std::string(1024uz * 1024uz - 128uz, 'x')
        + "\"); } } const test \"next\" { print(\"next\"); }";
    const auto result = compile_static(source, output, errors);
    REQUIRE(!result.has_value());
    CHECK(find_compiler_diagnostic(result.error(), "CV-CONST-LIMIT") != nullptr);
    CHECK(output == "next");
    CHECK(
        std::ranges::count_if(
            result.error(),
            [](const auto& diagnostic) static noexcept {
                return diagnostic.finding.code == DiagnosticCode::ConstTest;
            }
        )
        < 16
    );
}
