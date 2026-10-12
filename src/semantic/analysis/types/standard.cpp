module carven:semantic.analysis.types.standard.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.types.standard;
import :semantic.semir.decl;
import :semantic.visibility;
import :source.module_path;
import :source.standard;
import std;

namespace {

auto reject(
    ProgramDraft& draft,
    ProgramModuleID module,
    Span span,
    DiagnosticCode code,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(
        DiagnosticBuilder(code, std::move(message))
            .primary(locate(draft.syntax_tree(module).view().source_id(), span))
            .build()
    );
}

auto matches(const ProgramDraft& draft, const CatalogSymbol& symbol, StandardRecord record) noexcept
    -> bool {
    return symbol.name == standard_record_key(record).name
        && draft.module_path_copy(symbol.module_id).value() == standard_record_key(record).module;
}

auto complete_record(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    ProgramModuleID module,
    Span span,
    StandardRecord record,
    const CatalogSymbol& symbol
) noexcept -> AnalysisTask<TypeID> {
    const auto* structure = std::get_if<CatalogStructForm>(&symbol.form);
    if (!structure || symbol.visibility != DeclarationVisibility::Compilation) {
        co_return std::unexpected(reject(
            draft,
            module,
            span,
            DiagnosticCode::TypeMismatch,
            std::format(
                "standard '{}' must be an exported struct",
                standard_record_key(record).name
            )
        ));
    }
    auto completed = co_await requests.ensure_declaration(symbol.symbol_id, module, span);
    if (!completed) {
        co_return std::unexpected(completed.error());
    }
    const auto declaration = draft.construction_struct_declaration_copy(structure->structure);
    const auto valid = record == StandardRecord::Duration ? declaration.fields.size() == 1uz
            && draft.spelling(declaration.fields[0].name) == "nanoseconds"
            && declaration.fields[0].type
                == ConstructionTypeRef(draft.builtin_type(BuiltinType::I64))
                                                          : declaration.fields.empty();
    if (declaration.kind != RecordKind::Struct || !valid) {
        co_return std::unexpected(reject(
            draft,
            module,
            span,
            DiagnosticCode::TypeMismatch,
            std::format(
                "standard '{}' has an incompatible representation",
                standard_record_key(record).name
            )
        ));
    }
    co_return draft.intern_type({.value = StructTypeValue {structure->structure}});
}

} // namespace

auto resolve_standard_record(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ConstructionRequests& requests,
    ProgramModuleID module,
    Span span,
    StandardRecord record
) noexcept -> AnalysisTask<TypeID> {
    for (const auto& symbol : catalog.symbols()) {
        if (!matches(draft, symbol, record)) {
            continue;
        }
        co_return co_await complete_record(draft, requests, module, span, record, symbol);
    }
    co_return std::unexpected(reject(
        draft,
        module,
        span,
        DiagnosticCode::TypeUnresolved,
        std::format("required standard type '{}' is unavailable", standard_record_key(record).name)
    ));
}
