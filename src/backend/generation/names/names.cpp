module carven:backend.generation.names.impl;

import :backend.generation.names;
import std;

namespace {

auto implementation_reserved(std::string_view spelling) noexcept -> bool {
    return spelling.contains("__")
        || (spelling.size() >= 2 && spelling[0] == '_' && spelling[1] >= 'A' && spelling[1] <= 'Z');
}

auto encoded_identifier(std::string_view spelling) noexcept -> std::string {
    static constexpr auto digits = std::string_view("0123456789abcdef");
    auto escaped = std::string("cv_name_");
    escaped.reserve(escaped.size() + spelling.size() * 2);
    for (const auto byte : spelling) {
        const auto value = static_cast<unsigned char>(byte);
        escaped += digits[value >> 4u];
        escaped += digits[value & 0x0fu];
    }
    return escaped;
}

auto source_identifier(std::string_view spelling, std::string_view enclosing_class) noexcept
    -> std::string {
    if (TargetIdentifier::accepts_spelling(spelling)
        && !implementation_reserved(spelling)
        && spelling != enclosing_class) {
        return std::string(spelling);
    }
    if (!spelling.empty() && !implementation_reserved(spelling)) {
        const auto suffixed = std::format("{}_cv", spelling);
        if (TargetIdentifier::accepts_spelling(suffixed)) {
            return suffixed;
        }
    }
    auto readable = std::string(spelling);
    readable.erase(readable.begin(), std::ranges::find_if(readable, [](char value) static noexcept {
                       return value != '_';
                   }));
    if (readable.empty()) {
        readable = "name";
    }
    readable += "_cv";
    return TargetIdentifier::accepts_spelling(readable) ? readable : encoded_identifier(spelling);
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
        case TargetTemporaryNameKind::Region:               return "region";
        case TargetTemporaryNameKind::Continue:             return "continue_target";
        case TargetTemporaryNameKind::TestValue:            return "test_value";
        case TargetTemporaryNameKind::TestRegistration:     return "test_registration";
        case TargetTemporaryNameKind::CppBoundaryParameter: return "cpp_boundary_parameter";
        case TargetTemporaryNameKind::CppProviderPointer:   return "cpp_provider_pointer";
    }
    std::unreachable();
}

} // namespace

auto TargetNameAllocator::source(
    std::string_view spelling,
    std::string_view enclosing_class
) const noexcept -> TargetIdentifier {
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
    const auto preferred = std::string(source(spelling, enclosing_class).spelling());
    auto candidate = preferred;
    auto suffix = 2uz;
    auto& scope_names = local_claimed_names[scope];
    const auto conflicts_with_initializer = [&](std::string_view value) noexcept {
        return std::ranges::any_of(avoided, [&](const TargetIdentifier& identifier) noexcept {
            return identifier.spelling() == value;
        });
    };
    while (conflicts_with_initializer(candidate) || !scope_names.insert(candidate).second) {
        candidate = std::format("{}_{}", preferred, suffix++);
    }
    auto identifier = TargetIdentifier::from_spelling(candidate);
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
    auto suffix = 2uz;
    while (reserved_names.contains(candidate) || !claimed_names.insert(candidate).second) {
        candidate = std::format("{}_{}", preferred, suffix++);
    }
    return TargetIdentifier::from_spelling(candidate);
}

auto TargetNameAllocator::claim(std::string_view preferred, TargetScopeID scope) noexcept
    -> TargetIdentifier {
    auto candidate = std::string(preferred);
    auto suffix = 2uz;
    auto& claimed = local_claimed_names[scope];
    const auto& reserved = scoped_reserved_names[scope];
    while (reserved.contains(candidate) || !claimed.insert(candidate).second) {
        candidate = std::format("{}_{}", preferred, suffix++);
    }
    return TargetIdentifier::from_spelling(candidate);
}

auto TargetNameAllocator::fixed(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto TargetNameAllocator::generated_namespace() noexcept -> TargetName {
    return TargetName::from_components({
        fixed("carven"),
        fixed("generated"),
    });
}

auto TargetNameAllocator::domain_namespace(const LinkageDomainID& linkage_domain) noexcept
    -> TargetName {
    return TargetName::from_components({fixed(linkage_domain.namespace_identifier())});
}

auto TargetNameAllocator::derived_type(
    const TargetIdentifier& source_name,
    std::string_view role
) noexcept -> TargetIdentifier {
    return fixed(std::format("{}{}", upper_camel_spelling(source_name.spelling()), role));
}

auto TargetNameAllocator::derived_value(
    std::string_view role,
    const TargetIdentifier& source_name
) noexcept -> TargetIdentifier {
    return fixed(std::format("{}_{}", role, lower_snake_spelling(source_name.spelling())));
}

auto TargetNameAllocator::claim_source(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier {
    return fixed(claim_spelling(preferred, "_", occupied));
}

auto TargetNameAllocator::claim_type(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier {
    return fixed(claim_spelling(preferred, "", occupied));
}

auto TargetNameAllocator::claim_value(
    std::string_view preferred,
    std::flat_set<std::string>& occupied
) noexcept -> TargetIdentifier {
    return fixed(claim_spelling(preferred, "_", occupied));
}

auto payload_enum_names(
    std::span<const TargetIdentifier> case_names,
    const TargetIdentifier& enum_name
) noexcept -> TargetPayloadEnumNames {
    auto occupied = std::flat_set<std::string> {};
    for (const auto& name : case_names) {
        occupied.insert(std::string(name.spelling()));
    }
    auto cases = std::vector<TargetPayloadEnumCaseNames> {};
    cases.reserve(case_names.size());
    for (const auto& name : case_names) {
        const auto record_type = TargetNameAllocator::derived_type(name, "Payload");
        const auto holds_function = TargetNameAllocator::derived_value("is", name);
        const auto payload_function = TargetNameAllocator::derived_value("as", name);
        cases.push_back({
            .record_type = TargetNameAllocator::claim_type(record_type.spelling(), occupied),
            .holds_function = TargetNameAllocator::claim_value(holds_function.spelling(), occupied),
            .payload_function =
                TargetNameAllocator::claim_value(payload_function.spelling(), occupied),
        });
    }
    auto parameter_occupied = std::flat_set<std::string> {std::string(enum_name.spelling())};
    return {
        .cases = std::move(cases),
        .storage_type = TargetNameAllocator::claim_type("Storage", occupied),
        .storage_member = TargetNameAllocator::claim_value("storage", occupied),
        .storage_parameter = TargetNameAllocator::claim_type("StorageValue", parameter_occupied),
    };
}

auto TargetNameAllocator::enum_payload_field(std::size_t payload_index) noexcept
    -> TargetIdentifier {
    return fixed(std::format("value_{}", payload_index));
}

auto TargetNameAllocator::process_entry() noexcept -> TargetIdentifier {
    return fixed("main");
}

auto TargetNameAllocator::process_argument_count() noexcept -> TargetIdentifier {
    return fixed("carven_argc");
}

auto TargetNameAllocator::process_argument_vector() noexcept -> TargetIdentifier {
    return fixed("carven_argv");
}
