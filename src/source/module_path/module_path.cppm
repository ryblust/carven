module carven:source.module_path;

import std;

enum class CanonicalModulePathErrorKind {
    Empty,
    EmptyComponent,
    InvalidIdentifier,
    IncompleteCraftPath,
};

struct CanonicalModulePathError final {
    CanonicalModulePathErrorKind kind;
    std::size_t component_index;
    std::string component;
};

class ModuleDomainPrefix final {
public:
    auto craft_name() const noexcept -> std::optional<std::string_view>;
    auto operator==(const ModuleDomainPrefix&) const noexcept -> bool = default;

private:
    explicit ModuleDomainPrefix(std::optional<std::string> name) noexcept;

    std::optional<std::string> name;

    friend class CanonicalModulePath;
};

class CanonicalModulePath final {
public:
    static auto from_value(std::string_view value) noexcept
        -> std::expected<CanonicalModulePath, CanonicalModulePathError>;
    static auto from_components(std::span<const std::string_view> components) noexcept
        -> std::expected<CanonicalModulePath, CanonicalModulePathError>;
    auto value() const noexcept -> std::string_view;
    auto components() const noexcept -> std::span<const std::string>;
    auto module_domain_prefix() const noexcept -> ModuleDomainPrefix;
    auto domain_relative_components() const noexcept -> std::span<const std::string>;
    auto operator==(const CanonicalModulePath&) const noexcept -> bool;
    auto operator<=>(const CanonicalModulePath&) const noexcept -> std::strong_ordering;

private:
    CanonicalModulePath(std::string value, std::vector<std::string> components) noexcept;

    std::string path;
    std::vector<std::string> path_components;
};

auto same_module_domain(const CanonicalModulePath& left, const CanonicalModulePath& right) noexcept
    -> bool;
