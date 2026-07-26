module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.visibility;

import :semantic.visibility;
import :source.manager;
import :source.module_path;
import :source.provenance;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace

TEST_CASE("Visibility: program-module audiences derive from the owned provenance catalog") {
    auto sources = SourceManager();
    const auto alpha_api_source = sources.append_virtual("alpha-api.cv", "");
    const auto alpha_main_source = sources.append_virtual("alpha-main.cv", "");
    const auto beta_api_source = sources.append_virtual("beta-api.cv", "");
    REQUIRE(alpha_api_source.has_value());
    REQUIRE(alpha_main_source.has_value());
    REQUIRE(beta_api_source.has_value());

    auto builder = CompilationProvenanceBuilder();
    const auto alpha_api_snapshot = builder.intern_source_snapshot(sources.view(*alpha_api_source));
    const auto alpha_main_snapshot =
        builder.intern_source_snapshot(sources.view(*alpha_main_source));
    const auto beta_api_snapshot = builder.intern_source_snapshot(sources.view(*beta_api_source));
    const auto alpha_api = builder.append_module({
        .source_id = alpha_api_snapshot,
        .path = path("crafts.alpha.api"),
    });
    const auto alpha_main = builder.append_module({
        .source_id = alpha_main_snapshot,
        .path = path("crafts.alpha.main"),
    });
    const auto beta_api = builder.append_module({
        .source_id = beta_api_snapshot,
        .path = path("crafts.beta.api"),
    });
    const auto provenance = std::move(builder).finish();
    const auto view = provenance.view();
    const auto private_audience =
        declaration_audience(DeclarationVisibility::Module, alpha_api, view);
    const auto* private_module = std::get_if<ModuleAudience>(&private_audience);
    REQUIRE(private_module != nullptr);
    CHECK_EQ(private_module->module_id, alpha_api);

    const auto alpha_audience =
        declaration_audience(DeclarationVisibility::ModuleDomain, alpha_api, view);
    const auto* alpha_domain = std::get_if<ModuleDomainAudience>(&alpha_audience);
    REQUIRE(alpha_domain != nullptr);
    REQUIRE(alpha_domain->prefix.craft_name().has_value());
    CHECK_EQ(*alpha_domain->prefix.craft_name(), "alpha");

    const auto compilation_audience =
        declaration_audience(DeclarationVisibility::Compilation, alpha_api, view);
    CHECK(std::holds_alternative<CompilationAudience>(compilation_audience));

    CHECK(declaration_visible_to(DeclarationVisibility::Module, alpha_api, alpha_api, view));
    CHECK_FALSE(declaration_visible_to(DeclarationVisibility::Module, alpha_api, alpha_main, view));
    CHECK(declaration_visible_to(DeclarationVisibility::ModuleDomain, alpha_api, alpha_main, view));
    CHECK_FALSE(
        declaration_visible_to(DeclarationVisibility::ModuleDomain, alpha_api, beta_api, view)
    );
    CHECK(declaration_visible_to(DeclarationVisibility::Compilation, alpha_api, beta_api, view));

    const auto beta_private_audience =
        declaration_audience(DeclarationVisibility::Module, beta_api, view);
    const auto beta_audience =
        declaration_audience(DeclarationVisibility::ModuleDomain, beta_api, view);
    CHECK(audience_subset_of(private_audience, alpha_audience, view));
    CHECK(audience_subset_of(private_audience, compilation_audience, view));
    CHECK_FALSE(audience_subset_of(beta_private_audience, alpha_audience, view));
    CHECK(audience_subset_of(alpha_audience, compilation_audience, view));
    CHECK_FALSE(audience_subset_of(compilation_audience, alpha_audience, view));
    CHECK_FALSE(audience_subset_of(alpha_audience, beta_audience, view));
}
