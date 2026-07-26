module carven:backend.lowering.names.symbol.impl;

import :backend.lowering.program;
import :backend.generation.names;
import :backend.lowering.names;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.symbol;
import :support.invariant;
import std;

auto symbol_identifier(TargetModuleLowerer& context, SymbolID symbol) noexcept -> TargetIdentifier {
    const auto& value = context.semantic().symbol(symbol);
    const auto source_name = context.semantic().provenance().spelling(value.name);
    const auto enclosing = value.parent.has_value()
        ? context.semantic().provenance().spelling(context.semantic().symbol(*value.parent).name)
        : std::string_view();
    if (!value.module_id.has_value()) {
        const auto scope = value.place.has_value()
            ? context.name_scope(context.semantic().place(*value.place).scope)
            : TargetScopeID {.ordinal = 0u};
        return context.name_allocator().local_symbol(source_name, symbol.index(), scope, enclosing);
    }
    return context.entity_identifier(symbol);
}

auto binding_identifier(
    TargetModuleLowerer& context,
    SymbolID symbol,
    const EvaluationEffect& initializer_effect
) noexcept -> TargetIdentifier {
    const auto& value = context.semantic().symbol(symbol);
    if (value.module_id.has_value()) {
        return symbol_identifier(context, symbol);
    }
    auto avoided = std::vector<TargetIdentifier> {};
    const auto collect_uses = [&](std::span<const SemanticPlaceID> places) noexcept {
        for (const auto place : places) {
            const auto used_symbol = context.semantic().place(place).symbol;
            if (!used_symbol.has_value() || *used_symbol == symbol) {
                continue;
            }
            const auto identifier = symbol_identifier(context, *used_symbol);
            if (!std::ranges::contains(
                    avoided,
                    identifier.spelling(),
                    &TargetIdentifier::spelling
                )) {
                avoided.push_back(identifier);
            }
        }
    };
    collect_uses(initializer_effect.reads);
    collect_uses(initializer_effect.writes);
    collect_uses(initializer_effect.takes);
    const auto source_name = context.semantic().provenance().spelling(value.name);
    const auto enclosing = value.parent.has_value()
        ? context.semantic().provenance().spelling(context.semantic().symbol(*value.parent).name)
        : std::string_view();
    const auto scope = value.place.has_value()
        ? context.name_scope(context.semantic().place(*value.place).scope)
        : TargetScopeID {.ordinal = 0u};
    return context.name_allocator()
        .local_symbol(source_name, symbol.index(), scope, enclosing, avoided);
}

auto symbol_is_used(const TargetModuleLowerer& context, SymbolID symbol) noexcept -> bool {
    return context.semantic().symbol(symbol).referenced;
}

auto symbol_reference_name(TargetModuleLowerer& context, SymbolID symbol) noexcept -> TargetName {
    return symbol_name(context, symbol);
}

auto symbol_name(TargetModuleLowerer& context, SymbolID symbol) noexcept -> TargetName {
    const auto& value = context.semantic().symbol(symbol);
    if (value.module_id.has_value()) {
        return context.entity_name(symbol);
    }
    auto components = value.parent.has_value()
        ? std::vector<
              TargetIdentifier> {symbol_identifier(context, *value.parent), symbol_identifier(context, symbol)}
        : std::vector<TargetIdentifier> {symbol_identifier(context, symbol)};
    return TargetName::from_components(std::move(components));
}

auto member_name(TargetModuleLowerer& context, const HIRMemberExpr& member) noexcept
    -> TargetMemberName {
    if (const auto* unresolved = std::get_if<HIRUnresolvedMemberTarget>(&member.target)) {
        return TargetRawIdentifier {
            .spelling = std::string(context.semantic().provenance().spelling(unresolved->name)),
        };
    }
    const auto* enumeration = std::get_if<HIREnumCaseTarget>(&member.target);
    if (const auto* field = std::get_if<HIRStructFieldTarget>(&member.target)) {
        const auto& structure = context.semantic().structure(field->owner);
        if (field->index >= structure.fields.size()) {
            invariant_violation("resolved structure field index is invalid");
        }
        return context.name_allocator().source(
            context.semantic().provenance().spelling(structure.fields[field->index].name),
            context.semantic().provenance().spelling(structure.name)
        );
    }
    if (enumeration != nullptr) {
        return symbol_identifier(
            context,
            context.semantic().enum_case(enumeration->enum_case).symbol
        );
    }
    std::unreachable();
}
