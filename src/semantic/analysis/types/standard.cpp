module carven:semantic.analysis.types.standard.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.types.standard;
import :semantic.semir.decl;
import :semantic.visibility;
import :source.module_path;
import std;

namespace {

auto record_name(StandardRecord record) noexcept -> std::string_view {
    switch (record) {
        case StandardRecord::Duration:       return "Duration";
        case StandardRecord::Timeout:        return "Timeout";
        case StandardRecord::TimeRangeError: return "RangeError";
    }
    std::unreachable();
}

auto record_module(StandardRecord record) noexcept -> std::string_view {
    return record == StandardRecord::Timeout ? "crafts.carven.std.time.timeout"
                                             : "crafts.carven.std.time.duration";
}

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
    return symbol.name == record_name(record)
        && draft.module_path_copy(symbol.module_id).value() == record_module(record);
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
            std::format("standard '{}' must be an exported struct", record_name(record))
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
            std::format("standard '{}' has an incompatible representation", record_name(record))
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
        std::format("required standard type '{}' is unavailable", record_name(record))
    ));
}
