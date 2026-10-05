module carven:test.internal.semantic.analysis.source_observation;

import :compiler.analysis;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :semantic.analysis.source;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto analyze_observed(std::string text, const SourceAnalysisOutput& observation) noexcept
    -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("observed.cv", std::move(text));
    require(source.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = semantic_test_module_path(),
    }};
    return analyze_compilation(sources, SourceBatch {.modules = inputs}, {}, {}, observation);
}

const TestSuite tests([] static noexcept {
    "Source observation: published types belong to the delivered program"_test =
        [] static noexcept {
            auto calls = 0uz;
            auto retained = std::vector<SourceOccurrence>();
            auto moved = [&]() noexcept {
                auto result = analyze_observed(
                    "fn f(value: i32) -> i32 { let local = value; return local; }",
                    [&](std::span<const SourceOccurrence> occurrences) noexcept {
                        ++calls;
                        retained.assign(occurrences.begin(), occurrences.end());
                    }
                );
                require(result.has_value());
                expect_equal(calls, 1uz);
                require(!retained.empty());
                auto typed = 0uz;
                for (const auto& occurrence : retained) {
                    if (!occurrence.type) {
                        continue;
                    }
                    const auto* type = std::get_if<TypeID>(&*occurrence.type);
                    require(type != nullptr);
                    expect(result->value.types().contains(*type));
                    ++typed;
                }
                expect(typed != 0uz);
                return std::move(result->value);
            }();
            for (const auto& occurrence : retained) {
                if (occurrence.type) {
                    expect(moved.types().contains(std::get<TypeID>(*occurrence.type)));
                }
            }
        };

    "Source observation: failure gates control evidence delivery"_test = [] static noexcept {
        struct Scenario final {
            std::string_view name;
            std::string_view source;
            DiagnosticCode diagnostic;
            std::size_t calls;
            bool has_occurrences;
        };
        const auto scenarios = std::array {
            Scenario {
                .name = "parse admission",
                .source = "fn f(1) {}",
                .diagnostic = DiagnosticCode::Syntax,
                .calls = 0uz,
                .has_occurrences = false,
            },
            Scenario {
                .name = "declaration admission",
                .source = "fn f(value: Missing) {}",
                .diagnostic = DiagnosticCode::TypeUnresolved,
                .calls = 1uz,
                .has_occurrences = false,
            },
            Scenario {
                .name = "body construction",
                .source =
                    "fn good(value: i32) -> i32 { return value; } fn bad() -> i32 { return missing; }",
                .diagnostic = DiagnosticCode::NameUnresolved,
                .calls = 1uz,
                .has_occurrences = true,
            },
            Scenario {
                .name = "semantic publication",
                .source =
                    "struct Entry { value: i32 } fn bad() -> i32 { var entry = Entry { 1 }; let taken = &&entry; return entry.value; }",
                .diagnostic = DiagnosticCode::AccessUnavailable,
                .calls = 1uz,
                .has_occurrences = true,
            },
        };
        each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
            auto calls = 0uz;
            auto retained = std::vector<SourceOccurrence>();
            const auto result = analyze_observed(
                std::string(scenario.source),
                [&](std::span<const SourceOccurrence> occurrences) noexcept {
                    ++calls;
                    retained.assign(occurrences.begin(), occurrences.end());
                }
            );
            require(!result.has_value());
            expect_diagnostic(result.error(), scenario.diagnostic);
            expect_equal(calls, scenario.calls);
            expect_equal(!retained.empty(), scenario.has_occurrences);
            for (const auto& occurrence : retained) {
                expect(!occurrence.type || !std::holds_alternative<TypeID>(*occurrence.type));
            }
        });
    };
});

} // namespace
