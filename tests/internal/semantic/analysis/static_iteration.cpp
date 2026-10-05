module carven:test.internal.semantic.analysis.static_iteration;

import :compiler.analysis;
import :semantic.evaluation.output;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
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
    auto result = analyze_compilation(
        sources,
        SourceBatch {.modules = std::span(&input, 1uz)},
        [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            require(stream == ExecutionOutputStream::Standard);
            output.append(bytes);
        }
    );
    require(result.has_value()).note("source = ", text);
    check(result->value, output);
}

const TestSuite suite([] static noexcept {
    "Static iteration: one source execution preserves element order and independent scopes"_test =
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
                [](const auto& program, const auto& output) static noexcept {
                    expect_equal(output, "source\n");
                    auto loops = 0uz;
                    for (const auto body : program.bodies().entries()) {
                        visit_semantic_nodes(
                            body.value.region(),
                            [&](const SemanticStatement& statement) noexcept {
                                const auto* loop = std::get_if<SemExpandedLoop>(&statement.value);
                                if (!loop) {
                                    return;
                                }
                                ++loops;
                                require(loop->iterations.size() == 3uz);
                                auto scopes = std::flat_set<LifetimeRegionID>();
                                auto bindings = std::flat_set<LocalBindingID>();
                                auto values = std::vector<std::uint64_t>();
                                for (const auto& iteration : loop->iterations) {
                                    scopes.emplace(iteration.lifetime);
                                    visit_semantic_nodes(
                                        iteration,
                                        [&](const SemanticStatement& item) noexcept {
                                            if (const auto* local =
                                                    std::get_if<SemInitialize>(&item.value)) {
                                                bindings.emplace(local->binding);
                                                require(local->initializer.constant.has_value());
                                                const auto& fact = program.constants().constant(
                                                    *local->initializer.constant
                                                );
                                                values.push_back(
                                                    std::get<IntegerConstant>(fact.value)
                                                        .magnitude()
                                                );
                                            }
                                        }
                                    );
                                }
                                expect_equal(scopes.size(), 3uz);
                                expect_equal(bindings.size(), 3uz);
                                expect(values == std::vector<std::uint64_t>({2u, 4u, 8u}));
                            }
                        );
                    }
                    expect_equal(loops, 1uz);
                }
            );
        };

    "Static iteration: module blocks execute while selected entry bodies remain executable"_test =
        [] static noexcept {
            with_iteration(
                R"(const { for value in [1, 2] { println(value); } })",
                [](const auto&, const auto& output) static noexcept {
                    expect_equal(output, "1\n2\n");
                }
            );
            with_iteration(
                R"(const if true { println("runtime"); })",
                [](const auto& program, const auto& output) static noexcept {
                    expect(output.empty());
                    auto prints = 0uz;
                    for (const auto body : program.bodies().entries()) {
                        visit_semantic_nodes(
                            body.value.region(),
                            [&](const SemanticExpression& expression) noexcept {
                                prints += std::holds_alternative<SemPrint>(expression.value);
                            }
                        );
                    }
                    expect_equal(prints, 1uz);
                }
            );
        };
});

} // namespace
