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
    std::visit(
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
        },
        type.value
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
    std::visit(
        [&](const auto& value) noexcept {
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
        },
        expression.value
    );
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
    auto target_types = std::move(types);
    const auto validation =
        validate_target_unit(TargetVerificationInput(identity, target_types, sections));
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
        TargetUnitContents {
            .directive_groups = std::move(directive_groups),
            .sections = std::move(sections),
        }
    );
}
