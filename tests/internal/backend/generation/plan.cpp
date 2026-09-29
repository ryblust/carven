module carven:test.internal.backend.generation.plan;

import :artifacts;
import :backend.emission.layout;
import :backend.emission.render;
import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :support.visit;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

auto analyze_failure_profiles() noexcept -> SemIRProgram {
    auto sources = SourceManager();
    auto inputs = std::vector<SourceModuleInput>();
    const auto append = [&](std::string_view path_text, std::string source_text) noexcept {
        const auto source =
            sources.append_virtual(std::format("{}.cv", path_text), std::move(source_text));
        ct::require(source.has_value());
        const auto path = CanonicalModulePath::from_value(path_text);
        ct::require(path.has_value());
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
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    ct::require(syntax.has_value());
    auto semantic = analyze(std::move(*syntax));
    ct::require(semantic.has_value());
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
    ct::expect(false).note(std::format("missing function '{}'", name));
    for (const auto function : semantic.declarations().functions()) {
        return function.id;
    }
    std::unreachable();
}

auto failure_name(const SemIRProgram& semantic, TypeID type) noexcept -> std::string_view {
    return semantic.types().type(type).value.visit(
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
            [](const auto&) static noexcept -> std::string_view {
                ct::expect(false).note("failure ABI member is not nominal");
                return {};
            },
        }
    );
}

auto public_names(std::string source_text) noexcept -> std::array<std::string, 2> {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("support.cv", std::move(source_text));
    ct::require(source.has_value());
    const auto path = CanonicalModulePath::from_value("support");
    ct::require(path.has_value());
    const auto input = SourceModuleInput {.source_id = *source, .module_path = *path};
    auto syntax = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
    ct::require(syntax.has_value());
    auto analyzed = analyze(std::move(*syntax));
    ct::require(analyzed.has_value());
    const auto compilation = PlannedCompilation::build(
        std::move(analyzed->value),
        request(TestGenerationMode::None, "name-plan-test")
    );
    const auto structure = [&]() noexcept {
        for (const auto entry : compilation.semantic().declarations().structures()) {
            if (compilation.semantic().provenance().spelling(entry.value.name) == "union_cv") {
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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Target plan: failure ABI has one deterministic nominal order", [] static noexcept {
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

        ct::expect((
            names == std::vector<std::string_view> {"ZFailure", "BFailure", "YFailure", "AFailure"}
        ));
        ct::expect((compilation.target().semantic_identity() == compilation.semantic().identity()));
    });

    ct::test("Target plan: artifact IDs are owner-bound and dependency-first", [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_failure_profiles(),
            request(TestGenerationMode::RunnerEntryPoint, "artifact-graph-test")
        );
        auto count = 0uz;
        auto dependency_count = 0uz;
        auto last_role = GeneratedArtifactRole::Interface;
        for (const auto entry : compilation.target().artifacts()) {
            ++count;
            ct::expect((entry.id.owner() == compilation.target().identity()));
            ct::expect(!(artifact_logical_path(entry.value).empty()));
            for (const auto dependency : artifact_dependencies(entry.value)) {
                ++dependency_count;
                ct::expect((dependency.owner() == compilation.target().identity()));
                ct::expect_less(dependency.index(), entry.id.index());
            }
            last_role = artifact_role(entry.value);
        }
        ct::expect_equal(count, compilation.target().artifact_count());
        ct::expect_greater(dependency_count, 0u);
        ct::expect_equal(last_role, GeneratedArtifactRole::TestEntry);
    });

    ct::test(
        "Target plan: generated test artifacts occupy the private path domain",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_failure_profiles(),
                request(TestGenerationMode::RunnerEntryPoint, "test-artifact-paths")
            );
            auto logical_paths = std::vector<std::string_view>();
            for (const auto artifact : compilation.target().artifacts()) {
                logical_paths.push_back(artifact_logical_path(artifact.value));
            }

            ct::expect(
                std::ranges::contains(logical_paths, "carven/generated/carven-test-runner.hpp")
            );
            ct::expect(
                std::ranges::contains(logical_paths, "carven/generated/carven-test-main.cpp")
            );
            ct::expect(!(std::ranges::contains(logical_paths, "carven-test-runner.hpp")));
            ct::expect(!(std::ranges::contains(logical_paths, "carven-test-main.cpp")));

            const auto module_and_runner_paths = std::array<std::string, 3> {
                module_implementation_logical_path(std::array<std::string, 1> {"carven-test-main"}),
                "carven/generated/carven-test-runner.hpp",
                "carven/generated/carven-test-main.cpp",
            };
            verify_target_artifact_logical_paths(module_and_runner_paths);
        }
    );

    ct::test("Target plan: malformed artifact path schedules fail before seal", [] static noexcept {
        const auto duplicate = std::array<std::string, 2> {
            "module.cpp",
            "module.cpp",
        };
        ct::expect(expect_termination("target-plan-duplicate-artifact-path", [&] noexcept {
            verify_target_artifact_logical_paths(duplicate);
        }));

        const auto prefix_collision = std::array<std::string, 2> {
            "carven/generated",
            "carven/generated/module.hpp",
        };
        ct::expect(expect_termination("target-plan-prefix-artifact-path", [&] noexcept {
            verify_target_artifact_logical_paths(prefix_collision);
        }));
    });

    ct::test(
        "Target names: private collisions do not perturb public allocation",
        [] static noexcept {
            constexpr auto with_private =
                "private struct union {}\n"
                "export struct union_cv { value: i32, }\n"
                "export fn identity(value: union_cv) -> union_cv { return value; }\n";
            constexpr auto without_private =
                "export struct union_cv { value: i32, }\n"
                "export fn identity(value: union_cv) -> union_cv { return value; }\n";

            ct::expect(public_names(with_private) == public_names(without_private));
        }
    );

    ct::test(
        "Target generation: repeated artifact lowering owns independent target types",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_failure_profiles(),
                request(TestGenerationMode::RunnerHeader, "repeat-artifact")
            );
            for (const auto entry : compilation.target().artifacts()) {
                const auto first = lower_artifact(compilation, entry.id);
                const auto second = lower_artifact(compilation, entry.id);
                ct::expect((first.identity() != second.identity()));
                const auto render = [](const TargetUnit& unit) static noexcept {
                    return render_layout(
                        TargetRenderer(unit, StableInterfaceEmission {}).render_unit()
                    );
                };
                ct::expect_equal(render(first), render(second));
            }
        }
    );

    ct::test(
        "Target generation: declared entry failures retain the ABI and argument forwarding",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source =
                *sources.append_virtual("entry.cv", "struct E {} private fn main(args) throw E {}");
            const auto input = SourceModuleInput {
                .source_id = source,
                .module_path = *CanonicalModulePath::from_value("entry"),
            };
            auto syntax = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
            if (!ct::expect(syntax.has_value())) {
                return;
            }
            auto semantic = analyze(std::move(*syntax));
            if (!ct::expect(semantic.has_value())) {
                return;
            }
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
                    if (!ct::expect_equal(entry->parameters.size(), 2uz)) {
                        return;
                    }
                    const auto* body = std::get_if<TargetFreeFunctionDefinition>(&entry->form);
                    if (!ct::expect(body != nullptr)) {
                        return;
                    }
                    if (!ct::expect(!(body->body.empty()))) {
                        return;
                    }
                    const auto* result = std::get_if<TargetVariableStmt>(&body->body.front().value);
                    if (!ct::expect(result != nullptr)) {
                        return;
                    }
                    const auto* call = std::get_if<TargetCallExpr>(&result->initializer.value);
                    if (!ct::expect(call != nullptr)) {
                        return;
                    }
                    if (!ct::expect_equal(call->arguments.size(), 1uz)) {
                        return;
                    }
                    const auto* arguments = std::get_if<TargetCallExpr>(&call->arguments[0].value);
                    if (!ct::expect(arguments != nullptr)) {
                        return;
                    }
                    const auto* adapter =
                        std::get_if<TargetIntrinsicNameExpr>(&arguments->callee->value);
                    if (!ct::expect(adapter != nullptr)) {
                        return;
                    }
                    ct::expect_equal(adapter->symbol, TargetSymbol::RuntimeEntryArgs);
                    if (!ct::expect_equal(arguments->arguments.size(), 2uz)) {
                        return;
                    }
                    for (auto index = 0uz; index < entry->parameters.size(); ++index) {
                        const auto* forwarded =
                            std::get_if<TargetLocalExpr>(&arguments->arguments[index].value);
                        if (!ct::expect(forwarded != nullptr)) {
                            return;
                        }
                        if (!ct::expect(entry->parameters[index].local.has_value())) {
                            return;
                        }
                        ct::expect((forwarded->local == *entry->parameters[index].local));
                    }
                }
            }
            ct::expect_equal(wrappers, 1uz);
        }
    );
});

} // namespace
