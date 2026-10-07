module carven:test.internal.semantic.analysis.source_observation;

import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :compiler.analysis;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto analyze_observed(
    std::string text,
    SourceAnalysisOutput observation,
    ExecutionOutput output = {}
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("observed.cv", std::move(text));
    require(source.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = semantic_test_module_path(),
    }};
    return analyze_compilation(sources, SourceBatch {.modules = inputs}, output, {}, observation);
}

auto occurrence_at(std::span<const SourceOccurrence> occurrences, std::size_t position) noexcept
    -> const SourceOccurrence* {
    const auto found = std::ranges::find_if(occurrences, [&](const auto& occurrence) noexcept {
        return occurrence.location.span.start() == position;
    });
    return found == occurrences.end() ? nullptr : std::addressof(*found);
}

constexpr auto observed_source = std::string_view(R"cv(
struct Model { field: i32 }
const base: i32 = 3;
const next: i32 = base + 1;
const fn seed() -> i32 => next;
const answer: i32 = seed();
fn probe(input: Model) -> i32 {
    let callback = [input]() { let inside = input.field; return inside; };
    let constructed = Model { field: answer };
    return callback() + constructed.field;
}
const { let block_value = answer; println(block_value); }
test "runtime source" { let test_value = answer; check(test_value == 4); }
const test "static source" { let static_value = answer; check(static_value == 4); }
)cv");

const TestSuite tests([] static noexcept {
    "Source observation: published types belong to the delivered program"_test =
        [] static noexcept {
            auto calls = 0uz;
            auto retained = std::vector<SourceOccurrence>();
            const auto moved = [&]() noexcept {
                const auto collect = [&](std::span<const SourceOccurrence> occurrences) noexcept {
                    ++calls;
                    retained.assign(occurrences.begin(), occurrences.end());
                };
                auto result = analyze_observed(
                    "fn f(value: i32) -> i32 { let local = value; return local; }",
                    collect
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
                .name = "declaration admission after demanded body construction",
                .source =
                    "const fn seed() => 1; const answer: i32 = seed(); struct Broken { value: Missing }",
                .diagnostic = DiagnosticCode::TypeUnresolved,
                .calls = 1uz,
                .has_occurrences = false,
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
            const auto collect = [&](std::span<const SourceOccurrence> occurrences) noexcept {
                ++calls;
                retained.assign(occurrences.begin(), occurrences.end());
            };
            const auto result = analyze_observed(std::string(scenario.source), collect);
            require(!result.has_value());
            expect_diagnostic(result.error(), scenario.diagnostic);
            expect_equal(calls, scenario.calls);
            expect_equal(!retained.empty(), scenario.has_occurrences);
            for (const auto& occurrence : retained) {
                expect(!occurrence.type || !std::holds_alternative<TypeID>(*occurrence.type));
            }
        });
    };

    "Source observation: semantic selections cover declaration and source body scopes"_test =
        [] static noexcept {
            auto occurrences = std::vector<SourceOccurrence>();
            const auto collect = [&](std::span<const SourceOccurrence> values) noexcept {
                occurrences.assign(values.begin(), values.end());
            };
            const auto result = analyze_observed(std::string(observed_source), collect);
            if (!expect(result.has_value())) {
                return;
            }
            struct Selection final {
                std::string_view token;
                std::string_view declaration;
                std::optional<BuiltinType> builtin;
            };
            const auto selections = std::array {
                Selection {.token = "base +", .declaration = "base:", .builtin = BuiltinType::I32},
                Selection {.token = "seed();", .declaration = "seed()", .builtin = std::nullopt},
                Selection {.token = "Model) ->", .declaration = "Model {", .builtin = std::nullopt},
                Selection {
                    .token = "Model { field: answer",
                    .declaration = "Model {",
                    .builtin = std::nullopt
                },
                Selection {
                    .token = "field: answer",
                    .declaration = "field: i32",
                    .builtin = BuiltinType::I32
                },
                Selection {.token = "input]()", .declaration = "input:", .builtin = std::nullopt},
                Selection {
                    .token = "input.field",
                    .declaration = "input:",
                    .builtin = std::nullopt
                },
                Selection {
                    .token = "inside;",
                    .declaration = "inside =",
                    .builtin = BuiltinType::I32
                },
                Selection {
                    .token = "block_value);",
                    .declaration = "block_value =",
                    .builtin = BuiltinType::I32
                },
                Selection {
                    .token = "test_value ==",
                    .declaration = "test_value =",
                    .builtin = BuiltinType::I32
                },
                Selection {
                    .token = "static_value ==",
                    .declaration = "static_value =",
                    .builtin = BuiltinType::I32
                },
            };
            each(selections, &Selection::token, [&](const Selection& selection) noexcept {
                const auto position = observed_source.find(selection.token);
                const auto definition = observed_source.find(selection.declaration);
                if (!expect(position != std::string_view::npos)
                    || !expect(definition != std::string_view::npos)) {
                    return;
                }
                const auto* occurrence = occurrence_at(occurrences, position);
                if (!expect(occurrence != nullptr) || !expect(occurrence->definition.has_value())) {
                    return;
                }
                expect_equal(occurrence->definition->span.start(), definition);
                expect(occurrence->type.has_value());
                if (selection.builtin && occurrence->type) {
                    const auto* type = std::get_if<TypeID>(&*occurrence->type);
                    if (expect(type != nullptr)) {
                        expect(*type == result->value.types().builtin_type(*selection.builtin));
                    }
                }
            });
        };

    "Source observation: failed parent construction discards nested body transactions"_test =
        [] static noexcept {
            constexpr auto source = std::string_view(
                "fn good() -> i32 { return 7; } "
                "fn broken() -> i32 { let callback = []() { let nested = 1; return nested; }; "
                "return missing; }"
            );
            auto occurrences = std::vector<SourceOccurrence>();
            const auto collect = [&](std::span<const SourceOccurrence> values) noexcept {
                occurrences.assign(values.begin(), values.end());
            };
            const auto result = analyze_observed(std::string(source), collect);
            if (!expect(!result.has_value())) {
                return;
            }
            expect_diagnostic(result.error(), DiagnosticCode::NameUnresolved);
            expect(occurrence_at(occurrences, source.find("7;")) != nullptr);
            expect(occurrence_at(occurrences, source.find("nested =")) == nullptr);
            expect(occurrence_at(occurrences, source.find("nested;")) == nullptr);
            expect(occurrence_at(occurrences, source.find("callback =")) == nullptr);
        };

    "Source observation: static instances do not publish a chosen source template type"_test =
        [] static noexcept {
            constexpr auto source = std::string_view(
                "fn lane(const index: i32) -> i32 => index; "
                "fn probe() -> i32 { let first = lane(1); return first + lane(2); }"
            );
            auto occurrences = std::vector<SourceOccurrence>();
            const auto collect = [&](std::span<const SourceOccurrence> values) noexcept {
                occurrences.assign(values.begin(), values.end());
            };
            const auto result = analyze_observed(std::string(source), collect);
            if (!expect(result.has_value())) {
                return;
            }
            expect(occurrence_at(occurrences, source.find("index;")) == nullptr);
            const auto* use = occurrence_at(occurrences, source.find("lane(1)"));
            if (expect(use != nullptr) && expect(use->definition.has_value())) {
                expect_equal(use->definition->span.start(), source.find("lane("));
            }
        };

    "Source observation: optional recording preserves semantics output and generated artifacts"_test =
        [] static noexcept {
            auto occurrences = std::vector<SourceOccurrence>();
            auto observed_output = std::string();
            auto ordinary_output = std::string();
            const auto collect = [&](std::span<const SourceOccurrence> values) noexcept {
                occurrences.assign(values.begin(), values.end());
            };
            const auto write_observed = [&](ExecutionOutputStream,
                                            std::string_view bytes) noexcept {
                observed_output += bytes;
            };
            const auto write_ordinary = [&](ExecutionOutputStream,
                                            std::string_view bytes) noexcept {
                ordinary_output += bytes;
            };
            auto observed = analyze_observed(std::string(observed_source), collect, write_observed);
            auto ordinary = analyze_observed(std::string(observed_source), {}, write_ordinary);
            if (!expect(observed.has_value()) || !expect(ordinary.has_value())) {
                return;
            }
            expect_equal(observed_output, std::string_view("4\n"));
            expect_equal(ordinary_output, observed_output);
            expect(!occurrences.empty());
            expect_equal(observed->diagnostics.size(), ordinary->diagnostics.size());
            expect_equal(observed->value.types().size(), ordinary->value.types().size());
            const auto request = TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:source-observation").value(),
            };
            const auto observed_artifacts = generate_artifacts(std::move(observed->value), request);
            const auto ordinary_artifacts = generate_artifacts(std::move(ordinary->value), request);
            if (!expect_equal(
                    observed_artifacts.entries().size(),
                    ordinary_artifacts.entries().size()
                )) {
                return;
            }
            for (const auto& [left, right] :
                 std::views::zip(observed_artifacts.entries(), ordinary_artifacts.entries())) {
                expect_equal(left.logical_path, right.logical_path);
                expect_equal(left.role, right.role);
                expect(left.source_mapping == right.source_mapping);
                expect_equal(left.content, right.content);
            }
        };
});

} // namespace
