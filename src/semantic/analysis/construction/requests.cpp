module carven:semantic.analysis.construction.requests.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.requests;
import :semantic.analysis.construction;
import :source.standard;
import std;

ConstructionRequests::ConstructionRequests(ProgramConstruction& owner) noexcept
    : owner(owner) {}

auto ConstructionRequests::ensure_declaration(
    CatalogSymbolID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.declarations.ensure_available(id, requester, span);
}

auto ConstructionRequests::ensure_generic_function(
    GenericDeclarationID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.bodies.ensure_generic_function(id, requester, span);
}

auto ConstructionRequests::ensure_function_signature(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.bodies.ensure_function_signature(id, requester, span);
}

auto ConstructionRequests::ensure_function_body(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<BodyID> {
    return owner.bodies.ensure_function_body(id, requester, span);
}

auto ConstructionRequests::ensure_type(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.declarations.prepare_type(type, requester, span);
}

auto ConstructionRequests::ensure_callable(
    CallableID callable,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.declarations.prepare_callable(callable, requester, span);
}

auto ConstructionRequests::stage() noexcept -> StaticStage& {
    return owner.static_stage;
}

auto ConstructionRequests::standard_declaration(
    StandardDeclaration declaration,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<CatalogSymbolID> {
    const auto& contract = ::standard_declaration(declaration);
    const auto* symbol = owner.catalog.standard_declaration(owner.draft, declaration);
    if (!symbol) {
        co_return std::unexpected(owner.draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::NameUnresolved,
                std::format(
                    "standard declaration '{}.{}' is unavailable",
                    contract.module,
                    contract.name
                )
            )
                .primary(locate(owner.draft.module_source_id(requester), span))
                .build()
        ));
    }
    auto ready = co_await ensure_declaration(symbol->symbol_id, requester, span);
    if (!ready) {
        co_return std::unexpected(ready.error());
    }
    co_return symbol->symbol_id;
}

auto ConstructionRequests::standard_function(
    StandardDeclaration function,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<FunctionID> {
    auto symbol = co_await standard_declaration(function, requester, span);
    if (!symbol) {
        co_return std::unexpected(symbol.error());
    }
    co_return std::get<CatalogFunctionForm>(owner.catalog.symbol(*symbol)->form).function;
}

auto ConstructionRequests::standard_type(
    StandardDeclaration declaration,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<TypeID> {
    auto symbol = co_await standard_declaration(declaration, requester, span);
    if (!symbol) {
        co_return std::unexpected(symbol.error());
    }
    const auto* source = owner.catalog.symbol(*symbol);
    if (const auto* enumeration = std::get_if<CatalogEnumForm>(&source->form)) {
        co_return owner.draft.intern_type({.value = EnumTypeValue {enumeration->enumeration}});
    }
    const auto record = std::get<CatalogStructForm>(source->form).structure;
    const auto structure = owner.draft.construction_struct_declaration_copy(record);
    auto valid = structure.kind == RecordKind::Struct;
    if (declaration == StandardDeclaration::Duration) {
        valid = valid
            && structure.fields.size() == 1uz
            && owner.draft.spelling(structure.fields.front().name) == "nanoseconds"
            && structure.fields.front().type
                == ConstructionTypeRef(owner.draft.builtin_type(BuiltinType::I64));
    } else {
        valid = valid && structure.fields.empty();
    }
    if (!valid) {
        const auto& contract = ::standard_declaration(declaration);
        co_return std::unexpected(owner.draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::TypeMismatch,
                std::format(
                    "standard declaration '{}.{}' does not satisfy its builtin operation contract",
                    contract.module,
                    contract.name
                )
            )
                .primary(locate(owner.draft.module_source_id(requester), span))
                .related(
                    locate(
                        owner.draft.module_source_id(source->module_id),
                        source->declaration_span
                    ),
                    declaration == StandardDeclaration::Duration
                        ? "Duration requires one i64 nanoseconds field"
                        : "timeout failure requires an empty struct"
                )
                .build()
        ));
    }
    co_return owner.draft.intern_type({.value = StructTypeValue {record}});
}

auto ConstructionRequests::number_parser(
    BuiltinType number,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<CallableID> {
    auto failure = co_await standard_type(StandardDeclaration::NumberParseError, requester, span);
    if (!failure) {
        co_return std::unexpected(failure.error());
    }
    const auto enumeration =
        std::get<EnumTypeValue>(owner.draft.type_copy(*failure).value).enumeration;
    const auto declaration = owner.draft.enum_declaration_copy(enumeration);
    if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
        for (const auto id : declaration.cases) {
            const auto item = owner.draft.construction_enum_case_declaration_copy(id);
            if (owner.draft.spelling(item.name) == "Invalid" && item.constant) {
                co_return owner.draft.number_parser(number, *item.constant);
            }
        }
    }
    co_return std::unexpected(owner.draft.diagnostics().error(
        DiagnosticBuilder(
            DiagnosticCode::TypeMismatch,
            "standard NumberParseError requires a numeric enum with an Invalid case"
        )
            .primary(locate(owner.draft.module_source_id(requester), span))
            .build()
    ));
}

auto ConstructionRequests::evaluate_constant_expression(
    ProgramModuleID module_id,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected,
    const GenericTypeContext* generic_context
) noexcept -> AnalysisTask<std::optional<ConstantID>> {
    return owner.bodies
        .evaluate_constant_expression(module_id, expression, expected, generic_context);
}
