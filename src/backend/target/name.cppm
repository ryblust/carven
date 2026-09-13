module carven:backend.target.name;

import :backend.target.ids;
import std;

class TargetIdentifier final {
public:
    static auto accepts_spelling(std::string_view spelling) noexcept -> bool;
    static auto from_spelling(std::string_view spelling) noexcept -> TargetIdentifier;
    auto spelling() const noexcept -> std::string_view;
    auto operator==(const TargetIdentifier&) const noexcept -> bool = default;

private:
    explicit TargetIdentifier(std::string spelling) noexcept;

    std::string value;
};

struct TargetRawIdentifier final {
    std::string spelling;
};

using TargetMemberName = std::variant<TargetIdentifier, TargetRawIdentifier>;

class TargetName final {
public:
    explicit TargetName(TargetIdentifier identifier) noexcept;
    static auto from_components(std::initializer_list<TargetIdentifier> values) noexcept
        -> TargetName;
    static auto from_components(std::vector<TargetIdentifier> values) noexcept -> TargetName;
    static auto globally_qualified(std::vector<TargetIdentifier> values) noexcept -> TargetName;
    auto components() const noexcept -> std::span<const TargetIdentifier>;
    auto is_globally_qualified() const noexcept -> bool;
    auto append(TargetIdentifier identifier) noexcept -> void;
    auto operator==(const TargetName&) const noexcept -> bool = default;

private:
    TargetName(std::vector<TargetIdentifier> components, bool globally_qualified) noexcept;

    std::vector<TargetIdentifier> name_components;
    bool global_qualification;
};
