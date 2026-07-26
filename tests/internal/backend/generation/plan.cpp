module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.plan;

import :backend.generate;
import :backend.generation.linkage;
import :backend.generation.plan;
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

auto failure_name(const SemanticProgram& semantic, HIRTypeID type) noexcept -> std::string_view {
    const auto& value = semantic.type(type).value;
    auto symbol = std::optional<SymbolID>();
    if (const auto* structure = std::get_if<HIRStructTypeValue>(&value)) {
        symbol = semantic.structure(structure->structure).symbol;
    } else if (const auto* enumeration = std::get_if<HIREnumTypeValue>(&value)) {
        symbol = semantic.enumeration(enumeration->enumeration).symbol;
    }
    REQUIRE(symbol.has_value());
    return semantic.provenance().spelling(semantic.symbol(*symbol).name);
}

auto callable_failure_set(const SemanticProgram& semantic, std::uint32_t function_index) noexcept
    -> FailureSetID {
    const auto callable = semantic.function(FunctionID::from_index(function_index)).callable;
    return semantic.callable(callable).failure_set;
}

auto profile_names(
    const SemanticProgram& semantic,
    const TargetGenerationPlan& plan,
    FailureSetID failure_set
) noexcept -> std::vector<std::string_view> {
    return plan.failure_set(failure_set).ordered_members
        | std::views::transform([&](HIRTypeID type) noexcept {
               return failure_name(semantic, type);
           })
        | std::ranges::to<std::vector>();
}

} // namespace

TEST_CASE("Target plan: every failure set has one stable nominal representation order") {
    const auto semantic = analyze_failure_profiles();
    const auto request = TargetGenerationRequest {
        .tests = TestEmissionMode::None,
        .linkage = ExplicitLinkageForm {.domain = "failure-profile-test"},
    };
    const auto domain = derive_target_domain_id(semantic, request);
    const auto plan = TargetGenerationPlan::build(semantic, domain);

    const auto cross_module = callable_failure_set(semantic, 0);
    const auto same_module = callable_failure_set(semantic, 1);
    const auto combined = callable_failure_set(semantic, 2);
    CHECK_EQ(
        profile_names(semantic, plan, cross_module),
        std::vector<std::string_view> {"ZFailure", "AFailure"}
    );
    CHECK_EQ(
        profile_names(semantic, plan, same_module),
        std::vector<std::string_view> {"BFailure", "YFailure"}
    );
    CHECK_EQ(
        profile_names(semantic, plan, combined),
        std::vector<std::string_view> {"ZFailure", "BFailure", "YFailure", "AFailure"}
    );
}
