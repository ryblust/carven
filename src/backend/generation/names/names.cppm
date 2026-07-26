module carven:backend.generation.names;

import :backend.target.name;
import :backend.generation.linkage;
import :semantic.hir.expr;
import :semantic.hir.ids;
import std;

enum class TargetTemporaryNameKind {
    Discard,
    Operand,
    MatchDone,
    Test,
    Owner,
    Try,
    CatchDone,
    Logic,
    Outcome,
    Region,
    Continue,
    TestValue,
    TestRegistration,
};

struct TargetScopeID final {
    std::uint32_t ordinal;
    constexpr auto operator<=>(const TargetScopeID&) const noexcept = default;
};

struct TargetPayloadEnumCaseNames final {
    TargetIdentifier record_type;
    TargetIdentifier holds_function;
    TargetIdentifier payload_function;
};

struct TargetPayloadEnumNames final {
    std::vector<TargetPayloadEnumCaseNames> cases;
    TargetIdentifier storage_type;
    TargetIdentifier storage_member;
    TargetIdentifier storage_parameter;
};

class TargetNameAllocator final {
public:
    auto source(std::string_view spelling, std::string_view enclosing_class = {}) const noexcept
        -> TargetIdentifier;
    auto alias_scope(TargetScopeID source, TargetScopeID target) noexcept -> void;
    auto canonical_scope(TargetScopeID scope) const noexcept -> TargetScopeID;
    auto reserve(std::string_view spelling) noexcept -> void;
    auto reserve(std::string_view spelling, TargetScopeID scope) noexcept -> void;
    auto local_symbol(
        std::string_view spelling,
        std::uint32_t ordinal,
        TargetScopeID scope,
        std::string_view enclosing_class = {},
        std::span<const TargetIdentifier> avoided = {}
    ) noexcept -> TargetIdentifier;
    auto fresh(TargetTemporaryNameKind kind) noexcept -> TargetIdentifier;
    auto fresh(TargetTemporaryNameKind kind, TargetScopeID scope) noexcept -> TargetIdentifier;

    static auto fixed(std::string_view spelling) noexcept -> TargetIdentifier;
    static auto generated_namespace() noexcept -> TargetName;
    static auto domain_namespace(const TargetDomainID& linkage_domain) noexcept -> TargetName;
    static auto derived_type(const TargetIdentifier& source_name, std::string_view role) noexcept
        -> TargetIdentifier;
    static auto derived_value(std::string_view role, const TargetIdentifier& source_name) noexcept
        -> TargetIdentifier;
    static auto claim_source(
        std::string_view preferred,
        std::flat_set<std::string>& occupied
    ) noexcept -> TargetIdentifier;
    static auto claim_type(
        std::string_view preferred,
        std::flat_set<std::string>& occupied
    ) noexcept -> TargetIdentifier;
    static auto claim_value(
        std::string_view preferred,
        std::flat_set<std::string>& occupied
    ) noexcept -> TargetIdentifier;
    static auto enum_payload_field(std::size_t payload_index) noexcept -> TargetIdentifier;
    static auto process_entry() noexcept -> TargetIdentifier;
    static auto process_argument_count() noexcept -> TargetIdentifier;
    static auto process_argument_vector() noexcept -> TargetIdentifier;

private:
    auto claim(std::string_view preferred) noexcept -> TargetIdentifier;
    auto claim(std::string_view preferred, TargetScopeID scope) noexcept -> TargetIdentifier;

    std::flat_map<std::uint32_t, TargetIdentifier> local_names;
    std::flat_set<std::string> claimed_names;
    std::flat_set<std::string> reserved_names;
    std::flat_map<TargetScopeID, TargetScopeID> scope_aliases;
    std::flat_map<TargetScopeID, std::flat_set<std::string>> scoped_reserved_names;
    std::flat_map<TargetScopeID, std::flat_set<std::string>> local_claimed_names;
};

auto payload_enum_names(
    std::span<const TargetIdentifier> case_names,
    const TargetIdentifier& enum_name
) noexcept -> TargetPayloadEnumNames;
