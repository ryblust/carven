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

auto analyze_observed(std::string text, bool observe, ExecutionOutput output = {}) noexcept
    -> CompilationAnalysis {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("observed.cv", std::move(text));
    require(source.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = semantic_test_module_path(),
    }};
    return analyze_compilation(
        std::move(sources),
        SourceBatch {.modules = inputs},
        {.output = output,
         .timings = {},
         .collect_source_observations = observe,
         .capture_output = false}
    );
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
struct Box<T> { value: T, items: Vector<T>, pair: (T, void) }
fn inspect<T>(generic_value: T) -> i32 => 1;
fn read_box(box: Box<i32>) -> i32 => inspect(box.value);
async fn read_async(parameter: i32) -> i32 {
    let callback = [parameter]() -> i32 => parameter;
    return callback();
}
fn forward<T>(&&value: T) -> T => &&value;
async fn inferred() -> i32 {
    let pending = forward(&&read_async(2));
    return await pending;
}
fn lane(const index: i32) -> i32 => index;
fn lanes() -> i32 => lane(1) + lane(2);
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
    "Source observation: recording preserves compilation and published ownership"_test =
        [] static noexcept {
            auto observed_output = std::string();
            auto ordinary_output = std::string();
            const auto write_observed = [&](ExecutionOutputStream,
                                            std::string_view bytes) noexcept {
                observed_output += bytes;
            };
            const auto write_ordinary = [&](ExecutionOutputStream,
                                            std::string_view bytes) noexcept {
                ordinary_output += bytes;
            };
            auto observed = analyze_observed(std::string(observed_source), true, write_observed);
            auto ordinary = analyze_observed(std::string(observed_source), false, write_ordinary);
            require(observed.program() != nullptr);
            require(ordinary.program() != nullptr);
            const auto occurrences = observed.source_occurrences();
            expect_equal(observed_output, std::string_view("4\n"));
            expect_equal(ordinary_output, observed_output);
            expect_equal(observed.diagnostics().size(), ordinary.diagnostics().size());
            for (const auto& occurrence : occurrences) {
                if (occurrence.type) {
                    const auto* type = std::get_if<TypeID>(&*occurrence.type);
                    if (expect(type != nullptr)) {
                        expect(observed.program()->types().contains(*type));
                    }
                }
            }
            const auto* field = occurrence_at(occurrences, observed_source.find("box.value") + 4uz);
            require(field != nullptr);
            require(field->definition.has_value());
            expect_equal(field->definition->span.start(), observed_source.find("value: T"));
            expect_equal(slice(observed_source, field->definition->span), "value");
            require(field->type.has_value());
            expect(
                std::get<TypeID>(*field->type)
                == observed.program()->types().builtin_type(BuiltinType::I32)
            );
            const auto* rigid =
                occurrence_at(occurrences, observed_source.find("generic_value: T"));
            require(rigid != nullptr);
            expect(rigid->definition.has_value());
            expect(!rigid->type.has_value());
            const auto* captured =
                occurrence_at(occurrences, observed_source.find("=> parameter") + 3uz);
            require(captured != nullptr);
            require(captured->definition.has_value());
            expect_equal(
                captured->definition->span.start(),
                observed_source.find("parameter: i32")
            );
            expect(occurrence_at(occurrences, observed_source.find("=> index") + 3uz) == nullptr);
            const auto* pending = occurrence_at(occurrences, observed_source.find("pending ="));
            if (!expect(pending != nullptr) || !expect(pending->type.has_value())) {
                return;
            }
            const auto* pending_type = std::get_if<TypeID>(&*pending->type);
            if (!expect(pending_type != nullptr)) {
                return;
            }
            const auto* operation = std::get_if<OperationTypeValue>(
                &observed.program()->types().type(*pending_type).value
            );
            if (!expect(operation != nullptr)) {
                return;
            }
            expect(
                operation->success == observed.program()->types().builtin_type(BuiltinType::I32)
            );

            const auto request = TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:source-observation").value(),
                .library = std::nullopt,
            };
            const auto observed_artifacts = generate_artifacts(*observed.program(), request);
            const auto ordinary_artifacts = generate_artifacts(*ordinary.program(), request);
            require(observed_artifacts.has_value());
            require(ordinary_artifacts.has_value());
            require(observed_artifacts->entries().size() == ordinary_artifacts->entries().size());
            for (const auto& [left, right] :
                 std::views::zip(observed_artifacts->entries(), ordinary_artifacts->entries())) {
                expect_equal(left.logical_path, right.logical_path);
                expect_equal(left.role, right.role);
                expect(left.source_mapping == right.source_mapping);
                expect_equal(left.content, right.content);
            }
        };

    "Source observation: failed parent bodies discard nested transactions"_test =
        [] static noexcept {
            constexpr auto source = std::string_view(
                "fn good() -> i32 { return 7; } "
                "fn broken() -> i32 { let callback = []() { let nested = 1; return nested; }; "
                "return missing; }"
            );
            const auto result = analyze_observed(std::string(source), true);
            const auto occurrences = result.source_occurrences();
            require(result.program() == nullptr);
            expect_diagnostic(result.diagnostics(), DiagnosticCode::NameUnresolved);
            expect(occurrence_at(occurrences, source.find("7;")) != nullptr);
            expect(occurrence_at(occurrences, source.find("nested =")) == nullptr);
            expect(occurrence_at(occurrences, source.find("callback =")) == nullptr);
            for (const auto& occurrence : occurrences) {
                expect(!occurrence.type || !std::holds_alternative<TypeID>(*occurrence.type));
            }
        };

    "Source observation: declaration and publication gates bound surviving evidence"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view source;
                DiagnosticCode code;
                bool has_occurrences;
            };
            const auto cases = std::array {
                Case {
                    "declaration failure after a demanded body",
                    "const fn seed() => 1; const answer: i32 = seed(); struct Broken { value: Missing }",
                    DiagnosticCode::TypeUnresolved,
                    false,
                },
                Case {
                    "nominal storage failure",
                    "struct Box<T> { value: Box<T> } fn good() -> i32 => 1;",
                    DiagnosticCode::TypeRecursiveStorage,
                    false,
                },
                Case {
                    "ownership failure during publication",
                    "struct Entry { value: i32 } fn bad() -> i32 { var entry = Entry { 1 }; "
                    "let taken = &&entry; return entry.value; }",
                    DiagnosticCode::AccessUnavailable,
                    true,
                },
            };
            each(cases, &Case::name, [](const Case& input) static noexcept {
                const auto result = analyze_observed(std::string(input.source), true);
                const auto occurrences = result.source_occurrences();
                require(result.program() == nullptr);
                expect_diagnostic(result.diagnostics(), input.code);
                expect_equal(!occurrences.empty(), input.has_occurrences);
                for (const auto& occurrence : occurrences) {
                    expect(!occurrence.type || !std::holds_alternative<TypeID>(*occurrence.type));
                }
            });
        };
});

} // namespace
