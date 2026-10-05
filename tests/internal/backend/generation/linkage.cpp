module carven:test.internal.backend.generation.linkage;

import :backend.generation.linkage;
import :backend.generation.request;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

auto domain_id(LinkageDomain domain, TestGenerationMode tests = TestGenerationMode::None) noexcept
    -> LinkageDomainID {
    return derive_linkage_domain_id(
        TargetPlanningRequest {
            .test_mode = tests,
            .linkage_domain = std::move(domain),
        }
    );
}

const TestSuite suite([] static noexcept {
    "Linkage domain: construction establishes resolved identity"_test = [] static noexcept {
        expect(!(LinkageDomain::explicit_value("").has_value()));
        expect(!(LinkageDomain::artifact_root("relative/root").has_value()));

        const auto explicit_domain = LinkageDomain::explicit_value("target:debug");
        if (!expect(explicit_domain.has_value())) {
            return;
        }
        expect_equal(explicit_domain->kind(), LinkageDomainKind::Explicit);
        expect_equal(explicit_domain->value(), std::string_view("target:debug"));

        const auto directory = TempDirectory("linkage-domain");
        const auto root = directory.path("root");
        const auto artifact_domain = LinkageDomain::artifact_root(root / ".." / "root");
        if (!expect(artifact_domain.has_value())) {
            return;
        }
        expect_equal(artifact_domain->kind(), LinkageDomainKind::ArtifactRoot);
        expect_equal(artifact_domain->value(), root.generic_string());

        const auto artifact_domain_with_trailing_separator =
            LinkageDomain::artifact_root(root / ".");
        if (!expect(artifact_domain_with_trailing_separator.has_value())) {
            return;
        }
        expect_equal(artifact_domain_with_trailing_separator->value(), artifact_domain->value());
    };

    "Linkage domain: identity follows resolved domain and emission mode"_test = [] static noexcept {
        const auto left = domain_id(LinkageDomain::explicit_value("target:left").value());
        const auto left_again = domain_id(LinkageDomain::explicit_value("target:left").value());
        const auto right = domain_id(LinkageDomain::explicit_value("target:right").value());
        const auto directory = TempDirectory("linkage-domain");
        const auto root = directory.path("root");
        const auto artifact_domain = LinkageDomain::artifact_root(root);
        if (!expect(artifact_domain.has_value())) {
            return;
        }
        const auto path_spelling =
            domain_id(LinkageDomain::explicit_value(root.generic_string()).value());
        const auto artifact_root = domain_id(*artifact_domain);
        const auto tests = domain_id(
            LinkageDomain::explicit_value("target:left").value(),
            TestGenerationMode::RunnerEntryPoint
        );

        expect((left == left_again));
        expect((left != right));
        expect((path_spelling != artifact_root));
        expect((left != tests));
    };

    "Linkage domain: module namespace follows canonical module identity"_test = [] static noexcept {
        expect(
            (derive_module_namespace_id("alpha.beta") == derive_module_namespace_id("alpha.beta"))
        );
        expect(
            (derive_module_namespace_id("alpha.beta") != derive_module_namespace_id("alpha.gamma"))
        );
    };
});

} // namespace
