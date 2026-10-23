module;

#include <carven/std/providers.hpp>

module carven:test.internal.semantic.analysis.fixture.impl;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.analyze;
import :semantic.evaluation.execution;
import :semantic.evaluation.native;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

auto standard_test_bindings() noexcept -> std::span<const ExecutionNativeBinding> {
    static const auto entries = []() noexcept {
        auto bindings = std::vector<ExecutionNativeBinding>();
        carven::standard::visit_native_bindings(
            [&]<auto Function>(std::string_view name, bool environment_effects) noexcept {
                bindings.push_back(
                    execution_native_function<Function>(
                        std::string(name),
                        {.static_execution = true, .environment_effects = environment_effects}
                    )
                );
            },
            [&]<typename From, typename To>(std::string_view name) noexcept {
                bindings.push_back(
                    execution_native_conversion<From, To>(
                        std::string(name),
                        {.static_execution = true, .environment_effects = false}
                    )
                );
            }
        );
        return bindings;
    }();
    return entries;
}

auto analyze_test_syntax(
    SyntaxProgram syntax,
    HostEffects host_effects,
    ExecutionOutput output
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto host = ExecutionNativeHost(standard_test_bindings(), static_execution_limits());
    return analyze(std::move(syntax), host_effects, output, {}, {}, host);
}
