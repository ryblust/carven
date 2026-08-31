module carven:backend.lowering.decl.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.target.item;
import :semantic.hir.decl;
import :support.visit;
import std;

auto lower_declaration(
    TargetModuleLowerer& context,
    HIRDeclarationRef declaration,
    bool declaration_only
) noexcept -> TargetItemID {
    const auto origin = std::visit(
        Overloaded {
            [&](FunctionID id) noexcept { return context.source().function(id).origin; },
            [&](StructID id) noexcept { return context.source().structure(id).origin; },
            [&](EnumID id) noexcept { return context.source().enumeration(id).origin; },
        },
        declaration
    );
    auto target = std::visit(
        Overloaded {
            [&](FunctionID id) noexcept {
                return lower_function_declaration(context, id, declaration_only);
            },
            [&](StructID id) noexcept { return lower_structure_declaration(context, id); },
            [&](EnumID id) noexcept { return lower_enumeration_declaration(context, id); },
        },
        declaration
    );
    return context.target().append_item({
        .value = std::move(target),
        .attribution = {
            .kind = TargetAttributionKind::SourceOwned,
            .origin = target_source_origin(context, origin),
            .reason = std::nullopt,
        },
    });
}
