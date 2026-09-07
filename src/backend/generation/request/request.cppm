module carven:backend.generation.request;

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
    static auto explicit_value(std::string value) noexcept -> std::optional<LinkageDomain> {
        if (value.empty()) {
            return std::nullopt;
        }
        return LinkageDomain(LinkageDomainKind::Explicit, std::move(value));
    }

    static auto artifact_root(const std::filesystem::path& root) noexcept
        -> std::optional<LinkageDomain> {
        if (root.empty() || !root.is_absolute()) {
            return std::nullopt;
        }
        auto normalized = root.lexically_normal();
        while (normalized != normalized.root_path() && normalized.filename().empty()) {
            normalized = normalized.parent_path();
        }
        return LinkageDomain(LinkageDomainKind::ArtifactRoot, normalized.generic_string());
    }

    auto kind() const noexcept -> LinkageDomainKind { return domain_kind; }

    auto value() const noexcept -> std::string_view { return domain_value; }

private:
    LinkageDomain(LinkageDomainKind kind, std::string value) noexcept
        : domain_kind(kind),
          domain_value(std::move(value)) {}

    LinkageDomainKind domain_kind;
    std::string domain_value;
};

struct TargetPlanningRequest final {
    TestGenerationMode test_mode;
    LinkageDomain linkage_domain;
};
