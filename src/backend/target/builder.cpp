module carven:backend.target.builder.impl;

import :backend.target.builder;
import :backend.target.dependencies;
import :backend.target.traversal;
import :support.invariant;
import std;

TargetUnitBuilder::TargetUnitBuilder() noexcept
    : unit_identity(TargetUnitIdentity::fresh()) {}

TargetUnitBuilder::TargetUnitBuilder(TargetUnitBuilder&& other) noexcept
    : unit_identity(std::exchange(other.unit_identity, std::nullopt)),
      types(std::move(other.types)) {}

auto TargetUnitBuilder::require_identity() const noexcept -> TargetUnitIdentity {
    if (!unit_identity.has_value()) {
        invariant_violation("target unit builder was used after move or finish");
    }
    return *unit_identity;
}

auto TargetUnitBuilder::identity() const noexcept -> TargetUnitIdentity {
    return require_identity();
}

auto TargetUnitBuilder::intern_type(TargetType type) noexcept -> TargetTypeID {
    const auto identity = require_identity();

    struct Children final {
        TargetUnitIdentity identity;
        std::size_t count;

        auto visit_type(TargetTypeID child) const noexcept -> bool {
            if (child.owner() != identity || child.index() >= count) {
                invariant_violation("target type child is foreign or has not been constructed");
            }
            return true;
        }
    };

    auto children = Children {.identity = identity, .count = types.size()};
    static_cast<void>(visit_target_type_children(type.value, children));
    for (auto index = 0uz; index < types.size(); ++index) {
        if (types[index] == type) {
            return TargetTypeID(identity, static_cast<std::uint32_t>(index));
        }
    }
    if (types.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("target unit type table exhausted its 32-bit identity space");
    }
    const auto id = TargetTypeID(identity, static_cast<std::uint32_t>(types.size()));
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
    const auto identity = require_identity();
    unit_identity.reset();
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
