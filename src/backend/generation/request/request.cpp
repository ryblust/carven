module carven:backend.generation.request.impl;

import :backend.generation.request;
import std;

auto LinkageDomain::explicit_value(std::string value) noexcept -> std::optional<LinkageDomain> {
    if (value.empty()) {
        return std::nullopt;
    }
    return LinkageDomain(LinkageDomainKind::Explicit, std::move(value));
}

auto LinkageDomain::artifact_root(const std::filesystem::path& root) noexcept
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

auto LinkageDomain::kind() const noexcept -> LinkageDomainKind {
    return domain_kind;
}

auto LinkageDomain::value() const noexcept -> std::string_view {
    return domain_value;
}

LinkageDomain::LinkageDomain(LinkageDomainKind kind, std::string value) noexcept
    : domain_kind(kind),
      domain_value(std::move(value)) {}
