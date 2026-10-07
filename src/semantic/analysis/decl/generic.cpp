module carven:semantic.analysis.decl.generic.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.constant.root;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.types;
import :semantic.semir.generic;
import :support.invariant;
import :support.visit;
import std;

auto DeclResolver::resolve_generic_nominal(
    const CatalogSymbol& symbol,
    const CatalogGenericForm& form
) noexcept -> AnalysisTask<void> {
    const auto syntax = draft.syntax_tree(symbol.module_id).view();
    const auto& item = syntax.item(symbol.item_id);
    const auto* record = std::get_if<ASTRecordDecl>(&item.value);
    const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value);
    const auto* clause = record ? &record->type_parameters
        : enumeration           ? &enumeration->type_parameters
                                : nullptr;
    if (!clause || !*clause) {
        invariant_violation("generic catalog identity does not name parameterized source");
    }
    auto parameters = std::vector<ProgramSpellingID>();
    auto parameter_names = std::map<std::string, Span, std::less<>>();
    for (const auto span : (*clause)->names) {
        auto name = draft.source_slice_copy(symbol.module_id, span);
        if (name == "_" || source_type_name_is_reserved(name)) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                span,
                DiagnosticCode::TypeGenericDefinition,
                "type parameter requires an unreserved name"
            ));
        }
        const auto [prior, inserted] = parameter_names.emplace(name, span);
        if (!inserted) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::TypeGenericDefinition,
                "a type parameter is declared more than once"
            );
            diagnostic.primary(
                locate(declaration_source_id(draft, symbol.module_id), span),
                "duplicate parameter"
            );
            diagnostic.related(
                locate(declaration_source_id(draft, symbol.module_id), prior->second),
                "first declaration"
            );
            co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        parameters.push_back(draft.intern_spelling(name));
    }
    const auto context =
        GenericTypeContext {.definition = form.definition, .parameters = parameters};
    auto scope = ConstantScope {*this, symbol.module_id, syntax, &context};
    const auto extent = [&](ASTExprID expression) noexcept {
        return evaluate_array_extent(draft, symbol.module_id, syntax, scope, expression);
    };
    auto contract = GenericDeclarationContract {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_source_origin(draft, symbol.module_id, item.span),
        .visibility = symbol.visibility,
        .parameters = {},
        .audience_dependencies = {},
    };
    auto member_names = std::map<std::string, Span, std::less<>>();
    auto visited_surface_types = std::flat_set<GenericTypeID>();
    const auto collect_surface =
        [&](auto&& self, GenericTypeID type, ProgramOriginID origin) noexcept -> void {
        if (!visited_surface_types.insert(type).second) {
            return;
        }
        draft.generic_type_copy(type).visit(
            Overloaded {
                [&](TypeID concrete) noexcept {
                    contract.audience_dependencies.push_back(
                        {.reference = concrete, .origin = origin}
                    );
                },
                [](const GenericTypeParameter&) static noexcept {},
                [&](const GenericArrayType& array) noexcept { self(self, array.element, origin); },
                [&](const GenericSliceType& slice) noexcept { self(self, slice.element, origin); },
                [&](const GenericOwnedSequenceType& sequence) noexcept {
                    self(self, sequence.element, origin);
                },
                [&](const GenericPointerType& pointer) noexcept {
                    self(self, pointer.target, origin);
                },
                [&](const GenericNominalApplication& application) noexcept {
                    contract.audience_dependencies.push_back(
                        {.reference = application.definition, .origin = origin}
                    );
                    for (const auto argument : application.arguments) {
                        self(self, argument, origin);
                    }
                },
            }
        );
    };
    if (record) {
        if (!record->operations.empty()) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                syntax.item(record->operations.front()).span,
                DiagnosticCode::TypeGenericDefinition,
                "operations on type-parameterized classes are not supported"
            ));
        }
        auto fields = std::vector<GenericField>();
        for (const auto& field : record->fields) {
            const auto name = draft.source_slice_copy(symbol.module_id, field.name_span);
            const auto [prior, inserted] = member_names.emplace(name, field.name_span);
            if (!inserted) {
                auto diagnostic = DiagnosticBuilder(
                    DiagnosticCode::NameDuplicateField,
                    "a structure field is declared more than once"
                );
                diagnostic.primary(
                    locate(declaration_source_id(draft, symbol.module_id), field.name_span),
                    "duplicate field"
                );
                diagnostic.related(
                    locate(declaration_source_id(draft, symbol.module_id), prior->second),
                    "first declaration"
                );
                co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
            }
            auto type = (co_await resolve_generic_source_type(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                field.type,
                context,
                extent,
                true,
                &requests
            ));
            if (!type) {
                co_return std::unexpected(type.error());
            }
            fields.push_back(
                {.name = draft.intern_spelling(name),
                 .type = *type,
                 .origin = declaration_source_origin(draft, symbol.module_id, field.span)}
            );
        }
        if (record->kind == ASTRecordKind::Struct) {
            for (const auto& field : fields) {
                collect_surface(collect_surface, field.type, field.origin);
            }
        }
        contract.parameters = std::move(parameters);
        draft.define_generic_declaration(
            form.definition,
            GenericRecordDefinition {
                .contract = std::move(contract),
                .kind =
                    record->kind == ASTRecordKind::Class ? RecordKind::Class : RecordKind::Struct,
                .fields = std::move(fields),
            }
        );
        co_return {};
    }
    if (enumeration->cases.empty()) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            enumeration->name_span,
            DiagnosticCode::TypeEnumEmpty,
            "enum must declare at least one case"
        ));
    }
    const auto payload =
        std::ranges::any_of(enumeration->cases, [](const ASTEnumCase& source_case) static noexcept {
            return !source_case.payload_types.empty();
        });
    if (!payload) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            enumeration->name_span,
            DiagnosticCode::TypeGenericDefinition,
            "type-parameterized numeric enums are not supported"
        ));
    }
    if (enumeration->underlying_type) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            syntax.type(*enumeration->underlying_type).span,
            DiagnosticCode::TypeEnumProfile,
            "payload enum cannot declare an underlying integer type"
        ));
    }
    auto cases = std::vector<GenericEnumCase>();
    for (const auto& source_case : enumeration->cases) {
        const auto name = draft.source_slice_copy(symbol.module_id, source_case.name_span);
        const auto [prior, inserted] = member_names.emplace(name, source_case.name_span);
        if (!inserted) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::NameDuplicateEnumCase,
                "an enum case is declared more than once"
            );
            diagnostic.primary(
                locate(declaration_source_id(draft, symbol.module_id), source_case.name_span),
                "duplicate case"
            );
            diagnostic.related(
                locate(declaration_source_id(draft, symbol.module_id), prior->second),
                "first declaration"
            );
            co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        if (source_case.initializer) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                syntax.expression(*source_case.initializer).span,
                DiagnosticCode::TypeEnumProfile,
                "payload enum case cannot declare a numeric initializer"
            ));
        }
        auto types = std::vector<GenericTypeID>();
        for (const auto source_type : source_case.payload_types) {
            auto type = (co_await resolve_generic_source_type(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                source_type,
                context,
                extent,
                true,
                &requests
            ));
            if (!type) {
                co_return std::unexpected(type.error());
            }
            types.push_back(*type);
        }
        cases.push_back(
            {.name = draft.intern_spelling(name),
             .origin = declaration_source_origin(draft, symbol.module_id, source_case.span),
             .payload_types = std::move(types)}
        );
    }
    for (const auto& source_case : cases) {
        for (const auto type : source_case.payload_types) {
            collect_surface(collect_surface, type, source_case.origin);
        }
    }
    contract.parameters = std::move(parameters);
    draft.define_generic_declaration(
        form.definition,
        GenericEnumDefinition {
            .contract = std::move(contract),
            .cases = std::move(cases),
        }
    );
    co_return {};
}
