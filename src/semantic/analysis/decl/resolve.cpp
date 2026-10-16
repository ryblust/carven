module carven:semantic.analysis.decl.resolve.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.constant.extent;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.interop;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.source.builder;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

auto DeclResolver::publish_modules() noexcept -> void {
    for (const auto& module_record : catalog.modules()) {
        const auto syntax = draft.syntax_tree(module_record.module_id).view();
        const auto& source = syntax.ast_module();
        auto declaration = ModuleDeclaration {
            .provenance_module = module_record.module_id,
            .origin = declaration_source_origin(draft, module_record.module_id, source.span),
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        };
        declaration.cpp_headers.reserve(source.cpp_header_imports.size());
        for (const auto& header : source.cpp_header_imports) {
            declaration.cpp_headers.push_back({
                .delimiter = header.delimiter == ASTCppHeaderDelimiter::AngleBrackets
                    ? CppHeaderDelimiter::AngleBrackets
                    : CppHeaderDelimiter::Quotes,
                .name = draft.intern_spelling(
                    draft.source_slice_copy(module_record.module_id, header.name_span)
                ),
                .origin = declaration_source_origin(draft, module_record.module_id, header.span),
                .namespace_opening = std::nullopt,
            });
        }
        for (const auto& binding : catalog.cpp_imports(module_record.module_id)) {
            if (!binding.opens_namespace) {
                continue;
            }
            auto components = std::vector<ProgramSpellingID>();
            for (const auto& component : binding.components) {
                components.push_back(draft.intern_spelling(component));
            }
            declaration.cpp_headers.at(binding.header_index).namespace_opening =
                CppNamespaceOpening {
                    .components = std::move(components),
                    .origin =
                        declaration_source_origin(draft, module_record.module_id, binding.origin),
            };
        }
        declaration.cpp_source_fragments.reserve(source.cpp_source_fragments.size());
        for (const auto& fragment : source.cpp_source_fragments) {
            declaration.cpp_source_fragments.push_back({
                .payload_origin = declaration_source_origin(
                    draft,
                    module_record.module_id,
                    fragment.payload_span
                ),
            });
        }
        declaration.items.reserve(module_record.items.size());
        for (const auto& item : module_record.items) {
            if (std::holds_alternative<CatalogConstBlockForm>(item.form)) {
                continue;
            }
            declaration.items.push_back(item.form.visit(
                Overloaded {
                    [](FunctionID id) static noexcept -> ModuleItem { return id; },
                    [](StructID id) static noexcept -> ModuleItem { return id; },
                    [](EnumID id) static noexcept -> ModuleItem { return id; },
                    [](ModuleConstantID id) static noexcept -> ModuleItem { return id; },
                    [](const CatalogConstBlockForm&) static noexcept -> ModuleItem {
                        invariant_violation("const block has no runtime module item");
                    },
                    [](const CatalogTestForm& test) static noexcept -> ModuleItem {
                        return test.test;
                    },
                }
            ));
        }
        draft.define_declaration(module_record.declaration, std::move(declaration));
    }
}

auto DeclResolver::finish_declarations() noexcept -> void {
    for (const auto& symbol : catalog.symbols()) {
        if (published[symbol.symbol_id.index()]) {
            continue;
        }
        symbol.form.visit(
            Overloaded {
                [](const CatalogFunctionForm&) static noexcept {},
                [](const CatalogGenericForm&) static noexcept {},
                [&](const CatalogStructForm& form) noexcept {
                    if (!structures[form.structure.index()].has_value()) {
                        invariant_violation("resolved struct has no declaration fact");
                    }
                    draft.define_declaration(
                        form.structure,
                        std::move(*structures[form.structure.index()])
                    );
                },
                [&](const CatalogEnumForm& form) noexcept {
                    if (!enumerations[form.enumeration.index()].has_value()) {
                        invariant_violation("resolved enum has no declaration fact");
                    }
                    draft.define_declaration(
                        form.enumeration,
                        std::move(*enumerations[form.enumeration.index()])
                    );
                },
                [&](const CatalogEnumCaseForm& form) noexcept {
                    if (!enum_cases[form.enum_case.index()].has_value()) {
                        invariant_violation("resolved enum case has no declaration fact");
                    }
                    draft.define_declaration(
                        form.enum_case,
                        std::move(*enum_cases[form.enum_case.index()])
                    );
                },
                [&](const CatalogConstantForm& form) noexcept {
                    if (!module_constants[form.constant.index()].has_value()) {
                        invariant_violation("resolved module constant has no declaration fact");
                    }
                    draft.define_declaration(
                        form.constant,
                        *module_constants[form.constant.index()]
                    );
                },
            }
        );
    }
    draft.finish_declaration_heads();
    auto enum_roots = std::vector<TypeID>();
    for (const auto id : draft.enum_declaration_ids()) {
        enum_roots.push_back(draft.intern_type({.value = EnumTypeValue {.enumeration = id}}));
    }
    draft.resolve_enum_equality(enum_roots);
    heads_finished = true;
    for (const auto& symbol : catalog.symbols()) {
        const auto* form = std::get_if<CatalogFunctionForm>(&symbol.form);
        if (form == nullptr
            || !cpp_imports[form->callable.index()].has_value()
            || published[symbol.symbol_id.index()]) {
            continue;
        }
        draft.complete_callable(form->callable, *cpp_imports[form->callable.index()]);
    }
}

DeclResolver::DeclResolver(
    ProgramDraft& target,
    AnalysisCatalogView source_catalog,
    ImportUsage& usage,
    ConstructionRequests& requests
) noexcept
    : draft(target),
      catalog(source_catalog),
      import_usage(usage),
      requests(requests),
      heads_finished(false),
      states(source_catalog.symbols().size(), Unvisited {}),
      published(source_catalog.symbols().size(), false),
      structures(source_catalog.struct_count()),
      enumerations(source_catalog.enum_count()),
      enum_cases(source_catalog.enum_case_count()),
      module_constants(source_catalog.symbols().size()),
      cpp_imports(source_catalog.function_count()) {
    publish_modules();
}

auto DeclResolver::run() noexcept -> AnalysisTask<void> {
    for (const auto& symbol : catalog.symbols()) {
        if (std::holds_alternative<CatalogGenericForm>(symbol.form)) {
            static_cast<void>(
                (co_await resolve(symbol.symbol_id, symbol.module_id, symbol.declaration_span))
            );
        }
    }
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    auto checked_generics = draft.validate_generic_definitions();
    if (!checked_generics) {
        co_return checked_generics;
    }
    for (const auto& symbol : catalog.symbols()) {
        static_cast<void>(
            (co_await resolve(symbol.symbol_id, symbol.module_id, symbol.declaration_span))
        );
    }
    for (const auto& symbol : catalog.symbols()) {
        if (!std::holds_alternative<CatalogEnumForm>(symbol.form)) {
            continue;
        }
        static_cast<void>(validate_enum_codes(symbol));
    }
    if (std::ranges::any_of(states, [](const State& state) static noexcept {
            return std::holds_alternative<Unvisited>(state)
                || std::holds_alternative<Resolving>(state);
        })) {
        invariant_violation("declaration resolution left a non-terminal symbol state");
    }
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    finish_declarations();
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    co_return {};
}

auto DeclResolver::module_declaration(ProgramModuleID id) const noexcept -> ModuleID {
    const auto* record = catalog.find_module(id);
    if (record == nullptr) {
        invariant_violation("declaration references an unknown source module");
    }
    return record->declaration;
}

auto DeclResolver::diagnose_cycle(
    const CatalogSymbol& target,
    ProgramModuleID requester,
    Span origin
) noexcept -> AnalysisFailure {
    const auto first = std::ranges::find(active_path, target.symbol_id);
    if (first == active_path.end()) {
        invariant_violation("resolving declaration is absent from its active path");
    }
    auto diagnostic = DiagnosticBuilder(DiagnosticCode::ConstCycle, "declaration dependency cycle");
    diagnostic.primary(
        locate(declaration_source_id(draft, requester), origin),
        "this dependency closes the cycle"
    );
    for (auto edge = first; edge != active_path.end(); ++edge) {
        const auto& declaration = require_catalog_symbol(catalog, *edge);
        diagnostic.related(
            locate(
                declaration_source_id(draft, declaration.module_id),
                declaration.declaration_span
            ),
            edge == first ? std::format("cycle starts at declaration '{}'", declaration.name)
                          : std::format("cycle passes through declaration '{}'", declaration.name)
        );
    }
    return draft.diagnostics().error(diagnostic.build());
}

auto DeclResolver::resolve(CatalogSymbolID id, ProgramModuleID requester, Span origin) noexcept
    -> AnalysisTask<void> {
    const auto& symbol = require_catalog_symbol(catalog, id);
    auto& state = states[id.index()];
    if (std::holds_alternative<Resolved>(state)) {
        co_return {};
    }
    if (const auto* failed = std::get_if<Failed>(&state)) {
        co_return std::unexpected(failed->failure);
    }
    if (std::holds_alternative<Resolving>(state)) {
        const auto failure = diagnose_cycle(symbol, requester, origin);
        state = Failed {.failure = failure};
        co_return std::unexpected(failure);
    }

    state = Resolving {};
    active_path.push_back(id);
    auto result = (co_await resolve_fresh(symbol));
    active_path.pop_back();
    if (const auto* failed = std::get_if<Failed>(&state)) {
        co_return std::unexpected(failed->failure);
    }
    if (!result.has_value()) {
        const auto failure = result.error();
        state = Failed {.failure = failure};
        co_return std::unexpected(failure);
    }
    state = Resolved {};
    co_return {};
}

auto DeclResolver::resolve_fresh(const CatalogSymbol& symbol) noexcept -> AnalysisTask<void> {
    if (const auto* form = std::get_if<CatalogEnumCaseForm>(&symbol.form)) {
        co_return (co_await resolve_enum_case(symbol, *form));
    }
    if (!symbol.item_id) {
        invariant_violation("source catalog declaration has no syntax identity");
    }
    const auto syntax = draft.syntax_tree(symbol.module_id).view();
    const auto& item = syntax.item(*symbol.item_id);
    co_return (co_await symbol.form.visit(
        Overloaded {
            [&](const CatalogFunctionForm& form) noexcept -> AnalysisTask<void> {
                const auto* source = std::get_if<ASTFunctionDecl>(&item.value);
                if (source == nullptr) {
                    invariant_violation("function catalog row does not match source syntax");
                }
                co_return (co_await resolve_function(symbol, form, syntax, *source, item.span));
            },
            [&](const CatalogStructForm& form) noexcept -> AnalysisTask<void> {
                const auto* source = std::get_if<ASTRecordDecl>(&item.value);
                if (source == nullptr) {
                    invariant_violation("struct catalog row does not match source syntax");
                }
                co_return (co_await resolve_struct(symbol, form, syntax, *source, item.span));
            },
            [&](const CatalogEnumForm& form) noexcept -> AnalysisTask<void> {
                const auto* source = std::get_if<ASTEnumDecl>(&item.value);
                if (source == nullptr) {
                    invariant_violation("enum catalog row does not match source syntax");
                }
                co_return (co_await resolve_enum(symbol, form, syntax, *source, item.span));
            },
            [&](const CatalogGenericForm& form) noexcept -> AnalysisTask<void> {
                co_return (co_await resolve_generic_declaration(symbol, form));
            },
            [&](const CatalogConstantForm& form) noexcept -> AnalysisTask<void> {
                const auto* source = std::get_if<ASTConstantDecl>(&item.value);
                if (source == nullptr) {
                    invariant_violation("constant catalog row does not match source syntax");
                }
                co_return (
                    co_await resolve_module_constant(symbol, form, syntax, *source, item.span)
                );
            },
            [](const CatalogEnumCaseForm&) static noexcept -> AnalysisTask<void> {
                invariant_violation("enum case entered top-level declaration resolution");
            },
        }
    ));
}

auto DeclResolver::ConstantScope::evaluate_constant_expression(
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<std::optional<ConstantID>> {
    return resolver.requests
        .evaluate_constant_expression(module_id, expression, expected, generic_context);
}

auto DeclResolver::ConstantScope::evaluate_array_extent(ASTExprID expression) noexcept
    -> AnalysisTask<std::uint64_t> {
    return ::evaluate_array_extent(resolver.draft, syntax, *this, expression);
}

auto DeclResolver::resolve_type(ProgramModuleID module_id, ASTView syntax, ASTTypeID type) noexcept
    -> AnalysisTask<ConstructionTypeRef> {
    auto scope = ConstantScope {*this, module_id, syntax};
    const auto extent = [&](ASTExprID expression) noexcept {
        return scope.evaluate_array_extent(expression);
    };
    co_return (co_await resolve_source_type(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        type,
        extent,
        &requests,
        nullptr,
        draft.source_analysis() ? &draft.source_analysis()->declarations() : nullptr
    ));
}

auto DeclResolver::resolve_failures(
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTThrowClause& clause,
    const GenericTypeContext* generic_context
) noexcept -> AnalysisTask<ConstructionFailureRef> {
    auto scope = ConstantScope {*this, module_id, syntax, generic_context};
    const auto extent = [&](ASTExprID expression) noexcept {
        return scope.evaluate_array_extent(expression);
    };
    co_return (co_await resolve_failure_types(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        clause,
        extent,
        &requests,
        generic_context,
        draft.source_analysis() ? &draft.source_analysis()->declarations() : nullptr
    ));
}
