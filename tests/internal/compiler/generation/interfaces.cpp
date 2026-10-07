module carven:test.internal.compiler.generation.interfaces;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.report;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

struct ModuleFixture final {
    std::string_view path;
    std::string_view source;
};

auto compile_modules(
    std::span<const ModuleFixture> modules,
    std::string_view linkage_domain = "test:interfaces",
    TestGenerationMode test_mode = TestGenerationMode::None
) noexcept -> GeneratedArtifactSet {
    auto sources = SourceManager();
    auto inputs = std::vector<SourceModuleInput>();
    inputs.reserve(modules.size());
    for (const auto& fixture : modules) {
        const auto source =
            sources.append_virtual(std::format("{}.cv", fixture.path), std::string(fixture.source));
        require(source.has_value());
        const auto path = CanonicalModulePath::from_value(fixture.path);
        require(path.has_value());
        inputs.push_back({.source_id = *source, .module_path = *path});
    }
    auto result = compile(
        sources,
        SourceBatch {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = test_mode,
            .linkage_domain = LinkageDomain::explicit_value(std::string(linkage_domain)).value(),
        }
    );
    require(result.has_value()).note([&] noexcept {
        return render_diagnostics(result.error(), sources);
    });
    return std::move(result->value);
}

auto interfaces(const GeneratedArtifactSet& artifacts) noexcept
    -> std::vector<const GeneratedArtifact*> {
    auto result = std::vector<const GeneratedArtifact*>();
    for (const auto& artifact : artifacts.entries()) {
        if (artifact.logical_path.starts_with("carven/generated/")
            && artifact.logical_path.ends_with(".hpp")) {
            result.push_back(&artifact);
        }
    }
    return result;
}

auto interface_for(const GeneratedArtifactSet& artifacts, std::string_view module_path) noexcept
    -> const GeneratedArtifact& {
    auto logical_path = std::string("carven/generated/");
    logical_path += module_path;
    std::ranges::replace(logical_path, '.', '/');
    logical_path += ".hpp";
    const auto found =
        std::ranges::find(artifacts.entries(), logical_path, &GeneratedArtifact::logical_path);
    require(found != artifacts.entries().end()).note("artifact:", logical_path);
    return *found;
}

auto artifact(const GeneratedArtifactSet& artifacts, std::string_view logical_path) noexcept
    -> const GeneratedArtifact& {
    const auto found =
        std::ranges::find(artifacts.entries(), logical_path, &GeneratedArtifact::logical_path);
    require(found != artifacts.entries().end()).note("artifact:", logical_path);
    return *found;
}

auto component_include(const GeneratedArtifact& component) noexcept -> std::string {
    return std::format("#include <{}>", component.logical_path);
}

const TestSuite suite([] static noexcept {
    "Interface components: stable domain keeps interface changes surface-local"_test =
        [] static noexcept {
            constexpr auto baseline = "private fn helper() -> i32 => 1;\n"
                                      "export struct PublicItem { value: i32, }\n";
            constexpr auto private_edit = "// source line and comment changed\n"
                                          "private fn helper() -> i32 => 2;\n"
                                          "private fn another_helper() -> i32 => 3;\n"
                                          "export struct PublicItem { value: i32, }\n";
            constexpr auto surface_edit = "private fn helper() -> i32 => 1;\n"
                                          "export struct PublicItem { value: i64, }\n";

            const auto stable = compile_modules(std::array {ModuleFixture {"stable", baseline}});
            const auto stable_private =
                compile_modules(std::array {ModuleFixture {"stable", private_edit}});
            const auto stable_surface =
                compile_modules(std::array {ModuleFixture {"stable", surface_edit}});
            const auto& stable_header = interface_for(stable, "stable");
            const auto& private_header = interface_for(stable_private, "stable");
            const auto& surface_header = interface_for(stable_surface, "stable");
            expect_equal(stable_header.logical_path, private_header.logical_path);
            expect_equal(stable_header.logical_path, surface_header.logical_path);
            expect_equal(
                stable_header.logical_path,
                std::string_view("carven/generated/stable.hpp")
            );
            expect_equal(stable_header.content, private_header.content);
            expect_not_equal(stable_header.content, surface_header.content);
            expect(!(stable_header.content.contains("#line")));
        };

    "Interface components: a private implementation edit changes only its module unit"_test =
        [] static noexcept {
            constexpr auto provider_before = "export fn answer() -> i32 => helper();\n"
                                             "private fn helper() -> i32 => 1;\n";
            constexpr auto provider_after = "export fn answer() -> i32 => helper();\n"
                                            "private fn helper() -> i32 => 2;\n";
            constexpr auto consumer = "import provider using answer;\n"
                                      "export fn observed() -> i32 => answer();\n";
            constexpr auto unrelated = "export struct Unrelated { value: i32, }\n";
            const auto before = compile_modules(
                std::array {
                    ModuleFixture {"provider", provider_before},
                    ModuleFixture {"consumer", consumer},
                    ModuleFixture {"unrelated", unrelated},
                }
            );
            const auto after = compile_modules(
                std::array {
                    ModuleFixture {"provider", provider_after},
                    ModuleFixture {"consumer", consumer},
                    ModuleFixture {"unrelated", unrelated},
                }
            );

            if (!expect_equal(before.entries().size(), after.entries().size())) {
                return;
            }
            auto changed = std::vector<std::string_view>();
            for (const auto& [before_artifact, after_artifact] :
                 std::views::zip(before.entries(), after.entries())) {
                if (!expect_equal(before_artifact.logical_path, after_artifact.logical_path)) {
                    return;
                }
                if (before_artifact.content != after_artifact.content) {
                    changed.push_back(before_artifact.logical_path);
                }
            }
            expect((changed == std::vector<std::string_view> {"provider.cpp"}));
        };

    "Interface components: implementation-only references do not merge surfaces"_test =
        [] static noexcept {
            constexpr auto provider = "export fn answer() -> i32 => 42;\n";
            constexpr auto consumer_used = "import provider using answer;\n"
                                           "export struct Consumer { value: i32, }\n"
                                           "fn use_answer() -> i32 => answer();\n";
            constexpr auto consumer_unused = "import provider using answer;\n"
                                             "export struct Consumer { value: i32, }\n"
                                             "fn local_value() -> i32 => 0;\n";
            const auto used = compile_modules(
                std::array {
                    ModuleFixture {"provider", provider},
                    ModuleFixture {"consumer", consumer_used},
                }
            );
            const auto unused = compile_modules(
                std::array {
                    ModuleFixture {"provider", provider},
                    ModuleFixture {"consumer", consumer_unused},
                }
            );

            if (!expect_equal(interfaces(used).size(), 2uz)) {
                return;
            }
            const auto& provider_header = interface_for(used, "provider");
            const auto& consumer_header = interface_for(used, "consumer");
            expect_equal(
                provider_header.logical_path,
                std::string_view("carven/generated/provider.hpp")
            );
            expect_equal(
                consumer_header.logical_path,
                std::string_view("carven/generated/consumer.hpp")
            );
            expect_not_equal(provider_header.logical_path, consumer_header.logical_path);
            const auto& used_cpp = artifact(used, "consumer.cpp");
            expect(used_cpp.content.contains(component_include(provider_header)));
            expect(used_cpp.content.contains(component_include(consumer_header)));

            const auto& unused_provider = interface_for(unused, "provider");
            const auto& unused_consumer = interface_for(unused, "consumer");
            const auto& unused_cpp = artifact(unused, "consumer.cpp");
            expect(!(unused_cpp.content.contains(component_include(unused_provider))));
            expect(unused_cpp.content.contains(component_include(unused_consumer)));
        };

    "Interface components: covered match arms do not create dependencies"_test =
        [] static noexcept {
            constexpr auto provider = "export fn answer() -> i32 => 42;\n";
            constexpr auto consumer = "import provider using answer;\n"
                                      "export fn observed(value: bool) -> i32 {\n"
                                      "    return match value {\n"
                                      "        false | true => 0,\n"
                                      "        _ => answer(),\n"
                                      "    };\n"
                                      "}\n";
            const auto artifacts = compile_modules(
                std::array {
                    ModuleFixture {"provider", provider},
                    ModuleFixture {"consumer", consumer},
                }
            );

            const auto& provider_header = interface_for(artifacts, "provider");
            const auto& consumer_cpp = artifact(artifacts, "consumer.cpp");
            expect(!(consumer_cpp.content.contains(component_include(provider_header))));
            expect(!(consumer_cpp.content.contains("answer(")));
        };

    "Interface components: body-only dependency cycles stay separate"_test = [] static noexcept {
        constexpr auto left = "import right using right_value;\n"
                              "export fn left_value() -> i32 => right_value();\n";
        constexpr auto right = "import left using left_value;\n"
                               "export fn right_value() -> i32 => left_value();\n";
        const auto artifacts = compile_modules(
            std::array {
                ModuleFixture {"left", left},
                ModuleFixture {"right", right},
            }
        );

        if (!expect_equal(interfaces(artifacts).size(), 2uz)) {
            return;
        }
        const auto& left_header = interface_for(artifacts, "left");
        const auto& right_header = interface_for(artifacts, "right");
        expect_equal(left_header.logical_path, std::string_view("carven/generated/left.hpp"));
        expect_equal(right_header.logical_path, std::string_view("carven/generated/right.hpp"));
        expect_not_equal(left_header.logical_path, right_header.logical_path);
        expect(artifact(artifacts, "left.cpp").content.contains(component_include(right_header)));
        expect(artifact(artifacts, "right.cpp").content.contains(component_include(left_header)));
    };

    "Interface components: cyclic published surfaces form one SCC"_test = [] static noexcept {
        constexpr auto left = "import right using RightLeaf;\n"
                              "export struct LeftWrap { right: RightLeaf, }\n";
        constexpr auto right = "import left using LeftWrap;\n"
                               "export struct RightLeaf { value: i32, }\n"
                               "export struct RightWrap { left: LeftWrap, }\n";
        const auto artifacts = compile_modules(
            std::array {
                ModuleFixture {"left", left},
                ModuleFixture {"right", right},
            }
        );

        const auto headers = interfaces(artifacts);
        if (!expect_equal(headers.size(), 1uz)) {
            return;
        }
        expect_equal(headers.front()->logical_path, std::string_view("carven/generated/left.hpp"));
        expect(headers.front()->content.contains("struct LeftWrap"));
        expect(headers.front()->content.contains("struct RightLeaf"));
        expect(headers.front()->content.contains("struct RightWrap"));
        expect(
            artifact(artifacts, "left.cpp").content.starts_with(component_include(*headers.front()))
        );
        expect(artifact(artifacts, "right.cpp")
                   .content.starts_with(component_include(*headers.front())));
    };

    "Interface components: declaration-only predecessors use forward declarations"_test =
        [] static noexcept {
            constexpr auto model = "export struct Model { value: i32, }\n";
            constexpr auto api = "import model using Model;\n"
                                 "export fn identity(&value: Model) -> Model => value;\n";
            const auto artifacts = compile_modules(
                std::array {
                    ModuleFixture {"api", api},
                    ModuleFixture {"model", model},
                }
            );

            if (!expect_equal(interfaces(artifacts).size(), 2uz)) {
                return;
            }
            const auto& model_header = interface_for(artifacts, "model");
            const auto& api_header = interface_for(artifacts, "api");
            expect_equal(model_header.logical_path, std::string_view("carven/generated/model.hpp"));
            expect_equal(api_header.logical_path, std::string_view("carven/generated/api.hpp"));
            expect(!(api_header.content.contains(component_include(model_header))));
            expect(api_header.content.contains("struct Model;"));
            expect(!(model_header.content.contains(component_include(api_header))));
        };

    "Interface components: root-relative includes ignore the including directory"_test =
        [] static noexcept {
            constexpr auto model = "export struct Model { value: i32, }\n";
            constexpr auto shadow = "export struct Shadow { value: i32, }\n";
            constexpr auto api = "import lib.model using Model;\n"
                                 "export struct API { model: Model, }\n";
            const auto artifacts = compile_modules(
                std::array {
                    ModuleFixture {"src.api", api},
                    ModuleFixture {"lib.model", model},
                    ModuleFixture {"src.lib.model", shadow},
                }
            );

            if (!expect_equal(interfaces(artifacts).size(), 3uz)) {
                return;
            }
            const auto& api_header = interface_for(artifacts, "src.api");
            const auto& model_header = interface_for(artifacts, "lib.model");
            expect_equal(api_header.logical_path, std::string_view("carven/generated/src/api.hpp"));
            expect_equal(
                model_header.logical_path,
                std::string_view("carven/generated/lib/model.hpp")
            );
            expect(api_header.content.contains("#include <carven/generated/lib/model.hpp>"));
            expect(!(api_header.content.contains("#include \"carven/generated/lib/model.hpp\"")));
        };

    "Interface components: read parameters and failure results require complete types"_test =
        [] static noexcept {
            constexpr auto outcome_types = "export struct Model { value: i32, }\n"
                                           "export struct Failure { code: i32, }\n";
            constexpr auto parameter_types = "export enum Choice { Value(i32), Empty, }\n"
                                             "export enum Code: u8 { Ready = 1, Done, }\n";
            constexpr auto api = "import outcome_types using { Model, Failure, };\n"
                                 "import parameter_types using { Choice, Code, };\n"
                                 "export fn inspect(\n"
                                 "    model: Model,\n"
                                 "    choice: Choice,\n"
                                 "    code: Code,\n"
                                 "    callback: fn(Model) -> Model,\n"
                                 ") -> Model throw Failure { return callback(model); }\n";
            const auto artifacts = compile_modules(
                std::array {
                    ModuleFixture {"api", api},
                    ModuleFixture {"outcome_types", outcome_types},
                    ModuleFixture {"parameter_types", parameter_types},
                }
            );

            if (!expect_equal(interfaces(artifacts).size(), 3uz)) {
                return;
            }
            const auto& api_header = interface_for(artifacts, "api");
            const auto& outcome_header = interface_for(artifacts, "outcome_types");
            const auto& parameter_header = interface_for(artifacts, "parameter_types");
            expect(api_header.content.contains(component_include(outcome_header)));
            expect(api_header.content.contains(component_include(parameter_header)));
            expect(!(api_header.content.contains("struct Model;")));
            expect(!(api_header.content.contains("struct Failure;")));
        };

    "Interface components: arrays require complete predecessor definitions"_test =
        [] static noexcept {
            constexpr auto model = "export struct Model { value: i32, }\n";
            constexpr auto api = "import model using Model;\n"
                                 "export struct ModelPair { values: [Model; 2], }\n";
            const auto artifacts = compile_modules(
                std::array {
                    ModuleFixture {"api", api},
                    ModuleFixture {"model", model},
                }
            );

            const auto& api_header = interface_for(artifacts, "api");
            const auto& model_header = interface_for(artifacts, "model");
            expect(api_header.content.contains(component_include(model_header)));
        };

    "Interface components: SCC membership reuses the canonical anchor"_test = [] static noexcept {
        constexpr auto split_a = "export struct A { value: i32, }\n";
        constexpr auto split_b = "export struct B { value: i32, }\n";
        constexpr auto merged_a = "import b using BLeaf;\n"
                                  "export struct AWrap { b: BLeaf, }\n";
        constexpr auto merged_b = "import a using AWrap;\n"
                                  "export struct BLeaf { value: i32, }\n"
                                  "export struct BWrap { a: AWrap, }\n";
        const auto split = compile_modules(
            std::array {
                ModuleFixture {"a", split_a},
                ModuleFixture {"b", split_b},
            }
        );
        const auto merged = compile_modules(
            std::array {
                ModuleFixture {"a", merged_a},
                ModuleFixture {"b", merged_b},
            }
        );

        if (!expect_equal(interfaces(split).size(), 2uz)) {
            return;
        }
        expect_equal(
            interface_for(split, "a").logical_path,
            std::string_view("carven/generated/a.hpp")
        );
        expect_equal(
            interface_for(split, "b").logical_path,
            std::string_view("carven/generated/b.hpp")
        );
        const auto merged_headers = interfaces(merged);
        if (!expect_equal(merged_headers.size(), 1uz)) {
            return;
        }
        expect_equal(
            merged_headers.front()->logical_path,
            std::string_view("carven/generated/a.hpp")
        );
    };

    "Artifacts: input order and linkage domain produce deterministic schedules"_test =
        [] static noexcept {
            constexpr auto provider = "export struct Model { value: i32, }\n";
            constexpr auto consumer = "import provider using Model;\n"
                                      "export struct API { model: Model, }\n";
            constexpr auto forward = std::array {
                ModuleFixture {"consumer", consumer},
                ModuleFixture {"provider", provider},
            };
            constexpr auto reverse = std::array {
                ModuleFixture {"provider", provider},
                ModuleFixture {"consumer", consumer},
            };

            const auto first = compile_modules(forward, "test:determinism:first");
            const auto repeated = compile_modules(reverse, "test:determinism:first");
            if (!expect_equal(first.entries().size(), repeated.entries().size())) {
                return;
            }
            for (const auto& [left, right] : std::views::zip(first.entries(), repeated.entries())) {
                expect_equal(left.logical_path, right.logical_path);
                expect_equal(left.role, right.role);
                expect((left.source_mapping == right.source_mapping));
                expect_equal(left.content, right.content);
            }

            const auto other_domain = compile_modules(reverse, "test:determinism:second");
            if (!expect_equal(first.entries().size(), other_domain.entries().size())) {
                return;
            }
            auto changed_content = false;
            for (const auto& [left, right] :
                 std::views::zip(first.entries(), other_domain.entries())) {
                expect_equal(left.logical_path, right.logical_path);
                expect_equal(left.role, right.role);
                changed_content |= left.content != right.content;
            }
            expect(changed_content);
        };

    "Generated artifacts: associated interfaces precede direct dependencies"_test =
        [] static noexcept {
            const auto artifacts = compile_modules(
                std::array {ModuleFixture {
                    "order",
                    R"(export async fn load() -> i32 { return 7; }
enum Status { Pending, Shipped(i32), }
struct Order { id: i32, code: u32, status: Status, items: [str; 2], }
let order = Order { id: 7, code: 9u32, status: Status::Shipped(3), items: ["disk", "cable"], };
println(order);)"
                }}
            );
            const auto& implementation = artifact(artifacts, "order.cpp").content;
            expect(
                implementation.starts_with(component_include(interface_for(artifacts, "order")))
            );
            const auto runtime =
                implementation.find("#include <carven/runtime/display/display.hpp>");
            const auto standard = implementation.find("#include <array>");
            if (!expect(runtime != std::string::npos) || !expect(standard != std::string::npos)) {
                return;
            }
            expect(runtime < standard);
            const auto& header = interface_for(artifacts, "order").content;
            expect(header.starts_with("#pragma once"));
            const auto passing = header.find("#include <carven/runtime/passing.hpp>");
            const auto array = header.find("#include <array>");
            if (!expect(passing != std::string::npos) || !expect(array != std::string::npos)) {
                return;
            }
            expect(passing < array);
            for (const auto content :
                 {std::string_view(implementation), std::string_view(header)}) {
                expect(
                    content.find("#include <carven/runtime/async/async.hpp>")
                    < content.find("#include <array>")
                );
                const auto includes = std::array {
                    std::string_view("#include <carven/runtime/async/async.hpp>"),
                    std::string_view("#include <cstdint>"),
                };
                for (const auto include : includes) {
                    const auto first = content.find(include);
                    if (!expect(first != std::string_view::npos)) {
                        return;
                    }
                    expect(content.find(include, first + include.size()) == std::string_view::npos);
                }
            }
        };

    "Generated test entries: runner header is the first include"_test = [] static noexcept {
        const auto artifacts = compile_modules(
            std::array {ModuleFixture {"tested", "test { check(true); }"}},
            "test:runner",
            TestGenerationMode::RunnerEntryPoint
        );
        const auto& entry = artifact(artifacts, "carven/generated/carven-test-main.cpp").content;
        expect(entry.starts_with("#include <carven/generated/carven-test-runner.hpp>"));
    };

    "Generated interfaces: native environments follow canonical module order"_test =
        [] static noexcept {
            constexpr auto modules = std::array {
                ModuleFixture {
                    "zeta",
                    "import \"zeta.hpp\";\n"
                    "import alpha using AlphaLeaf;\n"
                    "export struct ZetaLeaf { native: ::ZetaNative, }\n"
                    "export struct ZetaWrap { alpha: AlphaLeaf, }\n"
                },
                ModuleFixture {
                    "alpha",
                    "\nimport \"alpha.hpp\";\n"
                    "import <shared.hpp>;\n"
                    "import \"alpha.hpp\";\n"
                    "import zeta using ZetaLeaf;\n"
                    "export struct AlphaLeaf { native: ::AlphaNative, }\n"
                    "export struct AlphaWrap { zeta: ZetaLeaf, }\n"
                },
            };
            const auto artifacts = compile_modules(modules);
            const auto& header = interface_for(artifacts, "alpha").content;
            const auto first = header.find("#include \"alpha.hpp\"");
            const auto shared = header.find("#include <shared.hpp>");
            const auto zeta = header.find("#include \"zeta.hpp\"");
            if (!expect(first != std::string::npos)
                || !expect(shared != std::string::npos)
                || !expect(zeta != std::string::npos)) {
                return;
            }
            const auto repeated = header.find("#include \"alpha.hpp\"", first + 1uz);
            if (!expect(repeated != std::string::npos)) {
                return;
            }
            expect(first < shared);
            expect(shared < repeated);
            expect(repeated < zeta);
            expect(header.find("#include \"alpha.hpp\"", repeated + 1uz) == std::string::npos);
            expect(header.find("#include \"zeta.hpp\"", zeta + 1uz) == std::string::npos);
            const auto& implementation = artifact(artifacts, "alpha.cpp").content;
            expect(implementation.contains("#line 2 \"alpha.cv\"\n#include \"alpha.hpp\""));
            expect(implementation.contains("#line 4 \"alpha.cv\"\n#include \"alpha.hpp\""));
            auto reversed = modules;
            std::ranges::reverse(reversed);
            const auto reordered = compile_modules(reversed);
            expect_equal(header, interface_for(reordered, "alpha").content);
        };

    "Generated interfaces: C++ environments preserve complete ordered imports"_test =
        [] static noexcept {
            constexpr auto modules = std::array {
                ModuleFixture {
                    "provider",
                    "import \"first.hpp\"; import <second.hpp>; import \"first.hpp\";"
                    "import \"lookup.hpp\" using native::{ Thing, unused };"
                    "fn global(value: ::Point) -> ::Point => value;"
                    "fn scoped(value: Thing) -> Thing => value;"
                },
            };
            const auto artifacts = compile_modules(modules);
            const auto& content = interface_for(artifacts, "provider").content;
            const auto first = content.find("#include \"first.hpp\"");
            const auto second = content.find("#include <second.hpp>");
            if (!expect(first != std::string::npos)) {
                return;
            }
            const auto repeated = content.find("#include \"first.hpp\"", first + 1uz);
            if (!expect(second != std::string::npos)) {
                return;
            }
            if (!expect(repeated != std::string::npos)) {
                return;
            }
            expect(first < second);
            expect(second < repeated);
            expect(content.find("#include \"first.hpp\"", repeated + 1uz) == std::string::npos);
        };

    "Generated interfaces: global lookup carries headers without using bindings"_test =
        [] static noexcept {
            constexpr auto modules = std::array {
                ModuleFixture {
                    "global_only",
                    "import \"native.hpp\" using native::unused;"
                    "fn point(value: ::Point) -> ::Point => value;"
                },
            };
            const auto artifacts = compile_modules(modules);
            const auto& content = interface_for(artifacts, "global_only").content;
            expect(content.contains("#include \"native.hpp\""));
            expect(!content.contains("using ::native::unused;"));
        };
});

} // namespace
