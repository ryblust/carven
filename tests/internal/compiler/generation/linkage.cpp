module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.generation.linkage;

import :artifacts;
import :compilation.request;
import :compiler.compile;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

auto compile_fixture(std::optional<std::string_view> domain = std::nullopt) noexcept
    -> ArtifactSet {
    auto sources = SourceManager();
    const auto source = sources.append_virtual(
        "stable.cv",
        "struct PublicItem {\n"
        "    value: i32,\n"
        "}\n"
    );
    REQUIRE(source.has_value());
    const auto path = CanonicalModulePath::from_value("stable");
    REQUIRE(path.has_value());
    const auto input = CompilationInput {.source_id = *source, .module_path = *path};
    const auto linkage = domain.has_value()
        ? LinkageIdentity(ExplicitLinkageForm {.domain = std::string(*domain)})
        : LinkageIdentity(ContentAddressedLinkageForm {});
    auto result = compile(
        sources,
        CompilationRequest {.inputs = std::span(&input, 1)},
        TargetGenerationRequest {
            .tests = TestEmissionMode::None,
            .linkage = linkage,
        }
    );
    REQUIRE(result.has_value());
    return std::move(result->value);
}

auto interface_content(const ArtifactSet& artifacts) noexcept -> std::string_view {
    const auto found = std::ranges::find_if(artifacts.artifacts(), [](const auto& artifact) static {
        return artifact.logical_path.starts_with("carven/generated/")
            && artifact.logical_path.ends_with(".hpp");
    });
    REQUIRE(found != artifacts.artifacts().end());
    CHECK_EQ(
        std::ranges::count_if(
            artifacts.artifacts(),
            [](const auto& artifact) static {
                return artifact.logical_path.starts_with("carven/generated/")
                    && artifact.logical_path.ends_with(".hpp");
            }
        ),
        1
    );
    return found->content;
}

auto compile_ordered_fixture(bool reverse) noexcept -> ArtifactSet {
    auto sources = SourceManager();
    const auto consumer_source = sources.append_virtual(
        "consumer.cv",
        "import provider using OrderedResult;\n"
        "fn classify(input: OrderedResult) -> i32 {\n"
        "    return match input {\n"
        "        .Value(value) => value,\n"
        "        .Empty => 0,\n"
        "    };\n"
        "}\n"
    );
    const auto provider_source = sources.append_virtual(
        "provider.cv",
        "export enum OrderedResult {\n"
        "    Value(i32),\n"
        "    Empty,\n"
        "}\n"
    );
    REQUIRE(consumer_source.has_value());
    REQUIRE(provider_source.has_value());
    const auto consumer_path = CanonicalModulePath::from_value("consumer");
    const auto provider_path = CanonicalModulePath::from_value("provider");
    REQUIRE(consumer_path.has_value());
    REQUIRE(provider_path.has_value());
    auto inputs = std::array {
        CompilationInput {.source_id = *consumer_source, .module_path = *consumer_path},
        CompilationInput {.source_id = *provider_source, .module_path = *provider_path},
    };
    if (reverse) {
        std::ranges::reverse(inputs);
    }
    auto result = compile(
        sources,
        CompilationRequest {.inputs = inputs},
        TargetGenerationRequest {
            .tests = TestEmissionMode::None,
            .linkage = ContentAddressedLinkageForm {},
        }
    );
    REQUIRE(result.has_value());
    return std::move(result->value);
}

} // namespace

TEST_CASE("Linkage identity: equal input is deterministic") {
    const auto first = compile_fixture();
    const auto second = compile_fixture();

    CHECK_EQ(first.artifacts().size(), second.artifacts().size());
    for (auto index = 0uz; index < first.artifacts().size(); ++index) {
        CHECK_EQ(first.artifacts()[index].logical_path, second.artifacts()[index].logical_path);
        CHECK_EQ(first.artifacts()[index].content, second.artifacts()[index].content);
    }
}

TEST_CASE("Linkage identity: caller domain isolates otherwise identical compilations") {
    const auto left = compile_fixture("target:left");
    const auto left_again = compile_fixture("target:left");
    const auto right = compile_fixture("target:right");

    const auto left_header = interface_content(left);
    CHECK_EQ(left_header, interface_content(left_again));
    CHECK_NE(left_header, interface_content(right));
}

TEST_CASE("Linkage identity: an explicit empty domain remains caller-selected") {
    const auto empty = compile_fixture("");
    const auto fallback = compile_fixture();

    CHECK_NE(interface_content(empty), interface_content(fallback));
}

TEST_CASE("Linkage identity: canonical module ordering ignores request order") {
    const auto forward = compile_ordered_fixture(false);
    const auto reverse = compile_ordered_fixture(true);
    REQUIRE_EQ(forward.artifacts().size(), reverse.artifacts().size());
    for (auto index = 0uz; index < forward.artifacts().size(); ++index) {
        CHECK_EQ(forward.artifacts()[index].logical_path, reverse.artifacts()[index].logical_path);
        CHECK_EQ(forward.artifacts()[index].content, reverse.artifacts()[index].content);
    }
}
