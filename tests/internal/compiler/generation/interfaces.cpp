module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.generation.interfaces;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

struct ModuleFixture final {
    std::string_view path;
    std::string_view source;
};

auto compile_modules(
    std::span<const ModuleFixture> modules,
    std::string_view linkage_domain = "test:interfaces"
) noexcept -> GeneratedArtifactSet {
    auto sources = SourceManager();
    auto inputs = std::vector<CompilationModuleInput>();
    inputs.reserve(modules.size());
    for (const auto& fixture : modules) {
        const auto source =
            sources.append_virtual(std::format("{}.cv", fixture.path), std::string(fixture.source));
        REQUIRE(source.has_value());
        const auto path = CanonicalModulePath::from_value(fixture.path);
        REQUIRE(path.has_value());
        inputs.push_back({.source_id = *source, .module_path = *path});
    }
    auto result = compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value(std::string(linkage_domain)).value(),
        }
    );
    if (!result.has_value()) {
        for (const auto& diagnostic : result.error()) {
            INFO(diagnostic.finding.message);
        }
    }
    REQUIRE(result.has_value());
    if (!result.has_value()) {
        return GeneratedArtifactSet(std::vector<GeneratedArtifact> {});
    }
    return std::move(result->value);
}

auto interfaces(const GeneratedArtifactSet& artifacts) noexcept
    -> std::vector<const GeneratedArtifact*> {
    auto result = std::vector<const GeneratedArtifact*>();
    for (const auto& artifact : artifacts.artifacts()) {
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
        std::ranges::find(artifacts.artifacts(), logical_path, &GeneratedArtifact::logical_path);
    REQUIRE(found != artifacts.artifacts().end());
    if (found == artifacts.artifacts().end()) {
        static const auto missing = GeneratedArtifact {
            .logical_path = "missing-interface.hpp",
            .role = GeneratedArtifactRole::Interface,
            .source_mapping = ArtifactSourceMappingPolicy::StableInterface,
            .content = {},
        };
        return missing;
    }
    return *found;
}

auto artifact(const GeneratedArtifactSet& artifacts, std::string_view logical_path) noexcept
    -> const GeneratedArtifact& {
    const auto found =
        std::ranges::find(artifacts.artifacts(), logical_path, &GeneratedArtifact::logical_path);
    REQUIRE(found != artifacts.artifacts().end());
    if (found == artifacts.artifacts().end()) {
        static const auto missing = GeneratedArtifact {
            .logical_path = "missing-artifact.cpp",
            .role = GeneratedArtifactRole::ModuleImplementation,
            .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
            .content = {},
        };
        return missing;
    }
    return *found;
}

auto component_include(const GeneratedArtifact& component) noexcept -> std::string {
    return std::format("#include <{}>", component.logical_path);
}

} // namespace

TEST_CASE("Interface components: stable domain keeps interface changes surface-local") {
    constexpr auto baseline = "private fn helper() -> i32 { return 1; }\n"
                              "export struct PublicItem { value: i32, }\n";
    constexpr auto private_edit = "// source line and comment changed\n"
                                  "private fn helper() -> i32 { return 2; }\n"
                                  "private fn another_helper() -> i32 { return 3; }\n"
                                  "export struct PublicItem { value: i32, }\n";
    constexpr auto surface_edit = "private fn helper() -> i32 { return 1; }\n"
                                  "export struct PublicItem { value: i64, }\n";

    const auto stable = compile_modules(std::array {ModuleFixture {"stable", baseline}});
    const auto stable_private =
        compile_modules(std::array {ModuleFixture {"stable", private_edit}});
    const auto stable_surface =
        compile_modules(std::array {ModuleFixture {"stable", surface_edit}});
    const auto& stable_header = interface_for(stable, "stable");
    const auto& private_header = interface_for(stable_private, "stable");
    const auto& surface_header = interface_for(stable_surface, "stable");
    CHECK_EQ(stable_header.logical_path, private_header.logical_path);
    CHECK_EQ(stable_header.logical_path, surface_header.logical_path);
    CHECK_EQ(stable_header.logical_path, "carven/generated/stable.hpp");
    CHECK_EQ(stable_header.content, private_header.content);
    CHECK_NE(stable_header.content, surface_header.content);
    CHECK_FALSE(stable_header.content.contains("#line"));
}

TEST_CASE("Interface components: a private implementation edit changes only its module unit") {
    constexpr auto provider_before = "export fn answer() -> i32 { return helper(); }\n"
                                     "private fn helper() -> i32 { return 1; }\n";
    constexpr auto provider_after = "export fn answer() -> i32 { return helper(); }\n"
                                    "private fn helper() -> i32 { return 2; }\n";
    constexpr auto consumer = "import provider using answer;\n"
                              "export fn observed() -> i32 { return answer(); }\n";
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

    REQUIRE_EQ(before.artifacts().size(), after.artifacts().size());
    auto changed = std::vector<std::string_view>();
    for (const auto& [before_artifact, after_artifact] :
         std::views::zip(before.artifacts(), after.artifacts())) {
        REQUIRE_EQ(before_artifact.logical_path, after_artifact.logical_path);
        if (before_artifact.content != after_artifact.content) {
            changed.push_back(before_artifact.logical_path);
        }
    }
    CHECK_EQ(changed, std::vector<std::string_view> {"provider.cpp"});
}

TEST_CASE("Interface components: implementation-only references do not merge surfaces") {
    constexpr auto provider = "export fn answer() -> i32 { return 42; }\n";
    constexpr auto consumer_used = "import provider using answer;\n"
                                   "export struct Consumer { value: i32, }\n"
                                   "private fn use_answer() -> i32 { return answer(); }\n";
    constexpr auto consumer_unused = "import provider using answer;\n"
                                     "export struct Consumer { value: i32, }\n"
                                     "private fn local_value() -> i32 { return 0; }\n";
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

    REQUIRE_EQ(interfaces(used).size(), 2);
    const auto& provider_header = interface_for(used, "provider");
    const auto& consumer_header = interface_for(used, "consumer");
    CHECK_EQ(provider_header.logical_path, "carven/generated/provider.hpp");
    CHECK_EQ(consumer_header.logical_path, "carven/generated/consumer.hpp");
    CHECK_NE(provider_header.logical_path, consumer_header.logical_path);
    const auto& used_cpp = artifact(used, "consumer.cpp");
    CHECK(used_cpp.content.contains(component_include(provider_header)));
    CHECK(used_cpp.content.contains(component_include(consumer_header)));

    const auto& unused_provider = interface_for(unused, "provider");
    const auto& unused_consumer = interface_for(unused, "consumer");
    const auto& unused_cpp = artifact(unused, "consumer.cpp");
    CHECK_FALSE(unused_cpp.content.contains(component_include(unused_provider)));
    CHECK(unused_cpp.content.contains(component_include(unused_consumer)));
}

TEST_CASE("Interface components: covered match arms do not create dependencies") {
    constexpr auto provider = "export fn answer() -> i32 { return 42; }\n";
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
    CHECK_FALSE(consumer_cpp.content.contains(component_include(provider_header)));
    CHECK_FALSE(consumer_cpp.content.contains("answer("));
}

TEST_CASE("Interface components: body-only dependency cycles stay separate") {
    constexpr auto left = "import right using right_value;\n"
                          "export fn left_value() -> i32 { return right_value(); }\n";
    constexpr auto right = "import left using left_value;\n"
                           "export fn right_value() -> i32 { return left_value(); }\n";
    const auto artifacts = compile_modules(
        std::array {
            ModuleFixture {"left", left},
            ModuleFixture {"right", right},
        }
    );

    REQUIRE_EQ(interfaces(artifacts).size(), 2);
    const auto& left_header = interface_for(artifacts, "left");
    const auto& right_header = interface_for(artifacts, "right");
    CHECK_EQ(left_header.logical_path, "carven/generated/left.hpp");
    CHECK_EQ(right_header.logical_path, "carven/generated/right.hpp");
    CHECK_NE(left_header.logical_path, right_header.logical_path);
    CHECK(artifact(artifacts, "left.cpp").content.contains(component_include(right_header)));
    CHECK(artifact(artifacts, "right.cpp").content.contains(component_include(left_header)));
}

TEST_CASE("Interface components: cyclic published surfaces form one SCC") {
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
    REQUIRE_EQ(headers.size(), 1);
    CHECK_EQ(headers.front()->logical_path, "carven/generated/left.hpp");
    CHECK(headers.front()->content.contains("struct LeftWrap"));
    CHECK(headers.front()->content.contains("struct RightLeaf"));
    CHECK(headers.front()->content.contains("struct RightWrap"));
    CHECK(artifact(artifacts, "left.cpp").content.contains(component_include(*headers.front())));
    CHECK(artifact(artifacts, "right.cpp").content.contains(component_include(*headers.front())));
}

TEST_CASE("Interface components: declaration-only predecessors use forward declarations") {
    constexpr auto model = "export struct Model { value: i32, }\n";
    constexpr auto api = "import model using Model;\n"
                         "export fn identity(&value: Model) -> Model { return value; }\n";
    const auto artifacts = compile_modules(
        std::array {
            ModuleFixture {"api", api},
            ModuleFixture {"model", model},
        }
    );

    REQUIRE_EQ(interfaces(artifacts).size(), 2);
    const auto& model_header = interface_for(artifacts, "model");
    const auto& api_header = interface_for(artifacts, "api");
    CHECK_EQ(model_header.logical_path, "carven/generated/model.hpp");
    CHECK_EQ(api_header.logical_path, "carven/generated/api.hpp");
    CHECK_FALSE(api_header.content.contains(component_include(model_header)));
    CHECK(api_header.content.contains("struct Model;"));
    CHECK_FALSE(model_header.content.contains(component_include(api_header)));
}

TEST_CASE("Interface components: root-relative includes ignore the including directory") {
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

    REQUIRE_EQ(interfaces(artifacts).size(), 3);
    const auto& api_header = interface_for(artifacts, "src.api");
    const auto& model_header = interface_for(artifacts, "lib.model");
    CHECK_EQ(api_header.logical_path, "carven/generated/src/api.hpp");
    CHECK_EQ(model_header.logical_path, "carven/generated/lib/model.hpp");
    CHECK(api_header.content.contains("#include <carven/generated/lib/model.hpp>"));
    CHECK_FALSE(api_header.content.contains("#include \"carven/generated/lib/model.hpp\""));
}

TEST_CASE("Interface components: read parameters and failure results require complete types") {
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

    REQUIRE_EQ(interfaces(artifacts).size(), 3);
    const auto& api_header = interface_for(artifacts, "api");
    const auto& outcome_header = interface_for(artifacts, "outcome_types");
    const auto& parameter_header = interface_for(artifacts, "parameter_types");
    CHECK(api_header.content.contains(component_include(outcome_header)));
    CHECK(api_header.content.contains(component_include(parameter_header)));
    CHECK_FALSE(api_header.content.contains("struct Model;"));
    CHECK_FALSE(api_header.content.contains("struct Failure;"));
}

TEST_CASE("Interface components: arrays require complete predecessor definitions") {
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
    CHECK(api_header.content.contains(component_include(model_header)));
}

TEST_CASE("Interface components: SCC membership reuses the canonical anchor") {
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

    REQUIRE_EQ(interfaces(split).size(), 2);
    CHECK_EQ(interface_for(split, "a").logical_path, "carven/generated/a.hpp");
    CHECK_EQ(interface_for(split, "b").logical_path, "carven/generated/b.hpp");
    const auto merged_headers = interfaces(merged);
    REQUIRE_EQ(merged_headers.size(), 1);
    CHECK_EQ(merged_headers.front()->logical_path, "carven/generated/a.hpp");
}

TEST_CASE("Artifacts: input order and linkage domain produce deterministic schedules") {
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
    REQUIRE_EQ(first.artifacts().size(), repeated.artifacts().size());
    for (const auto& [left, right] : std::views::zip(first.artifacts(), repeated.artifacts())) {
        CHECK_EQ(left.logical_path, right.logical_path);
        CHECK_EQ(left.role, right.role);
        CHECK_EQ(left.source_mapping, right.source_mapping);
        CHECK_EQ(left.content, right.content);
    }

    const auto other_domain = compile_modules(reverse, "test:determinism:second");
    REQUIRE_EQ(first.artifacts().size(), other_domain.artifacts().size());
    auto changed_content = false;
    for (const auto& [left, right] : std::views::zip(first.artifacts(), other_domain.artifacts())) {
        CHECK_EQ(left.logical_path, right.logical_path);
        CHECK_EQ(left.role, right.role);
        changed_content |= left.content != right.content;
    }
    CHECK(changed_content);
}

TEST_CASE("Generated interfaces: C++ environments preserve complete ordered imports") {
    constexpr auto modules = std::array {
        ModuleFixture {
            "provider",
            "import \"first.hpp\"; import <second.hpp>; import \"first.hpp\";"
            "import \"lookup.hpp\" using { native::Thing, native::unused };"
            "fn global(value: ::Point) -> ::Point { return value; }"
            "fn scoped(value: Thing) -> Thing { return value; }"
        },
    };
    const auto artifacts = compile_modules(modules);
    const auto& content = interface_for(artifacts, "provider").content;
    const auto first = content.find("#include \"first.hpp\"");
    const auto second = content.find("#include <second.hpp>");
    REQUIRE(first != std::string::npos);
    const auto repeated = content.find("#include \"first.hpp\"", first + 1uz);
    REQUIRE(second != std::string::npos);
    REQUIRE(repeated != std::string::npos);
    CHECK(first < second);
    CHECK(second < repeated);
    CHECK(content.find("#include \"first.hpp\"", repeated + 1uz) == std::string::npos);
    CHECK(content.contains("using ::native::unused;"));
}

TEST_CASE("Generated interfaces: global lookup carries headers without using bindings") {
    constexpr auto modules = std::array {
        ModuleFixture {
            "global_only",
            "import \"native.hpp\" using native::unused;"
            "fn point(value: ::Point) -> ::Point { return value; }"
        },
    };
    const auto artifacts = compile_modules(modules);
    const auto& content = interface_for(artifacts, "global_only").content;
    CHECK(content.contains("#include \"native.hpp\""));
    CHECK(!content.contains("using ::native::unused;"));
}
