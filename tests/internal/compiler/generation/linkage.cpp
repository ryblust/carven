module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.generation.linkage;

import :backend.generation.linkage;
import :backend.generation.request;
import std;

namespace {

auto domain_id(LinkageDomain domain, TestGenerationMode tests = TestGenerationMode::None) noexcept
    -> LinkageDomainID {
    return derive_linkage_domain_id(
        TargetGenerationRequest {
            .test_mode = tests,
            .linkage_domain = std::move(domain),
        }
    );
}

} // namespace

TEST_CASE("Linkage domain: construction establishes resolved identity") {
    CHECK_FALSE(LinkageDomain::explicit_value("").has_value());
    CHECK_FALSE(LinkageDomain::artifact_root("relative/root").has_value());

    const auto explicit_domain = LinkageDomain::explicit_value("target:debug");
    REQUIRE(explicit_domain.has_value());
    CHECK_EQ(explicit_domain->kind(), LinkageDomainKind::Explicit);
    CHECK_EQ(explicit_domain->value(), "target:debug");

    const auto artifact_domain = LinkageDomain::artifact_root("/carven/root/../root");
    REQUIRE(artifact_domain.has_value());
    CHECK_EQ(artifact_domain->kind(), LinkageDomainKind::ArtifactRoot);
    CHECK_EQ(artifact_domain->value(), "/carven/root");

    const auto artifact_domain_with_trailing_separator =
        LinkageDomain::artifact_root("/carven/root/.");
    REQUIRE(artifact_domain_with_trailing_separator.has_value());
    CHECK_EQ(artifact_domain_with_trailing_separator->value(), artifact_domain->value());
}

TEST_CASE("Linkage domain: identity follows resolved domain and emission mode") {
    const auto left = domain_id(LinkageDomain::explicit_value("target:left").value());
    const auto left_again = domain_id(LinkageDomain::explicit_value("target:left").value());
    const auto right = domain_id(LinkageDomain::explicit_value("target:right").value());
    const auto path_spelling = domain_id(LinkageDomain::explicit_value("/carven/root").value());
    const auto artifact_root = domain_id(LinkageDomain::artifact_root("/carven/root").value());
    const auto tests = domain_id(
        LinkageDomain::explicit_value("target:left").value(),
        TestGenerationMode::DefaultRunner
    );

    CHECK_EQ(left, left_again);
    CHECK_NE(left, right);
    CHECK_NE(path_spelling, artifact_root);
    CHECK_NE(left, tests);
}

TEST_CASE("Linkage domain: module namespace follows canonical module identity") {
    CHECK_EQ(derive_module_namespace_id("alpha.beta"), derive_module_namespace_id("alpha.beta"));
    CHECK_NE(derive_module_namespace_id("alpha.beta"), derive_module_namespace_id("alpha.gamma"));
}
