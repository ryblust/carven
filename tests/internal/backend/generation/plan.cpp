module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.plan;

import :artifacts;
import :backend.emission.layout;
import :backend.emission.render;
import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :compiler.request;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir;
import :source.manager;
import :source.module_path;
import :support.visit;
import :test.internal.harness.death;
import std;

namespace {

auto analyze_failure_profiles() noexcept -> SemIRProgram {
    auto sources = SourceManager();
    auto inputs = std::vector<CompilationModuleInput>();
    const auto append = [&](std::string_view path_text, std::string source_text) noexcept {
        const auto source =
            sources.append_virtual(std::format("{}.cv", path_text), std::move(source_text));
        REQUIRE(source.has_value());
        const auto path = CanonicalModulePath::from_value(path_text);
        REQUIRE(path.has_value());
        inputs.push_back({.source_id = *source, .module_path = *path});
    };
    append("zeta", "export struct AFailure {}\n");
    append("alpha", "export struct ZFailure {}\n");
    append(
        "profiles",
        "import alpha using ZFailure;\n"
        "import zeta using AFailure;\n"
        "struct YFailure {}\n"
        "struct BFailure {}\n"
        "private fn cross_module() throw AFailure + ZFailure {}\n"
        "private fn same_module() throw YFailure + BFailure {}\n"
        "private fn combined() throw AFailure + YFailure + ZFailure + BFailure {}\n"
    );
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto semantic = analyze(std::move(*syntax));
    REQUIRE(semantic.has_value());
    return std::move(semantic->value);
}

auto request(TestGenerationMode test_mode, std::string_view linkage) noexcept
    -> TargetPlanningRequest {
    return {
        .test_mode = test_mode,
        .linkage_domain = *LinkageDomain::explicit_value(std::string(linkage)),
    };
}

auto function_named(const SemIRProgram& semantic, std::string_view name) noexcept -> FunctionID {
    for (const auto function : semantic.declarations().functions()) {
        if (semantic.provenance().spelling(function.value.name) == name) {
            return function.id;
        }
    }
    FAIL_CHECK(std::format("missing function '{}'", name));
    for (const auto function : semantic.declarations().functions()) {
        return function.id;
    }
    std::unreachable();
}

auto failure_name(const SemIRProgram& semantic, TypeID type) noexcept -> std::string_view {
    return std::visit(
        Overloaded {
            [&](const StructTypeValue& value) noexcept {
                return semantic.provenance().spelling(
                    semantic.declarations().structure(value.structure).name
                );
            },
            [&](const EnumTypeValue& value) noexcept {
                return semantic.provenance().spelling(
                    semantic.declarations().enumeration(value.enumeration).name
                );
            },
            [](const auto&) noexcept -> std::string_view {
                FAIL_CHECK("failure ABI member is not nominal");
                return {};
            },
        },
        semantic.types().type(type).value
    );
}

auto public_names(std::string source_text) noexcept -> std::array<std::string, 2> {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("support.cv", std::move(source_text));
    REQUIRE(source.has_value());
    const auto path = CanonicalModulePath::from_value("support");
    REQUIRE(path.has_value());
    const auto input = CompilationModuleInput {.source_id = *source, .module_path = *path};
    auto syntax = parse_program(sources, CompilationRequest {.modules = std::span(&input, 1)});
    REQUIRE(syntax.has_value());
    auto analyzed = analyze(std::move(*syntax));
    REQUIRE(analyzed.has_value());
    const auto compilation = PlannedCompilation::build(
        std::move(analyzed->value),
        request(TestGenerationMode::None, "name-plan-test")
    );
    const auto structure = [&]() noexcept {
        for (const auto entry : compilation.semantic().declarations().structures()) {
            if (compilation.semantic().provenance().spelling(entry.value.name) == "class_cv") {
                return entry.id;
            }
        }
        std::unreachable();
    }();
    const auto function = function_named(compilation.semantic(), "identity");
    return {
        std::string(compilation.target().names().structure_identifier(structure).spelling()),
        std::string(compilation.target().names().function_identifier(function).spelling()),
    };
}

static_assert(std::move_constructible<PlannedCompilation>);
static_assert(!std::is_move_assignable_v<PlannedCompilation>);
static_assert(!std::is_move_assignable_v<TargetPlan>);

} // namespace

TEST_CASE("Target plan: failure ABI has one deterministic nominal order") {
    const auto compilation = PlannedCompilation::build(
        analyze_failure_profiles(),
        request(TestGenerationMode::None, "failure-profile-test")
    );
    const auto function = function_named(compilation.semantic(), "combined");
    const auto callable = compilation.semantic().declarations().function(function).callable;
    const auto signature = compilation.semantic().declarations().callable(callable).signature;
    const auto failures =
        compilation.semantic().callable_signatures().signature(signature).failures;
    auto names = std::vector<std::string_view>();
    for (const auto type : compilation.target().failure_abi().members(failures)) {
        names.push_back(failure_name(compilation.semantic(), type));
    }

    CHECK_EQ(names, std::vector<std::string_view> {"ZFailure", "BFailure", "YFailure", "AFailure"});
    CHECK_EQ(compilation.target().semantic_identity(), compilation.semantic().identity());
}

TEST_CASE("Target plan: artifact IDs are owner-bound and dependency-first") {
    const auto compilation = PlannedCompilation::build(
        analyze_failure_profiles(),
        request(TestGenerationMode::RunnerEntryPoint, "artifact-graph-test")
    );
    auto count = 0uz;
    auto dependency_count = 0uz;
    auto last_role = GeneratedArtifactRole::Interface;
    for (const auto entry : compilation.target().artifacts()) {
        ++count;
        CHECK_EQ(entry.id.owner(), compilation.target().identity());
        CHECK_FALSE(artifact_logical_path(entry.value).empty());
        for (const auto dependency : artifact_dependencies(entry.value)) {
            ++dependency_count;
            CHECK_EQ(dependency.owner(), compilation.target().identity());
            CHECK_LT(dependency.index(), entry.id.index());
        }
        last_role = artifact_role(entry.value);
    }
    CHECK_EQ(count, compilation.target().artifact_count());
    CHECK_GT(dependency_count, 0u);
    CHECK_EQ(last_role, GeneratedArtifactRole::TestEntry);
}

TEST_CASE("Target plan: generated test artifacts occupy the private path domain") {
    const auto compilation = PlannedCompilation::build(
        analyze_failure_profiles(),
        request(TestGenerationMode::RunnerEntryPoint, "test-artifact-paths")
    );
    auto logical_paths = std::vector<std::string_view>();
    for (const auto artifact : compilation.target().artifacts()) {
        logical_paths.push_back(artifact_logical_path(artifact.value));
    }

    CHECK(std::ranges::contains(logical_paths, "carven/generated/carven-test-runner.hpp"));
    CHECK(std::ranges::contains(logical_paths, "carven/generated/carven-test-main.cpp"));
    CHECK_FALSE(std::ranges::contains(logical_paths, "carven-test-runner.hpp"));
    CHECK_FALSE(std::ranges::contains(logical_paths, "carven-test-main.cpp"));

    const auto module_and_runner_paths = std::array<std::string, 3> {
        module_implementation_logical_path(std::array<std::string, 1> {"carven-test-main"}),
        "carven/generated/carven-test-runner.hpp",
        "carven/generated/carven-test-main.cpp",
    };
    verify_target_artifact_logical_paths(module_and_runner_paths);
}

TEST_CASE("Target plan: malformed artifact path schedules fail before seal") {
    const auto duplicate = std::array<std::string, 2> {
        "module.cpp",
        "module.cpp",
    };
    CHECK(expect_termination("target-plan-duplicate-artifact-path", [&] {
        verify_target_artifact_logical_paths(duplicate);
    }));

    const auto prefix_collision = std::array<std::string, 2> {
        "carven/generated",
        "carven/generated/module.hpp",
    };
    CHECK(expect_termination("target-plan-prefix-artifact-path", [&] {
        verify_target_artifact_logical_paths(prefix_collision);
    }));
}

TEST_CASE("Target names: private collisions do not perturb public allocation") {
    constexpr auto with_private =
        "private struct class {}\n"
        "export struct class_cv { value: i32, }\n"
        "export fn identity(value: class_cv) -> class_cv { return value; }\n";
    constexpr auto without_private =
        "export struct class_cv { value: i32, }\n"
        "export fn identity(value: class_cv) -> class_cv { return value; }\n";

    CHECK_EQ(public_names(with_private), public_names(without_private));
}

TEST_CASE("Target generation: repeated artifact lowering owns independent target types") {
    const auto compilation = PlannedCompilation::build(
        analyze_failure_profiles(),
        request(TestGenerationMode::RunnerHeader, "repeat-artifact")
    );
    for (const auto entry : compilation.target().artifacts()) {
        const auto first = lower_artifact(compilation, entry.id);
        const auto second = lower_artifact(compilation, entry.id);
        CHECK_NE(first.identity(), second.identity());
        const auto render = [](const TargetUnit& unit) static noexcept {
            return render_layout(TargetRenderer(unit, StableInterfaceEmission {}).render_unit());
        };
        CHECK_EQ(render(first), render(second));
    }
}

TEST_CASE("Target generation: declared entry failures retain the ABI and argument forwarding") {
    auto sources = SourceManager();
    const auto source =
        *sources.append_virtual("entry.cv", "struct E {} private fn main(args) throw E {}");
    const auto input = CompilationModuleInput {
        .source_id = source,
        .module_path = *CanonicalModulePath::from_value("entry"),
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = std::span(&input, 1)});
    REQUIRE(syntax.has_value());
    auto semantic = analyze(std::move(*syntax));
    REQUIRE(semantic.has_value());
    const auto compilation = PlannedCompilation::build(
        std::move(semantic->value),
        request(TestGenerationMode::None, "entry-arguments")
    );
    auto wrappers = 0uz;
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        for (const auto& item : unit.sections().epilogue) {
            const auto* declaration = std::get_if<TargetDecl>(&item.value);
            if (declaration == nullptr) {
                continue;
            }
            const auto* entry = std::get_if<TargetFunctionDecl>(declaration);
            if (entry == nullptr) {
                continue;
            }
            ++wrappers;
            REQUIRE_EQ(entry->parameters.size(), 2);
            const auto* body = std::get_if<TargetFreeFunctionDefinition>(&entry->form);
            REQUIRE(body != nullptr);
            REQUIRE_FALSE(body->body.empty());
            const auto* result = std::get_if<TargetVariableStmt>(&body->body.front().value);
            REQUIRE(result != nullptr);
            const auto* call = std::get_if<TargetCallExpr>(&result->initializer.value);
            REQUIRE(call != nullptr);
            REQUIRE_EQ(call->arguments.size(), 1);
            const auto* arguments = std::get_if<TargetCallExpr>(&call->arguments[0].value);
            REQUIRE(arguments != nullptr);
            const auto* adapter = std::get_if<TargetIntrinsicNameExpr>(&arguments->callee->value);
            REQUIRE(adapter != nullptr);
            CHECK_EQ(adapter->symbol, TargetSymbol::RuntimeEntryArgs);
            REQUIRE_EQ(arguments->arguments.size(), 2);
            for (auto index = 0uz; index < entry->parameters.size(); ++index) {
                const auto* forwarded =
                    std::get_if<TargetNameExpr>(&arguments->arguments[index].value);
                REQUIRE(forwarded != nullptr);
                REQUIRE(entry->parameters[index].name.has_value());
                CHECK_EQ(forwarded->name, TargetName(*entry->parameters[index].name));
            }
        }
    }
    CHECK_EQ(wrappers, 1);
}
