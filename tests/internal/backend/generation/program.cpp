module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.program;

import :artifacts;
import :backend.generate;
import :backend.generation.program;
import :compilation.request;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :source.manager;
import :source.module_path;
import std;

namespace {

auto analyze_failure_profiles() noexcept -> SemanticProgram {
    auto sources = SourceManager();
    auto inputs = std::vector<CompilationInput>();
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
    auto parsed = parse(sources, inputs);
    REQUIRE(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    REQUIRE(analyzed.has_value());
    return std::move(analyzed->value);
}

auto failure_name(const TargetArtifactView& artifact_view, HIRTypeID type_id) noexcept
    -> std::string_view {
    const auto& value = artifact_view.type(type_id).value;
    auto symbol = std::optional<SymbolID>();
    if (const auto* structure = std::get_if<HIRStructTypeValue>(&value)) {
        symbol = artifact_view.structure(structure->structure).symbol;
    } else if (const auto* enumeration = std::get_if<HIREnumTypeValue>(&value)) {
        symbol = artifact_view.enumeration(enumeration->enumeration).symbol;
    }
    REQUIRE(symbol.has_value());
    return artifact_view.provenance().spelling(artifact_view.symbol(*symbol).name);
}

auto callable_failure_set(const SemanticProgram& semantic, std::uint32_t function_index) noexcept
    -> FailureSetID {
    const auto callable = semantic.function(FunctionID::from_index(function_index)).callable;
    return semantic.callable_flow(callable).effective_failure_set;
}

auto profile_names(const TargetArtifactView& artifact_view, FailureSetID failure_set_id) noexcept
    -> std::vector<std::string_view> {
    return artifact_view.failure_profile(failure_set_id).ordered_members
        | std::views::transform([&](HIRTypeID type_id) noexcept {
               return failure_name(artifact_view, type_id);
           })
        | std::ranges::to<std::vector>();
}

} // namespace

TEST_CASE("Target program: every failure set has one stable nominal representation order") {
    auto semantic = analyze_failure_profiles();
    const auto request = TargetGenerationRequest {
        .tests = TestEmissionMode::None,
        .linkage_domain = *LinkageDomain::explicit_value("failure-profile-test"),
    };
    const auto cross_module = callable_failure_set(semantic, 0);
    const auto same_module = callable_failure_set(semantic, 1);
    const auto combined = callable_failure_set(semantic, 2);
    const auto program = TargetProgram::build(std::move(semantic), request);
    REQUIRE_FALSE(program.artifacts().empty());
    const auto artifact = program.focused_artifact(TargetArtifactID::from_index(0));
    CHECK_EQ(
        profile_names(artifact, cross_module),
        std::vector<std::string_view> {"ZFailure", "AFailure"}
    );
    CHECK_EQ(
        profile_names(artifact, same_module),
        std::vector<std::string_view> {"BFailure", "YFailure"}
    );
    CHECK_EQ(
        profile_names(artifact, combined),
        std::vector<std::string_view> {"ZFailure", "BFailure", "YFailure", "AFailure"}
    );
}

TEST_CASE("Target program: artifact graph is typed and dependency-first") {
    auto semantic = analyze_failure_profiles();
    const auto program = TargetProgram::build(
        std::move(semantic),
        TargetGenerationRequest {
            .tests = TestEmissionMode::DefaultRunner,
            .linkage_domain = *LinkageDomain::explicit_value("artifact-graph-test"),
        }
    );

    REQUIRE_FALSE(program.artifacts().empty());
    CHECK_EQ(program.artifacts().back().role, GeneratedArtifactRole::TestEntry);
    auto typed_dependency_count = 0uz;
    for (const auto& [index, artifact] : std::views::enumerate(program.artifacts())) {
        CHECK_FALSE(artifact.logical_path.empty());
        CHECK_FALSE(artifact.directive_groups.empty());
        for (const auto& group : artifact.directive_groups) {
            for (const auto& directive : group.directives) {
                if (const auto* include = std::get_if<TargetArtifactIncludeDirective>(&directive)) {
                    ++typed_dependency_count;
                    CHECK_LT(include->artifact.index(), index);
                }
            }
        }
        const auto stable = artifact.source_mapping == ArtifactSourceMappingPolicy::StableInterface;
        CHECK_EQ(stable, artifact.role == GeneratedArtifactRole::Interface);
    }
    CHECK_GT(typed_dependency_count, 0u);
}

TEST_CASE("Target program: carrier conversion uses one sealed classifier") {
    auto semantic = analyze_failure_profiles();
    const auto program = TargetProgram::build(
        std::move(semantic),
        TargetGenerationRequest {
            .tests = TestEmissionMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("carrier-classifier-test"),
        }
    );
    const auto artifact = program.focused_artifact(TargetArtifactID::from_index(0));
    const auto& cross_signature = artifact.callable_signature(CallableID::from_index(0));
    const auto& combined_signature = artifact.callable_signature(CallableID::from_index(2));
    REQUIRE(cross_signature.carrier_shape.has_value());
    REQUIRE(combined_signature.carrier_shape.has_value());
    const auto cross_module = *cross_signature.carrier_shape;
    const auto combined = *combined_signature.carrier_shape;

    CHECK_EQ(
        artifact.classify_carrier_conversion(cross_module, cross_module),
        TargetCarrierConversion::Identity
    );
    CHECK_EQ(
        artifact.classify_carrier_conversion(cross_module, combined),
        TargetCarrierConversion::Widen
    );
}
