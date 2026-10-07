module carven:test.internal.semantic.analysis.fixture;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

auto semantic_test_module_path() noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value("analysis");
    require(result.has_value());
    return std::move(*result);
}

auto analyze_test_errors(std::string source_text) noexcept -> Diagnostics {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("analysis.cv", std::move(source_text));
    require(source_id.has_value());
    const auto source_view = sources.view(*source_id);
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source_id,
        .module_path = semantic_test_module_path(),
    }};
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    require(parsed.has_value()).note("source = ", source_view.text);
    auto analyzed = analyze(std::move(*parsed));
    if (analyzed.has_value()) {
        return std::move(analyzed->diagnostics);
    }
    return std::move(analyzed.error());
}

auto analyze_test_program(
    std::string source_text,
    std::source_location location = std::source_location::current()
) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("analysis.cv", std::move(source_text));
    require(source_id.has_value());
    const auto source_view = sources.view(*source_id);
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source_id,
        .module_path = semantic_test_module_path(),
    }};
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    require(parsed.has_value(), location).note("source = ", source_view.text);
    auto analyzed = analyze(std::move(*parsed));
    require(analyzed.has_value(), location)
        .note("source = ", source_view.text)
        .note([&]() noexcept {
            auto errors = std::string();
            if (!analyzed.has_value()) {
                for (const auto& diagnostic : analyzed.error()) {
                    if (diagnostic.finding.severity == DiagnosticSeverity::Error) {
                        errors += std::format(
                            "\n  {}: {}",
                            diagnostic_code_info(diagnostic.finding.code).name,
                            diagnostic.finding.message
                        );
                    }
                }
            }
            return errors;
        });
    return std::move(analyzed->value);
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
