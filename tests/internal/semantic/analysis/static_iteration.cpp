module carven:test.internal.semantic.analysis.static_iteration;

import :compiler.analysis;
import :semantic.evaluation.execution;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

template<typename Check>
auto with_iteration(std::string_view text, Check check) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("iteration.cv", std::string(text));
    require(source.has_value());
    const auto input = SourceModuleInput {
        .source_id = *source,
        .module_path = *CanonicalModulePath::from_value("iteration"),
    };
    auto output = std::string();
    const auto write_output = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
        require(stream == ExecutionOutputStream::Standard);
        output.append(bytes);
    };
    const auto result = analyze_compilation(
        std::move(sources),
        SourceBatch {.modules = std::span(&input, 1uz)},
        {.output = write_output,
         .timings = {},
         .collect_source_observations = false,
         .capture_output = false,
         .host_effects = HostEffects::Allow}
    );
    require(result.program() != nullptr).note("source = ", text);
    check(output);
}

const TestSuite suite([] static noexcept {
    "Static iteration: preparing a residual loop executes its source once"_test =
        [] static noexcept {
            with_iteration(
                R"(
                    const fn source() -> [i32; 3] {
                        println("source");
                        return [2, 4, 8];
                    }
                    const for value in source() {
                        let local = value;
                        println(local);
                    }
                )",
                [](const auto& output) static noexcept { expect_equal(output, "source\n"); }
            );
        };

    "Static iteration: module blocks execute while entry bodies wait for runtime"_test =
        [] static noexcept {
            with_iteration(
                R"(const { for value in [1, 2] { println(value); } })",
                [](const auto& output) static noexcept { expect_equal(output, "1\n2\n"); }
            );
            with_iteration(
                R"(const if true { println("runtime"); })",
                [](const auto& output) static noexcept { expect(output.empty()); }
            );
        };
});

} // namespace
