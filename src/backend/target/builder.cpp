module carven:backend.target.builder.impl;

import :backend.target.builder;
import :backend.target.dependencies;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.traversal;
import :backend.target.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

class TypeLookup final {
public:
    TypeLookup(TargetUnitIdentity identity, std::size_t count) noexcept;
    auto key(const TargetType& type) noexcept -> std::size_t;
    auto visit_type(TargetTypeID child) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;

private:
    auto mix(std::size_t value) noexcept -> void;
    auto name(const TargetName& value) noexcept -> void;

    TargetUnitIdentity identity;
    std::size_t count;
    std::size_t hash = 0uz;
};

TypeLookup::TypeLookup(TargetUnitIdentity identity, std::size_t count) noexcept
    : identity(identity),
      count(count) {}

auto TypeLookup::key(const TargetType& type) noexcept -> std::size_t {
    mix(type.value.index());
    mix(type.const_qualified);
    type.value.visit(
        Overloaded {
            [&](const TargetNamedType& value) noexcept {
                name(value.name);
                for (const auto& segment : value.nested) {
                    mix(std::hash<std::string_view>()(segment.name.spelling()));
                    mix(segment.type_argument_ids.size());
                }
            },
            [&](const TargetIntrinsicType& value) noexcept {
                mix(static_cast<std::size_t>(value.symbol));
            },
            [&](const TargetArrayType& value) noexcept { mix(value.extent.magnitude); },
            [&](const TargetReferenceType& value) noexcept {
                mix(value.const_qualified);
                mix(value.rvalue);
            },
            [](const TargetFunctionType&) static noexcept {},
            [](const TargetPointerType&) static noexcept {},
            [](const TargetDecltypeType&) static noexcept {},
        }
    );
    static_cast<void>(visit_target_type_children(type.value, *this));
    return hash;
}

auto TypeLookup::visit_type(TargetTypeID child) noexcept -> bool {
    if (child.owner() != identity || child.index() >= count) {
        invariant_violation("target type child is foreign or has not been constructed");
    }
    mix(child.index());
    return true;
}

auto TypeLookup::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    mix(expression.value.index());
    expression.value.visit([&](const auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, TargetNameExpr>) {
            name(value.name);
        } else if constexpr (std::same_as<Value, TargetIntrinsicNameExpr>) {
            mix(static_cast<std::size_t>(value.symbol));
        } else if constexpr (std::same_as<Value, TargetMemberExpr>) {
            const auto* member = std::get_if<TargetIdentifier>(&value.name);
            if (member != nullptr) {
                mix(std::hash<std::string_view>()(member->spelling()));
            }
        } else if constexpr (std::same_as<Value, TargetPrefixExpr>
                             || std::same_as<Value, TargetBinaryExpr>) {
            mix(static_cast<std::size_t>(value.op));
        }
    });
    return true;
}

auto TypeLookup::mix(std::size_t value) noexcept -> void {
    hash ^= value + 0x9e3779b9uz + (hash << 6u) + (hash >> 2u);
}

auto TypeLookup::name(const TargetName& value) noexcept -> void {
    mix(value.is_globally_qualified());
    for (const auto& component : value.components()) {
        mix(std::hash<std::string_view>()(component.spelling()));
    }
}

struct NamespaceTypeDefinition final {
    TargetIdentifier name;
    TargetTypeID type;
};

class NamespaceTypePlacement final {
public:
    NamespaceTypePlacement(
        TargetUnitIdentity identity,
        std::vector<TargetType>& types,
        const std::map<TargetTypeID, NamespaceTypeDefinition>& definitions
    ) noexcept;
    auto collect(std::span<const TargetItem> items) noexcept -> void;
    auto place(std::vector<TargetItem>& items) noexcept -> void;
    auto visit_type(TargetTypeID id) noexcept -> bool;

private:
    TargetUnitIdentity identity;
    std::vector<TargetType>& types;
    const std::map<TargetTypeID, NamespaceTypeDefinition>& definitions;
    std::set<TargetTypeID> visited;
    using Scope = std::vector<std::string>;
    auto enter(const std::optional<TargetName>& name) noexcept -> void;
    auto claim(const TargetIdentifier& preferred) noexcept -> TargetIdentifier;
    Scope scope;
    std::map<Scope, std::set<std::string>> occupied;
    std::vector<TargetItem> pending;
};

NamespaceTypePlacement::NamespaceTypePlacement(
    TargetUnitIdentity identity,
    std::vector<TargetType>& types,
    const std::map<TargetTypeID, NamespaceTypeDefinition>& definitions
) noexcept
    : identity(identity),
      types(types),
      definitions(definitions) {}

auto NamespaceTypePlacement::enter(const std::optional<TargetName>& name) noexcept -> void {
    if (!name) {
        scope.emplace_back();
        return;
    }
    if (name->is_globally_qualified()) {
        scope.clear();
    }
    for (const auto& part : name->components()) {
        occupied[scope].insert(std::string(part.spelling()));
        scope.emplace_back(part.spelling());
    }
}

auto NamespaceTypePlacement::collect(std::span<const TargetItem> items) noexcept -> void {
    for (const auto& item : items) {
        if (const auto* space = std::get_if<TargetNamespace>(&item.value)) {
            const auto outer = scope;
            enter(space->name);
            collect(space->items);
            scope = outer;
        } else if (const auto* declaration = std::get_if<TargetDecl>(&item.value)) {
            declaration->visit([&](const auto& value) noexcept {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TargetFunctionDecl>) {
                    auto owner = value.name.is_globally_qualified() ? Scope() : scope;
                    const auto parts = value.name.components();
                    for (const auto& part : parts.first(parts.size() - 1uz)) {
                        owner.emplace_back(part.spelling());
                    }
                    occupied[owner].insert(std::string(parts.back().spelling()));
                } else if constexpr (!std::same_as<Value, TargetOutOfClassMemberDefinition>) {
                    occupied[scope].insert(std::string(value.name.spelling()));
                }
            });
        } else if (const auto* using_name = std::get_if<TargetUsing>(&item.value);
                   using_name != nullptr && !using_name->opens_namespace) {
            occupied[scope].insert(std::string(using_name->name.components().back().spelling()));
        }
    }
}

auto NamespaceTypePlacement::claim(const TargetIdentifier& preferred) noexcept -> TargetIdentifier {
    auto lookup = scope;
    if (!lookup.empty() && lookup.back().empty()) {
        lookup.pop_back();
    }
    auto anonymous = lookup;
    anonymous.emplace_back();
    const auto conflicts = [&](const std::string& name) noexcept {
        return occupied[scope].contains(name)
            || occupied[lookup].contains(name)
            || occupied[anonymous].contains(name);
    };
    auto candidate = std::string(preferred.spelling());
    auto suffix = 2uz;
    while (conflicts(candidate)) {
        candidate = std::format("{}_{}", preferred.spelling(), suffix++);
    }
    occupied[scope].insert(candidate);
    return TargetIdentifier::from_spelling(candidate);
}

auto NamespaceTypePlacement::visit_type(TargetTypeID id) noexcept -> bool {
    if (id.owner() != identity || id.index() >= types.size()) {
        invariant_violation("namespace type placement received a foreign or invalid type");
    }
    if (!visited.insert(id).second) {
        return true;
    }
    const auto found = definitions.find(id);
    if (found == definitions.end()) {
        return visit_target_type_children(types[id.index()].value, *this);
    }
    const auto& definition = found->second;
    static_cast<void>(visit_target_type_children(types[definition.type.index()].value, *this));
    const auto name = claim(definition.name);
    auto components = std::vector<TargetIdentifier>();
    for (const auto& part : scope) {
        if (!part.empty()) {
            components.push_back(TargetIdentifier::from_spelling(part));
        }
    }
    components.push_back(name);
    types[id.index()] = {
        .value =
            TargetNamedType {
                .name = TargetName::globally_qualified(std::move(components)),
                .type_argument_ids = {},
                .nested = {},
            },
        .const_qualified = false,
    };
    pending.push_back(target_lowering_item(
        TargetDecl {TargetTypeAlias {
            .name = name,
            .type = definition.type,
        }}
    ));
    return true;
}

auto NamespaceTypePlacement::place(std::vector<TargetItem>& items) noexcept -> void {
    auto placed = std::vector<TargetItem>();
    for (auto& item : items) {
        if (auto* space = std::get_if<TargetNamespace>(&item.value)) {
            const auto outer = scope;
            enter(space->name);
            place(space->items);
            scope = outer;
        } else {
            static_cast<void>(traverse_target_item(item, *this));
            placed.insert(
                placed.end(),
                std::make_move_iterator(pending.begin()),
                std::make_move_iterator(pending.end())
            );
            pending.clear();
        }
        placed.push_back(std::move(item));
    }
    items = std::move(placed);
}

} // namespace

TargetUnitBuilder::TargetUnitBuilder() noexcept
    : unit_identity(TargetUnitIdentity::fresh()) {}

auto TargetUnitBuilder::identity() const noexcept -> TargetUnitIdentity {
    return unit_identity;
}

auto TargetUnitBuilder::intern_type(TargetType type) noexcept -> TargetTypeID {
    const auto identity = unit_identity;

    auto lookup = TypeLookup(identity, types.size());
    const auto key = lookup.key(type);
    // The key selects candidates; structural equality remains authoritative.
    const auto [begin, end] = type_candidates.equal_range(key);
    for (auto candidate = begin; candidate != end; ++candidate) {
        if (types[candidate->second] == type) {
            return TargetTypeID(identity, candidate->second);
        }
    }
    if (types.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("target unit type table exhausted its 32-bit identity space");
    }
    const auto id = TargetTypeID(identity, static_cast<std::uint32_t>(types.size()));
    type_candidates.emplace(key, id.index());
    types.push_back(std::move(type));
    return id;
}

auto TargetUnitBuilder::name_namespace_type(
    TargetTypeID type,
    const TargetIdentifier& name
) noexcept -> void {
    if (type.owner() != unit_identity || type.index() >= types.size()) {
        invariant_violation("namespace type naming received a foreign or invalid type");
    }
    namespace_types.try_emplace(type, name);
}

auto target_lowering_statement(TargetStmtValue value) noexcept -> TargetStmt {
    return {
        .value = std::move(value),
        .attribution = TargetGeneratedExpansionAttribution {
            .reason = TargetExpansionReason::LoweringSupport,
        },
    };
}

auto target_lowering_item(TargetItemValue value) noexcept -> TargetItem {
    return {
        .value = std::move(value),
        .attribution = TargetGeneratedExpansionAttribution {
            .reason = TargetExpansionReason::LoweringSupport,
        },
    };
}

auto TargetUnitBuilder::finish(
    TargetUnitSections sections,
    TargetDirectiveInputs directives
) && noexcept -> TargetUnit {
    const auto identity = unit_identity;
    if (!namespace_types.empty()) {
        if (namespace_types.size() > std::numeric_limits<std::uint32_t>::max() - types.size()) {
            resource_limit_exceeded("target unit type table exhausted its 32-bit identity space");
        }
        types.reserve(types.size() + namespace_types.size());
        auto definitions = std::map<TargetTypeID, NamespaceTypeDefinition>();
        for (const auto& [id, name] : namespace_types) {
            const auto definition =
                TargetTypeID(identity, static_cast<std::uint32_t>(types.size()));
            types.push_back(std::move(types[id.index()]));
            types[id.index()] = {
                .value =
                    TargetNamedType {
                        .name = TargetName(name),
                        .type_argument_ids = {},
                        .nested = {}
                    },
                .const_qualified = false,
            };
            definitions.emplace(id, NamespaceTypeDefinition {.name = name, .type = definition});
        }
        auto placement = NamespaceTypePlacement(identity, types, definitions);
        placement.collect(sections.preamble);
        placement.collect(sections.body);
        placement.collect(sections.epilogue);
        placement.place(sections.preamble);
        placement.place(sections.body);
        placement.place(sections.epilogue);
    }
    auto target_types = std::move(types);
    const auto validation = validate_target_unit(
        TargetVerificationInput(identity, target_types, sections, locals.size())
    );
    if (!validation.has_value()) {
        invariant_violation(validation.error().message);
    }

    auto directive_groups = std::move(directives.prefix_groups);
    auto dependencies = collect_target_dependencies(identity, target_types, sections);
    if (!dependencies.empty()) {
        directive_groups.push_back({
            .directives = std::move(dependencies),
            .attribution = TargetCompilerOwnedAttribution {
                .reason = TargetCompilerReason::ArtifactScaffolding,
            },
        });
    }
    directive_groups.insert(
        directive_groups.end(),
        std::make_move_iterator(directives.suffix_groups.begin()),
        std::make_move_iterator(directives.suffix_groups.end())
    );
    return TargetUnit(
        identity,
        std::move(target_types),
        std::move(locals),
        TargetUnitContents {
            .directive_groups = std::move(directive_groups),
            .sections = std::move(sections),
        }
    );
}

auto TargetUnitBuilder::local_name(TargetLocalID id) const noexcept -> const TargetIdentifier& {
    if (id.owner() != unit_identity || id.index() >= locals.size()) {
        invariant_violation("target local lookup used a foreign or invalid identity");
    }
    return locals[id.index()];
}

auto TargetUnitBuilder::add_local(TargetIdentifier name) noexcept -> TargetLocalID {
    if (locals.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("target unit local table exhausted its 32-bit identity space");
    }
    const auto id = TargetLocalID(unit_identity, static_cast<std::uint32_t>(locals.size()));
    locals.push_back(std::move(name));
    return id;
}
