module carven:semantic.analysis.decl.resolve.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.constant.root;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.expr.scope;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.operations;
import :semantic.analysis.program;
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

auto DeclResolver::equality_capabilities(std::span<const ConstructionTypeRef> roots) noexcept
    -> std::vector<bool> {
    auto indices = std::map<ConstructionTypeRef, std::size_t>();
    auto types = std::vector<ConstructionTypeRef>();
    auto dependents = std::vector<std::vector<std::size_t>>();
    auto supported = std::vector<bool>();
    const auto intern = [&](ConstructionTypeRef type) noexcept {
        const auto [entry, inserted] = indices.try_emplace(type, types.size());
        if (inserted) {
            types.push_back(type);
            dependents.emplace_back();
            supported.push_back(true);
        }
        return entry->second;
    };
    auto root_indices = std::vector<std::size_t>();
    for (const auto type : roots) {
        root_indices.push_back(intern(type));
    }
    for (auto index = 0uz; index < types.size(); ++index) {
        const auto type = types[index];
        const auto depend = [&](ConstructionTypeRef dependency) noexcept {
            const auto child = intern(dependency);
            dependents[child].push_back(index);
        };
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            const auto construction = draft.construction_type_copy(*term);
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
                depend(array->element);
            } else {
                supported[index] = false;
            }
            continue;
        }
        draft.type_copy(std::get<TypeID>(type))
            .value.visit(
                Overloaded {
                    [&](const BuiltinTypeValue& value) noexcept {
                        supported[index] = builtin_type_supports_equality(value.kind);
                    },
                    [&](const StructTypeValue& value) noexcept {
                        if (value.structure.index() >= structures.size()
                            || !structures[value.structure.index()].has_value()) {
                            supported[index] = false;
                            return;
                        }
                        for (const auto& field : structures[value.structure.index()]->fields) {
                            depend(field.type);
                        }
                    },
                    [&](const EnumTypeValue& value) noexcept {
                        if (value.enumeration.index() >= enumerations.size()
                            || !enumerations[value.enumeration.index()].has_value()) {
                            supported[index] = false;
                            return;
                        }
                        const auto& declaration = *enumerations[value.enumeration.index()];
                        if (std::holds_alternative<NumericEnumRepresentation>(
                                declaration.representation
                            )) {
                            return;
                        }
                        for (const auto case_id : declaration.cases) {
                            for (const auto payload : enum_cases[case_id.index()]->payload_types) {
                                depend(payload);
                            }
                        }
                    },
                    [&](const ArrayTypeValue& value) noexcept { depend(value.element); },
                    [](const PointerTypeValue&) static noexcept {},
                    [&]<typename Value>(const Value&) noexcept {
                        static_assert(
                            std::same_as<Value, FunctionTypeValue>
                            || std::same_as<Value, ClosureTypeValue>
                            || std::same_as<Value, CallableViewTypeValue>
                            || std::same_as<Value, CppTypeValue>
                            || std::same_as<Value, RangeTypeValue>
                            || std::same_as<Value, SliceTypeValue>
                        );
                        supported[index] = false;
                    },
                }
            );
    }
    auto pending = std::vector<std::size_t>();
    for (auto index = 0uz; index < supported.size(); ++index) {
        if (!supported[index]) {
            pending.push_back(index);
        }
    }
    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
        for (const auto dependent : dependents[pending[cursor]]) {
            if (supported[dependent]) {
                supported[dependent] = false;
                pending.push_back(dependent);
            }
        }
    }
    auto result = std::vector<bool>();
    result.reserve(roots.size());
    for (const auto index : root_indices) {
        result.push_back(supported[index]);
    }
    return result;
}

auto DeclResolver::supports_equality(ConstructionTypeRef type) noexcept -> bool {
    return equality_capabilities(std::array {type}).front();
}

auto DeclResolver::finish_capabilities() noexcept -> void {
    auto types = std::vector<ConstructionTypeRef>();
    auto capabilities = std::vector<bool*>();
    for (const auto& symbol : catalog.symbols()) {
        if (const auto* form = std::get_if<CatalogStructForm>(&symbol.form)) {
            types.push_back(
                draft.intern_type({.value = StructTypeValue {.structure = form->structure}})
            );
            capabilities.push_back(&structures[form->structure.index()]->capabilities.equality);
        } else if (const auto* form = std::get_if<CatalogEnumForm>(&symbol.form)) {
            types.push_back(
                draft.intern_type({.value = EnumTypeValue {.enumeration = form->enumeration}})
            );
            capabilities.push_back(&enumerations[form->enumeration.index()]->capabilities.equality);
        }
    }
    const auto supported = equality_capabilities(types);
    for (auto index = 0uz; index < supported.size(); ++index) {
        *capabilities[index] = supported[index];
    }
}

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
            if (std::holds_alternative<CatalogConstantBlockForm>(item.form)) {
                continue;
            }
            declaration.items.push_back(item.form.visit(
                Overloaded {
                    [](FunctionID id) static noexcept -> ModuleItem { return id; },
                    [](StructID id) static noexcept -> ModuleItem { return id; },
                    [](EnumID id) static noexcept -> ModuleItem { return id; },
                    [](ModuleConstantID id) static noexcept -> ModuleItem { return id; },
                    [](const CatalogConstantBlockForm&) static noexcept -> ModuleItem {
                        invariant_violation("constant block has no runtime module item");
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
    heads_finished = true;
    for (const auto& symbol : catalog.symbols()) {
        const auto* form = std::get_if<CatalogFunctionForm>(&symbol.form);
        if (form == nullptr
            || !cpp_import_origins[form->callable.index()].has_value()
            || published[symbol.symbol_id.index()]) {
            continue;
        }
        draft.complete_callable(
            form->callable,
            CppImportImplementation {
                .form_origin = *cpp_import_origins[form->callable.index()],
            }
        );
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
      cpp_import_origins(source_catalog.function_count()) {
    publish_modules();
}

auto DeclResolver::run() noexcept -> AnalysisTask<void> {
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
    if (std::ranges::any_of(states, [](const State& state) noexcept {
            return std::holds_alternative<Unvisited>(state)
                || std::holds_alternative<Resolving>(state);
        })) {
        invariant_violation("declaration resolution left a non-terminal symbol state");
    }
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    finish_capabilities();
    finish_declarations();
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
    const auto syntax = draft.syntax_tree(symbol.module_id).view();
    const auto& item = syntax.item(symbol.item_id);
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
                const auto* source = std::get_if<ASTStructDecl>(&item.value);
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

auto DeclResolver::select_symbol(
    ProgramModuleID module_id,
    std::string_view name,
    Span origin
) noexcept -> AnalysisResult<const CatalogSymbol*> {
    const auto candidates = catalog.lookup(module_id, name);
    if (candidates.empty()) {
        return std::unexpected(declaration_failure(
            draft,
            module_id,
            origin,
            DiagnosticCode::NameUnresolved,
            std::format("unresolved name '{}'", name)
        ));
    }
    if (candidates.size() != 1uz) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::NameAmbiguous,
            std::format("name '{}' is provided by more than one wildcard import", name)
        );
        diagnostic.primary(
            locate(declaration_source_id(draft, module_id), origin),
            "ambiguous reference"
        );
        for (const auto& candidate : candidates) {
            const auto& selected = require_catalog_symbol(catalog, candidate.symbol_id);
            diagnostic.related(
                locate(declaration_source_id(draft, selected.module_id), selected.declaration_span),
                std::format(
                    "candidate from '{}'",
                    draft.module_path_copy(selected.module_id).value()
                )
            );
        }
        return std::unexpected(draft.diagnostics().error(diagnostic.build()));
    }
    const auto& selected = candidates.front();
    if (selected.import_binding.has_value()) {
        import_usage.record(*selected.import_binding);
    }
    return std::addressof(require_catalog_symbol(catalog, selected.symbol_id));
}

auto DeclResolver::ConstantScope::resolve_name(std::string_view name, Span span) noexcept
    -> AnalysisTask<std::optional<ConstantID>> {
    co_return (co_await resolver.resolve_constant_name(module_id, name, span));
}

auto DeclResolver::ConstantScope::resolve_function(std::string_view name, Span span) noexcept
    -> AnalysisTask<std::optional<FunctionID>> {
    if (resolver.catalog.lookup(module_id, name).empty()) {
        co_return std::optional<FunctionID>();
    }
    auto selected = resolver.select_symbol(module_id, name, span);
    if (!selected) {
        co_return std::unexpected(selected.error());
    }
    const auto* function = std::get_if<CatalogFunctionForm>(&(*selected)->form);
    if (function == nullptr) {
        co_return std::optional<FunctionID>();
    }
    auto result = (co_await resolver.ensure_available((*selected)->symbol_id, module_id, span));
    if (!result) {
        co_return std::unexpected(result.error());
    }

    result =
        (co_await resolver.requests.ensure_function_signature(function->function, module_id, span));
    if (!result) {
        co_return std::unexpected(result.error());
    }

    co_return std::optional(function->function);
}

auto DeclResolver::ConstantScope::construction_requests() noexcept -> ConstructionRequests& {
    return resolver.requests;
}

auto DeclResolver::ConstantScope::resolve_enum_qualifier(ASTExprID expression) noexcept
    -> AnalysisTask<std::optional<TypeID>> {
    co_return (co_await resolver.resolve_enum_qualifier(module_id, syntax, expression));
}

auto DeclResolver::ConstantScope::resolve_enum_case(
    TypeID type,
    std::string_view name,
    Span span
) noexcept -> AnalysisTask<ResolvedEnumCase> {
    co_return (co_await resolver.resolve_constant_enum_case(module_id, type, name, span));
}

auto DeclResolver::ConstantScope::resolve_construction_type(
    const ASTConstructionType& type
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    const auto extent = [&](ASTExprID expression) noexcept {
        return evaluate_array_extent(resolver.draft, module_id, syntax, *this, expression);
    };
    auto result = (co_await resolve_source_construction_type(
        resolver.draft,
        resolver.catalog,
        resolver.import_usage,
        module_id,
        syntax,
        type,
        extent
    ));
    if (result) {
        auto prepared = (co_await resolver.requests.ensure_type(*result, module_id, type.span));
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
    }
    co_return result;
}

auto DeclResolver::ConstantScope::resolve_type(ASTTypeID type) noexcept
    -> AnalysisTask<ConstructionTypeRef> {
    co_return (co_await resolver.resolve_type(module_id, syntax, type));
}

auto DeclResolver::ConstantScope::supports_equality(ConstructionTypeRef type) noexcept -> bool {
    return resolver.supports_equality(type);
}

auto DeclResolver::ConstantScope::is_numeric_enum(TypeID type) const noexcept -> bool {
    const auto canonical = resolver.draft.type_copy(type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    return nominal != nullptr
        && nominal->enumeration.index() < resolver.enumerations.size()
        && resolver.enumerations[nominal->enumeration.index()].has_value()
        && std::holds_alternative<NumericEnumRepresentation>(
               resolver.enumerations[nominal->enumeration.index()]->representation
        );
}

auto DeclResolver::resolve_type(ProgramModuleID module_id, ASTView syntax, ASTTypeID type) noexcept
    -> AnalysisTask<ConstructionTypeRef> {
    auto scope = ConstantScope {*this, module_id, syntax};
    const auto extent = [&](ASTExprID expression) noexcept {
        return evaluate_array_extent(draft, module_id, syntax, scope, expression);
    };
    co_return (
        co_await resolve_source_type(draft, catalog, import_usage, module_id, syntax, type, extent)
    );
}

auto DeclResolver::resolve_value_type(
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID type,
    std::string_view role
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    auto resolved = (co_await resolve_type(module_id, syntax, type));
    if (!resolved.has_value()) {
        co_return std::unexpected(resolved.error());
    }
    co_return require_source_value_type(draft, *resolved, module_id, syntax.type(type).span, role);
}

auto DeclResolver::resolve_failures(
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTThrowClause& clause
) noexcept -> AnalysisTask<std::vector<TypeID>> {
    auto scope = ConstantScope {*this, module_id, syntax};
    const auto extent = [&](ASTExprID expression) noexcept {
        return evaluate_array_extent(draft, module_id, syntax, scope, expression);
    };
    co_return (co_await resolve_failure_types(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        clause,
        extent
    ));
}
