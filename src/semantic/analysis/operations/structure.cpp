module carven:semantic.analysis.operations.structure.impl;

import :diagnostics.builder;
import :semantic.analysis.operations;
import :support.invariant;
import std;

auto select_structure_initializers(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTConstructionInitializer& source,
    std::span<const ConstructionStructField> fields
) noexcept -> AnalysisResult<std::vector<StructureInitializer>> {
    const auto syntax = draft.syntax_tree(module_id).view();
    const auto fail = [&](Span span, DiagnosticCode code, std::string message) noexcept {
        return std::unexpected(
            draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                          .primary(locate(syntax.source_id(), span))
                                          .build())
        );
    };
    auto result = std::vector<StructureInitializer>();
    if (const auto* positional = std::get_if<ASTPositionalInitializerList>(&source.value)) {
        if (positional->values.size() != fields.size()) {
            return fail(
                positional->span,
                DiagnosticCode::TypeConstructArity,
                "positional initializer count differs from structure fields"
            );
        }
        for (auto index = 0uz; index < positional->values.size(); ++index) {
            result.push_back(
                {.declaration_index = static_cast<std::uint32_t>(index),
                 .expression = positional->values[index]}
            );
        }
        return result;
    }
    if (const auto* named = std::get_if<ASTFieldInitializerList>(&source.value)) {
        auto initialized = std::vector<bool>(fields.size());
        for (const auto& field : named->fields) {
            const auto name = draft.source_slice_copy(module_id, field.name_span);
            const auto found = std::ranges::find_if(fields, [&](const auto& candidate) noexcept {
                return draft.spelling_copy(candidate.name) == name;
            });
            if (found == fields.end()) {
                return fail(
                    field.name_span,
                    DiagnosticCode::TypeConstructUnknownField,
                    std::format("structure has no field named '{}'", name)
                );
            }
            const auto index = static_cast<std::size_t>(found - fields.begin());
            if (initialized[index]) {
                return fail(
                    field.name_span,
                    DiagnosticCode::TypeConstructDuplicateField,
                    std::format("field '{}' is initialized more than once", name)
                );
            }
            initialized[index] = true;
            result.push_back(
                {.declaration_index = static_cast<std::uint32_t>(index), .expression = field.value}
            );
        }
        for (auto index = 0uz; index < fields.size(); ++index) {
            if (!initialized[index]) {
                return fail(
                    named->span,
                    DiagnosticCode::TypeConstructArity,
                    std::format(
                        "missing initializer for field '{}'",
                        draft.spelling_copy(fields[index].name)
                    )
                );
            }
        }
        return result;
    }
    invariant_violation("structure initializer selection requires a nonempty construction");
}

auto default_initialization(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> DefaultInitialization {
    return query_default_initialization(
        type,
        [&](ConstructionTypeRef reference) noexcept -> InitializationType {
            if (const auto* concrete = std::get_if<TypeID>(&reference)) {
                return draft.type_copy(*concrete);
            }
            return draft.construction_type_copy(std::get<TypeTermID>(reference));
        },
        [&](StructID structure) noexcept {
            return draft.construction_struct_declaration_copy(structure);
        }
    );
}
