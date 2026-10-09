module carven:backend.generation.request;

import :source.module_path;
import std;

enum class TestGenerationMode {
    None,
    RunnerHeader,
    RunnerEntryPoint,
};

enum class LinkageDomainKind {
    Explicit,
    ArtifactRoot,
};

class LinkageDomain final {
public:
    static auto explicit_value(std::string value) noexcept -> std::optional<LinkageDomain>;
    static auto artifact_root(const std::filesystem::path& root) noexcept
        -> std::optional<LinkageDomain>;
    auto kind() const noexcept -> LinkageDomainKind;
    auto value() const noexcept -> std::string_view;

private:
    LinkageDomain(LinkageDomainKind kind, std::string value) noexcept;

    LinkageDomainKind domain_kind;
    std::string domain_value;
};

// A closed, independently analyzed source library. Its native implementation
// has one identity independent of the consuming program's test mode.
struct SourceLibrary final {
    std::vector<CanonicalModulePath> modules;
    LinkageDomain linkage_domain;
    bool emit_implementations;
};

struct TargetPlanningRequest final {
    TestGenerationMode test_mode;
    LinkageDomain linkage_domain;
    std::optional<SourceLibrary> library;
};
