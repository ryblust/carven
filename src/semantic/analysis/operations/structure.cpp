module carven:semantic.analysis.operations.structure.impl;

import :diagnostics.builder;
import :semantic.analysis.operations;
import std;

auto select_structure_initializers(
    const ProgramDraft& draft,
    ProgramModuleID module,
    const ASTConstructionExpr& source,
    std::span<const ConstructionStructField> fields
) noexcept -> AnalysisResult<std::vector<StructureInitializer>> {
    const auto syntax = draft.syntax_tree(module).view();
    const auto fail = [&](Span span, DiagnosticCode code, std::string message) noexcept {
        return std::unexpected(
            draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                          .primary(locate(syntax.source_id(), span))
                                          .build())
        );
    };
    auto result = std::vector<StructureInitializer>();
    if (const auto* positional =
            std::get_if<ASTPositionalInitializerList>(&source.initializer.value)) {
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
    if (const auto* named = std::get_if<ASTFieldInitializerList>(&source.initializer.value)) {
        auto initialized = std::vector<bool>(fields.size());
        for (const auto& field : named->fields) {
            const auto name = draft.source_slice_copy(module, field.name_span);
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
        if (result.size() != fields.size()) {
            return fail(
                named->span,
                DiagnosticCode::TypeConstructArity,
                "named initializer omits one or more fields"
            );
        }
        return result;
    }
    if (!fields.empty()) {
        return fail(
            source.type.span,
            DiagnosticCode::TypeConstructArity,
            "structure initializer omits required fields"
        );
    }
    return result;
}
