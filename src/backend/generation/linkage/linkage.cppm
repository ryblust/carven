module carven:backend.generation.linkage;

import :backend.generate;
import :semantic.hir;
import std;

class TargetDomainID final {
public:
    auto hex() const noexcept -> std::string;
    auto namespace_identifier() const noexcept -> std::string;

    auto operator==(const TargetDomainID&) const noexcept -> bool = default;

private:
    explicit TargetDomainID(std::array<std::uint8_t, 16> bytes) noexcept;

    std::array<std::uint8_t, 16> value;

    friend auto derive_target_domain_id(
        const SemanticProgram&,
        const TargetGenerationRequest&
    ) noexcept -> TargetDomainID;
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

auto derive_target_domain_id(
    const SemanticProgram& semantic,
    const TargetGenerationRequest& request
) noexcept -> TargetDomainID;
auto derive_module_namespace_id(std::string_view canonical_module_path) noexcept
    -> ModuleNamespaceID;
