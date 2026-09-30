module carven:backend.generation.names;

import :backend.generation.linkage;
import :backend.target.name;
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
    SuccessProjection,
    FailureProjection,
    PayloadProjection,
    Region,
    Continue,
    Break,
    Explanation,
    CppBoundaryParameter,
};

struct TargetScopeID final {
    std::uint32_t ordinal;
    constexpr auto operator<=>(const TargetScopeID&) const noexcept = default;
};

struct TargetPayloadEnumCaseNames final {
    TargetIdentifier record_type;
    TargetIdentifier projection_function;
};

struct TargetPayloadEnumNames final {
    std::vector<TargetPayloadEnumCaseNames> cases;
    TargetIdentifier storage_type;
    TargetIdentifier storage_member;
};

struct TargetContentName final {
    std::string preferred;
    std::string content;
};

auto content_name_digest(std::string_view content) noexcept -> std::string;

auto source_target_identifier(
    std::string_view spelling,
    std::string_view enclosing_class = {}
) noexcept -> TargetIdentifier;
auto public_target_identifier(std::string_view spelling) noexcept -> TargetIdentifier;
auto generated_target_namespace() noexcept -> TargetName;
auto linkage_target_namespace(const LinkageDomainID& linkage_domain) noexcept -> TargetName;
auto claim_target_identifier(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier;
auto claim_target_type_identifier(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier;
// Equal complete content shares a spelling; distinct content claims a unique
// spelling in content order, independently of request order.
auto claim_content_identifiers(
    std::span<const TargetContentName> requests,
    std::flat_set<std::string>& occupied
) noexcept -> std::vector<TargetIdentifier>;
auto enum_payload_field_identifier(std::size_t payload_index) noexcept -> TargetIdentifier;
auto process_entry_identifier() noexcept -> TargetIdentifier;
auto process_argument_count_identifier() noexcept -> TargetIdentifier;
auto process_argument_vector_identifier() noexcept -> TargetIdentifier;
auto test_context_identifier() noexcept -> TargetIdentifier;

class TargetNameAllocator final {
public:
    TargetNameAllocator() = default;
    // The enclosing names remain immutable and outlive this allocator.
    explicit TargetNameAllocator(const std::flat_set<std::string>& enclosing) noexcept;
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

private:
    auto is_reserved(const std::string& spelling) const noexcept -> bool;
    auto claim(std::string_view preferred) noexcept -> TargetIdentifier;
    auto claim(std::string_view preferred, TargetScopeID scope) noexcept -> TargetIdentifier;

    std::map<std::string, std::size_t> next_suffix;
    std::map<TargetScopeID, std::map<std::string, std::size_t>> scoped_next_suffix;
    std::flat_map<std::uint32_t, TargetIdentifier> local_names;
    std::set<std::string> claimed_names;
    std::set<std::string> reserved_names;
    const std::flat_set<std::string>* enclosing_names = nullptr;
    std::flat_map<TargetScopeID, TargetScopeID> scope_aliases;
    std::map<TargetScopeID, std::set<std::string>> scoped_reserved_names;
    std::map<TargetScopeID, std::set<std::string>> local_claimed_names;
};

auto payload_enum_names(std::span<const TargetIdentifier> case_names) noexcept
    -> TargetPayloadEnumNames;
