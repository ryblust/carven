module carven:backend.generation.linkage;

import :backend.generation.request;
import std;

class LinkageDomainID final {
public:
    auto hex() const noexcept -> std::string;
    auto namespace_identifier() const noexcept -> std::string;

    auto operator==(const LinkageDomainID&) const noexcept -> bool = default;

private:
    explicit LinkageDomainID(std::array<std::uint8_t, 16> bytes) noexcept;

    std::array<std::uint8_t, 16> value;

    friend auto derive_linkage_domain_id(const TargetGenerationRequest&) noexcept
        -> LinkageDomainID;
};

class ModuleNamespaceID final {
public:
    auto hex() const noexcept -> std::string;
    auto namespace_identifier() const noexcept -> std::string;
    auto operator==(const ModuleNamespaceID&) const noexcept -> bool = default;

private:
    explicit ModuleNamespaceID(std::array<std::uint8_t, 16> bytes) noexcept;
    std::array<std::uint8_t, 16> value;
    friend auto derive_module_namespace_id(std::string_view) noexcept -> ModuleNamespaceID;
};

auto derive_linkage_domain_id(const TargetGenerationRequest& request) noexcept -> LinkageDomainID;
auto derive_module_namespace_id(std::string_view canonical_module_path) noexcept
    -> ModuleNamespaceID;
