module carven:backend.generation.plan.representation.impl;

import :backend.generation.plan.construction;
import :semantic.hir.decl;
import :semantic.hir.place;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto read_parameter_by_value(const SemanticProgram& semantic, const HIRType& type) noexcept
    -> bool {
    return std::visit(
        Overloaded {
            [](const HIRBuiltinTypeValue&) static noexcept { return true; },
            [](const HIRStructTypeValue&) static noexcept { return false; },
            [&](const HIREnumTypeValue& nominal) noexcept {
                return semantic.enumeration(nominal.enumeration).profile == HIREnumProfile::Numeric;
            },
            [](const HIRArrayTypeValue&) static noexcept { return false; },
            [](const HIRFunctionTypeValue&) static noexcept { return false; },
            [](const HIRFunctionRefTypeValue&) static noexcept { return false; },
            [](const HIRClosureTypeValue&) static noexcept { return false; },
            [](const HIRForeignTypeValue&) static noexcept { return false; },
            [](const HIRErrorTypeValue&) static noexcept { return false; },
        },
        type.value
    );
}

auto failure_order_key(const SemanticProgram& semantic, HIRTypeID id) noexcept
    -> std::tuple<std::string, std::string, std::uint8_t> {
    const auto& type = semantic.type(id).value;
    auto nominal_symbol = std::optional<SymbolID>();
    auto nominal_kind = std::uint8_t();
    if (const auto* structure = std::get_if<HIRStructTypeValue>(&type)) {
        nominal_symbol = semantic.structure(structure->structure).symbol;
        nominal_kind = 0u;
    } else if (const auto* enumeration = std::get_if<HIREnumTypeValue>(&type)) {
        nominal_symbol = semantic.enumeration(enumeration->enumeration).symbol;
        nominal_kind = 1u;
    }
    if (!nominal_symbol.has_value()) {
        invariant_violation("target failure set contains a non-nominal type");
    }
    const auto& symbol = semantic.symbol(*nominal_symbol);
    if (!symbol.module_id.has_value()) {
        invariant_violation("target failure nominal has no owning module");
    }
    auto names = std::vector<std::string_view>();
    auto current = std::optional {*nominal_symbol};
    while (current.has_value()) {
        const auto& current_symbol = semantic.symbol(*current);
        names.push_back(semantic.provenance().spelling(current_symbol.name));
        current = current_symbol.parent;
    }
    std::ranges::reverse(names);
    auto qualified_name = std::string();
    for (const auto name : names) {
        if (!qualified_name.empty()) {
            qualified_name += "::";
        }
        qualified_name += name;
    }
    return {
        std::string(semantic.provenance().module_record(*symbol.module_id).path.value()),
        std::move(qualified_name),
        nominal_kind,
    };
}

} // namespace

auto TargetPlanConstruction::derive_representations() noexcept -> void {
    read_parameter_by_value_flags =
        semantic.types() | std::views::transform([&](const HIRType& type) noexcept {
            return static_cast<std::uint8_t>(read_parameter_by_value(semantic, type));
        })
        | std::ranges::to<std::vector>();

    failure_sets.reserve(semantic.failure_sets().size());
    for (const auto& semantic_set : semantic.failure_sets()) {
        auto members = semantic_set.members;
        std::ranges::sort(members, [&](HIRTypeID left, HIRTypeID right) noexcept {
            return failure_order_key(semantic, left) < failure_order_key(semantic, right);
        });
        for (auto index = 1uz; index < members.size(); ++index) {
            if (failure_order_key(semantic, members[index - 1])
                == failure_order_key(semantic, members[index])) {
                invariant_violation(
                    "distinct failure nominals have the same stable target identity"
                );
            }
        }
        failure_sets.push_back({.ordered_members = std::move(members)});
    }
}

auto TargetPlanConstruction::derive_value_binding_requirements() noexcept -> void {
    mutable_value_binding_flags.resize(semantic.places().size(), 0u);
    for (auto index = 0uz; index < semantic.expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& use = semantic.expression_facts(id).place_use;
        if (use.has_value() && use->access != SemanticPlaceAccess::Read) {
            mutable_value_binding_flags[use->root.index()] = 1u;
        }
    }
}
