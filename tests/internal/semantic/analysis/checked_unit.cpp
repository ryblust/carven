module carven:test.internal.semantic.analysis.checked_unit;

import :compiler.analysis;
import :compiler.artifact;
import :diagnostics.code;
import :diagnostics.report;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.artifact;
import :semantic.evaluation.execution;
import :semantic.semir.constant;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import :test.internal.source.fixture;
import std;

namespace {

auto checked_analysis(
    std::string source,
    std::string_view module,
    std::optional<CheckedDefinitions> seed = std::nullopt,
    bool retain_definitions = true
) noexcept -> CompilationAnalysis {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual(std::string(module) + ".cv", std::move(source));
    require(source_id.has_value());
    auto module_path = CanonicalModulePath::from_value(module);
    require(module_path.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source_id,
        .module_path = std::move(*module_path),
    }};
    return analyze_checked_compilation(
        std::move(sources),
        SourceBatch {.modules = inputs},
        std::move(seed),
        retain_definitions,
        {.output = {},
         .timings = {},
         .collect_source_observations = false,
         .capture_output = true,
         .host_effects = HostEffects::Allow}
    );
}

auto captured_output(const CompilationAnalysis& analysis) noexcept -> std::string {
    auto result = std::string();
    for (const auto& chunk : analysis.output()) {
        result += chunk.bytes;
    }
    return result;
}

auto take_unit(CompilationAnalysis analysis) noexcept -> CheckedUnit {
    require(analysis.program() != nullptr).note([&]() noexcept {
        return render_diagnostics(analysis.diagnostics(), analysis.sources());
    });
    auto unit = std::move(analysis).take_checked_unit();
    require(unit.has_value());
    return std::move(*unit);
}

auto take_definitions(const CheckedUnit& unit) noexcept -> CheckedDefinitions {
    auto definitions = decode_checked_definitions(unit.definitions);
    require(definitions.has_value()).note([&]() noexcept { return definitions.error().message; });
    return std::move(*definitions);
}

auto integer_constant(const SemIRProgram& program, std::string_view name) noexcept -> std::int64_t {
    for (const auto declaration : program.declarations().module_constants()) {
        if (program.provenance().spelling(declaration.value.name) == name) {
            const auto* integer = std::get_if<IntegerConstant>(
                &program.constants().constant(declaration.value.value).value
            );
            require(integer != nullptr);
            const auto value = integer->as_signed();
            require(value.has_value());
            return *value;
        }
    }
    require(false).note("missing constant ", name);
    return 0;
}

constexpr auto library_source = std::string_view(R"cv(
export struct Box<T> { value: T }
export const fn identity<T>(&&value: T) -> T => &&value;
export const fn add(const increment: i32, value: i32) -> i32 {
    var result = value;
    const for index in 0..increment { result += index - index + 1; }
    return result;
}
export const fn seed() -> i32 {
    const { println("function root"); }
    return 40;
}
fn warm() {
    let _ = identity<i16>(&&1i16);
    let _ = Box<i16> { value: 1i16 };
    let _ = add(1, 1);
}
const { println("library root"); }
const test "checked static root" { check(seed() == 40); }
test "library runtime test" { check(seed() == 40); }
)cv");

const TestSuite suite([] static noexcept {
    "Checked unit: consumers instantiate retained generic and staged definitions"_test =
        [] static noexcept {
            const auto library =
                take_unit(checked_analysis(std::string(library_source), "library"));
            constexpr auto ownership = std::array {
                CompilationModuleOwnership {.owned = true, .standard = false},
            };
            const auto encoded = encode_compilation_artifact(library, ownership);
            require(encoded.has_value());
            const auto artifact = read_compilation_artifact(*encoded);
            require(artifact.has_value());
            auto definitions = decode_checked_definitions(artifact->definitions);
            require(definitions.has_value());
            auto consumer = checked_analysis(
                R"cv(
import library using { Box, identity, add, seed };
struct Own { value: i32 }
const answer = add(2, identity<i32>(&&seed()));
fn use() -> i64 {
    let source = Box<i64> { value: 7i64 };
    let local = Own { value: answer };
    let value = source.value;
    return identity<i64>(&&value) + local.value as i64;
}
const { println(answer); }
)cv",
                "consumer",
                std::move(*definitions),
                false
            );
            require(consumer.program() != nullptr).note([&]() noexcept {
                return render_diagnostics(consumer.diagnostics(), consumer.sources());
            });
            expect_equal(integer_constant(*consumer.program(), "answer"), 42ll);
            expect_equal(captured_output(consumer), std::string("42\n"));
            auto unit = take_unit(std::move(consumer));
            expect(unit.cacheable);
            expect(unit.definitions.empty());
        };

    "Checked unit: standard source closures supply default types and async definitions"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto inputs = std::vector<SourceModuleInput>();
            constexpr auto modules = std::to_array<std::string_view>({"time"});
            append_standard_sources(sources, inputs, modules);
            auto standard = take_unit(
                analyze_checked_compilation(std::move(sources), SourceBatch {.modules = inputs})
            );
            const auto consumer = checked_analysis(
                R"cv(
import std::time using { Duration, RangeError, sleep };
const amount: Duration = 2ms;
const test "checked duration" { check(amount.nanoseconds == 2000000); }
async fn wait() throw RangeError { await sleep(0ns)?; }
)cv",
                "consumer",
                take_definitions(standard)
            );
            require(consumer.program() != nullptr).note([&]() noexcept {
                return render_diagnostics(consumer.diagnostics(), consumer.sources());
            });
            expect(captured_output(consumer).empty());
        };

    "Checked unit: imported declarations retain diagnostic source identities"_test =
        [] static noexcept {
            auto library =
                take_unit(checked_analysis("export fn argument(value: i32) {}", "library"));
            const auto consumer = checked_analysis(
                "import library using argument; fn invalid() { argument(); }",
                "consumer",
                take_definitions(library)
            );
            expect(consumer.program() == nullptr);
            const auto rendered = render_diagnostics(consumer.diagnostics(), consumer.sources());
            expect(rendered.contains("consumer.cv"));
            expect(rendered.contains("library.cv"));
        };

    "Checked unit: cacheability follows observed host operations"_test = [] static noexcept {
        const auto pure = take_unit(checked_analysis(
            "export const fn sample() -> i64 => monotonic_now();",
            "crafts.carven.std.time"
        ));
        expect(pure.cacheable);
        auto observed = take_unit(checked_analysis(
            "export const fn sample() -> i64 => monotonic_now(); "
            "const captured = sample(); const { println(\"producer host\"); }",
            "crafts.carven.std.time"
        ));
        expect(!observed.cacheable);
        auto imported =
            checked_analysis("const value = 42;", "consumer", take_definitions(observed));
        expect(captured_output(imported).empty());
        const auto imported_unit = take_unit(std::move(imported));
        expect(imported_unit.cacheable);
        const auto executed = take_unit(checked_analysis(
            "import std::time using sample; const value = sample();",
            "consumer",
            take_definitions(observed)
        ));
        expect(!executed.cacheable);
    };
});

} // namespace
