module carven:backend.lowering.decl.structure.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.item;
import :semantic.hir.decl;
import std;

auto lower_structure_declaration(TargetModuleLowerer& context, StructID structure_id) noexcept
    -> TargetItemValue {
    const auto& structure = context.source().structure(structure_id);
    auto members = std::vector<TargetRecordMember>();
    members.reserve(structure.fields.size() + 1);
    for (const auto& field : structure.fields) {
        members.push_back(
            TargetStructField {
                .name = context.name_allocator().source(
                    context.source().provenance().spelling(field.name),
                    context.source().provenance().spelling(structure.name)
                ),
                .type = lower_type(context, field.type),
            }
        );
    }
    if (context.source().nominal_capabilities(HIRNominalDeclRef {structure_id}).equality) {
        const auto structure_type =
            lower_type(context, *context.source().symbol(structure.symbol).type);
        members.push_back(
            TargetMemberFunctionDecl {
                .name = TargetOperatorName::Equality,
                .parameters =
                    {
                        {.name = std::nullopt,
                         .type = reference_type(context, structure_type, true)},
                        {.name = std::nullopt,
                         .type = reference_type(context, structure_type, true)},
                    },
                .result = intrinsic_type(context, TargetSymbol::Bool),
                .body = {},
                .static_specifier = false,
                .constexpr_specifier = true,
                .friend_specifier = true,
                .declaration_only = false,
                .defaulted = true,
                .result_reference = false,
                .const_qualified = false,
            }
        );
    }
    return TargetDecl {TargetStructDecl {
        .name = symbol_identifier(context, structure.symbol),
        .members = std::move(members),
    }};
}
