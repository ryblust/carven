module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.fixture;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import std;

auto semantic_test_module_path() noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value("analysis");
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto analyze_test_errors(std::string source_text) noexcept -> Diagnostics {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("analysis.cv", std::move(source_text));
    REQUIRE(source_id.has_value());
    const auto inputs = std::array {CompilationModuleInput {
        .source_id = *source_id,
        .module_path = semantic_test_module_path(),
    }};
    auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    if (analyzed.has_value()) {
        return std::move(analyzed->diagnostics);
    }
    return std::move(analyzed.error());
}

auto analyze_test_program(std::string source_text) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("analysis.cv", std::move(source_text));
    REQUIRE(source_id.has_value());
    const auto inputs = std::array {CompilationModuleInput {
        .source_id = *source_id,
        .module_path = semantic_test_module_path(),
    }};
    auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    REQUIRE(analyzed.has_value());
    return std::move(analyzed->value);
}

auto contains_diagnostic_code(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept
    -> bool {
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
        return diagnostic.finding.code == code;
    });
}

auto find_diagnostic_code(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept
    -> const Diagnostic* {
    const auto found = std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.finding.code == code;
    });
    return found == diagnostics.end() ? nullptr : &*found;
}

constexpr auto semantic_test_payload_prelude = "struct Payload { value: i32 }\n"
                                               "fn consume(&&payload: Payload) {}\n";

auto test_function_callables(const SemIRProgram& program) noexcept -> std::vector<CallableID> {
    auto result = std::vector<CallableID>();
    for (const auto [id, declaration] : program.declarations().functions()) {
        static_cast<void>(id);
        result.push_back(declaration.callable);
    }
    return result;
}

auto test_callable_signature(const SemIRProgram& program, CallableID callable) noexcept
    -> const CallableSignature& {
    return program.callable_signatures().signature(
        program.declarations().callable(callable).signature
    );
}

auto test_callable_failures(const SemIRProgram& program, CallableID callable) noexcept
    -> const FailureSet& {
    return program.failure_sets().failure_set(test_callable_signature(program, callable).failures);
}
