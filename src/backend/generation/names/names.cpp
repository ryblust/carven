module carven:backend.generation.names.impl;

import :backend.generation.names;
import std;

namespace {

auto implementation_reserved(std::string_view spelling) noexcept -> bool {
    return spelling.contains("__")
        || (spelling.size() >= 2 && spelling[0] == '_' && spelling[1] >= 'A' && spelling[1] <= 'Z');
}

auto encoded_identifier(std::string_view spelling, std::string_view prefix = "cv_name_") noexcept
    -> std::string {
    static constexpr auto digits = std::string_view("0123456789abcdef");
    auto escaped = std::string(prefix);
    escaped.reserve(escaped.size() + spelling.size() * 2);
    for (const auto byte : spelling) {
        const auto value = static_cast<unsigned char>(byte);
        escaped += digits[value >> 4u];
        escaped += digits[value & 0x0fu];
    }
    return escaped;
}

auto generated_content_identifier(std::string_view spelling) noexcept -> bool {
    return spelling == "CarvenQuery"
        || spelling == "CarvenDisplay"
        || spelling == "carven_constant"
        || spelling.starts_with("CarvenQuery_")
        || spelling.starts_with("CarvenDisplay_")
        || spelling.starts_with("carven_constant_");
}

auto source_identifier(std::string_view spelling, std::string_view enclosing_class) noexcept
    -> std::string {
    auto result = TargetIdentifier::accepts_spelling(spelling)
            && !implementation_reserved(spelling)
            && !generated_content_identifier(spelling)
            && !spelling.starts_with("cv_name_")
        ? std::string(spelling)
        : encoded_identifier(spelling);
    if (result == enclosing_class) {
        return encoded_identifier(spelling, "cv_name_member_");
    }
    return result;
}

auto upper_camel_spelling(std::string_view spelling) noexcept -> std::string {
    auto result = std::string {};
    result.reserve(spelling.size());
    auto capitalize = true;
    for (const auto value : spelling) {
        if (value == '_') {
            capitalize = true;
            continue;
        }
        result += capitalize && value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A')
                                                             : value;
        capitalize = false;
    }
    if (result.empty() || (result.front() >= '0' && result.front() <= '9')) {
        result.insert(0, "Name");
    }
    return result;
}

auto lower_snake_spelling(std::string_view spelling) noexcept -> std::string {
    const auto is_lower = [](char value) static noexcept {
        return value >= 'a' && value <= 'z';
    };
    const auto is_upper = [](char value) static noexcept {
        return value >= 'A' && value <= 'Z';
    };
    const auto is_digit = [](char value) static noexcept {
        return value >= '0' && value <= '9';
    };
    auto result = std::string {};
    result.reserve(spelling.size());
    for (auto index = 0uz; index < spelling.size(); ++index) {
        const auto value = spelling[index];
        if (value == '_') {
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
            continue;
        }
        if (is_upper(value)) {
            const auto previous_boundary =
                index != 0 && (is_lower(spelling[index - 1]) || is_digit(spelling[index - 1]));
            const auto acronym_boundary = index != 0
                && is_upper(spelling[index - 1])
                && index + 1 < spelling.size()
                && is_lower(spelling[index + 1]);
            if (!result.empty()
                && result.back() != '_'
                && (previous_boundary || acronym_boundary)) {
                result += '_';
            }
            result += static_cast<char>(value - 'A' + 'a');
            continue;
        }
        result += value;
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? "value" : result;
}

auto claim_spelling(
    std::string_view preferred,
    std::string_view suffix_separator,
    std::flat_set<std::string>& occupied
) noexcept -> std::string {
    auto candidate = std::string(preferred);
    auto suffix = 2uz;
    while (!occupied.insert(candidate).second) {
        candidate = std::format("{}{}{}", preferred, suffix_separator, suffix++);
    }
    return candidate;
}

auto derived_type_identifier(const TargetIdentifier& source_name, std::string_view role) noexcept
    -> TargetIdentifier {
    return TargetIdentifier::from_spelling(
        std::format("{}{}", upper_camel_spelling(source_name.spelling()), role)
    );
}

auto derived_value_identifier(std::string_view role, const TargetIdentifier& source_name) noexcept
    -> TargetIdentifier {
    return TargetIdentifier::from_spelling(
        std::format("{}_{}", role, lower_snake_spelling(source_name.spelling()))
    );
}

auto temporary_stem(TargetTemporaryNameKind kind) noexcept -> std::string_view {
    switch (kind) {
        case TargetTemporaryNameKind::Discard:              return "discard";
        case TargetTemporaryNameKind::Operand:              return "operand";
        case TargetTemporaryNameKind::MatchDone:            return "match_done";
        case TargetTemporaryNameKind::Test:                 return "test_case";
        case TargetTemporaryNameKind::Owner:                return "owner";
        case TargetTemporaryNameKind::Try:                  return "try_value";
        case TargetTemporaryNameKind::CatchDone:            return "catch_done";
        case TargetTemporaryNameKind::Logic:                return "logic_value";
        case TargetTemporaryNameKind::Outcome:              return "outcome";
        case TargetTemporaryNameKind::SuccessProjection:    return "success";
        case TargetTemporaryNameKind::FailureProjection:    return "failure";
        case TargetTemporaryNameKind::PayloadProjection:    return "payload";
        case TargetTemporaryNameKind::Region:               return "region";
        case TargetTemporaryNameKind::Break:                return "cv_break";
        case TargetTemporaryNameKind::Continue:             return "continue_target";
        case TargetTemporaryNameKind::Explanation:          return "explanation";
        case TargetTemporaryNameKind::CppBoundaryParameter: return "cpp_boundary_parameter";
    }
    std::unreachable();
}

} // namespace

TargetNameAllocator::TargetNameAllocator(const std::flat_set<std::string>& enclosing) noexcept
    : enclosing_names(std::addressof(enclosing)) {}

auto TargetNameAllocator::is_reserved(const std::string& spelling) const noexcept -> bool {
    return reserved_names.contains(spelling)
        || (enclosing_names != nullptr && enclosing_names->contains(spelling));
}

auto source_target_identifier(std::string_view spelling, std::string_view enclosing_class) noexcept
    -> TargetIdentifier {
    return TargetIdentifier::from_spelling(source_identifier(spelling, enclosing_class));
}

auto TargetNameAllocator::alias_scope(TargetScopeID source, TargetScopeID target) noexcept -> void {
    source = canonical_scope(source);
    target = canonical_scope(target);
    if (source == target) {
        return;
    }
    scope_aliases.insert_or_assign(source, target);
}

auto TargetNameAllocator::canonical_scope(TargetScopeID scope) const noexcept -> TargetScopeID {
    auto current = scope;
    for (auto alias = scope_aliases.find(current); alias != scope_aliases.end();
         alias = scope_aliases.find(current)) {
        current = alias->second;
    }
    return current;
}

auto TargetNameAllocator::reserve(std::string_view spelling) noexcept -> void {
    reserved_names.insert(std::string(spelling));
}

auto TargetNameAllocator::reserve(std::string_view spelling, TargetScopeID scope) noexcept -> void {
    scoped_reserved_names[canonical_scope(scope)].insert(std::string(spelling));
}

auto TargetNameAllocator::local_symbol(
    std::string_view spelling,
    std::uint32_t ordinal,
    TargetScopeID scope,
    std::string_view enclosing_class,
    std::span<const TargetIdentifier> avoided
) noexcept -> TargetIdentifier {
    scope = canonical_scope(scope);
    const auto existing = local_names.find(ordinal);
    if (existing != local_names.end()) {
        return existing->second;
    }
    const auto preferred =
        std::string(source_target_identifier(spelling, enclosing_class).spelling());
    auto candidate = preferred;
    auto& suffix = next_suffix.try_emplace(std::string(preferred), 2uz).first->second;
    auto& scope_names = local_claimed_names[scope];
    const auto& reserved = scoped_reserved_names[scope];
    const auto conflicts_with_initializer = [&](std::string_view value) noexcept {
        return std::ranges::any_of(avoided, [&](const TargetIdentifier& identifier) noexcept {
            return identifier.spelling() == value;
        });
    };
    while (is_reserved(candidate)
           || claimed_names.contains(candidate)
           || reserved.contains(candidate)
           || conflicts_with_initializer(candidate)
           || !scope_names.insert(candidate).second) {
        candidate = std::format("{}_{}", preferred, suffix++);
    }
    auto identifier = TargetIdentifier::from_spelling(candidate);
    reserved_names.insert(candidate);
    local_names.emplace(ordinal, identifier);
    return identifier;
}

auto TargetNameAllocator::fresh(TargetTemporaryNameKind kind) noexcept -> TargetIdentifier {
    return claim(temporary_stem(kind));
}

auto TargetNameAllocator::fresh(TargetTemporaryNameKind kind, TargetScopeID scope) noexcept
    -> TargetIdentifier {
    return claim(temporary_stem(kind), canonical_scope(scope));
}

auto TargetNameAllocator::claim(std::string_view preferred) noexcept -> TargetIdentifier {
    auto candidate = std::string(preferred);
    auto& suffix = next_suffix.try_emplace(std::string(preferred), 2uz).first->second;
    while (is_reserved(candidate) || !claimed_names.insert(candidate).second) {
        candidate = std::format("{}_{}", preferred, suffix++);
    }
    return TargetIdentifier::from_spelling(candidate);
}

auto TargetNameAllocator::claim(std::string_view preferred, TargetScopeID scope) noexcept
    -> TargetIdentifier {
    auto candidate = std::string(preferred);
    auto& suffix = scoped_next_suffix[scope].try_emplace(std::string(preferred), 2uz).first->second;
    auto& claimed = local_claimed_names[scope];
    const auto& reserved = scoped_reserved_names[scope];
    while (reserved.contains(candidate) || !claimed.insert(candidate).second) {
        candidate = std::format("{}_{}", preferred, suffix++);
    }
    return TargetIdentifier::from_spelling(candidate);
}

auto public_target_identifier(std::string_view spelling) noexcept -> TargetIdentifier {
    if (TargetIdentifier::accepts_spelling(spelling)
        && !implementation_reserved(spelling)
        && !spelling.starts_with("cv_escaped_")) {
        return TargetIdentifier::from_spelling(spelling);
    }
    return TargetIdentifier::from_spelling(encoded_identifier(spelling, "cv_escaped_"));
}

auto generated_target_namespace() noexcept -> TargetName {
    return TargetName::from_components({
        TargetIdentifier::from_spelling("carven"),
        TargetIdentifier::from_spelling("generated"),
    });
}

auto linkage_target_namespace(const LinkageDomainID& linkage_domain) noexcept -> TargetName {
    return TargetName::from_components(
        {TargetIdentifier::from_spelling(linkage_domain.namespace_identifier())}
    );
}

auto claim_target_identifier(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(claim_spelling(preferred, "_", occupied));
}

auto claim_target_type_identifier(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(claim_spelling(preferred, "", occupied));
}

auto payload_enum_names(std::span<const TargetIdentifier> case_names) noexcept
    -> TargetPayloadEnumNames {
    auto occupied = std::flat_set<std::string> {};
    for (const auto& name : case_names) {
        occupied.insert(std::string(name.spelling()));
    }
    auto cases = std::vector<TargetPayloadEnumCaseNames> {};
    cases.reserve(case_names.size());
    for (const auto& name : case_names) {
        const auto record_type = derived_type_identifier(name, "Payload");
        const auto projection_base = derived_value_identifier("as", name);
        const auto projection_function = std::format("{}_if", projection_base.spelling());
        cases.push_back({
            .record_type = claim_target_type_identifier(record_type.spelling(), occupied),
            .projection_function = claim_target_identifier(projection_function, occupied),
        });
    }
    return {
        .cases = std::move(cases),
        .storage_type = claim_target_type_identifier("Storage", occupied),
        .storage_member = claim_target_identifier("storage", occupied),
    };
}

auto enum_payload_field_identifier(std::size_t payload_index) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(std::format("value_{}", payload_index));
}

auto process_entry_identifier() noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling("main");
}

auto process_argument_count_identifier() noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling("carven_argc");
}

auto process_argument_vector_identifier() noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling("carven_argv");
}

auto test_context_identifier() noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling("carven_test_context");
}

auto content_name_digest(std::string_view content) noexcept -> std::string {
    auto digest = 0xcbf29ce484222325ull;
    for (const auto byte : content) {
        digest = (digest ^ static_cast<unsigned char>(byte)) * 0x100000001b3ull;
    }
    return std::format("{:016x}", digest);
}

auto claim_content_identifiers(
    std::span<const TargetContentName> requests,
    std::flat_set<std::string>& occupied
) noexcept -> std::vector<TargetIdentifier> {
    auto order = std::vector<std::size_t>(requests.size());
    std::iota(order.begin(), order.end(), 0uz);
    std::ranges::sort(order, [&](std::size_t left, std::size_t right) noexcept {
        const auto& first = requests[left];
        const auto& second = requests[right];
        return std::tie(first.preferred, first.content)
            < std::tie(second.preferred, second.content);
    });
    auto names = std::vector<std::optional<TargetIdentifier>>(requests.size());
    auto previous = std::optional<std::size_t>();
    for (const auto index : order) {
        const auto& request = requests[index];
        if (previous
            && request.preferred == requests[*previous].preferred
            && request.content == requests[*previous].content) {
            names[index] = names[*previous];
        } else {
            names[index] = claim_target_identifier(request.preferred, occupied);
        }
        previous = index;
    }
    auto result = std::vector<TargetIdentifier>();
    result.reserve(names.size());
    for (auto& name : names) {
        result.push_back(std::move(*name));
    }
    return result;
}
