module carven:semantic.analysis.decl.resolver;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.decl.context;
import :semantic.analysis.expr.constant;
import :semantic.analysis.expr.scope;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

class DeclResolver final {
public:
    DeclResolver(
        ProgramDraft& target,
        AnalysisCatalogView source_catalog,
        ImportUsage& usage
    ) noexcept
        : draft(target),
          catalog(source_catalog),
          import_usage(usage),
          states(source_catalog.symbols().size(), Unvisited {}),
          modules(source_catalog.modules().size()),
          functions(source_catalog.function_count()),
          structures(source_catalog.struct_count()),
          enumerations(source_catalog.enum_count()),
          enum_cases(source_catalog.enum_case_count()),
          module_constants(source_catalog.symbols().size()),
          callable_contracts(source_catalog.function_count()),
          cpp_import_origins(source_catalog.function_count()) {}

    auto run() noexcept -> AnalysisResult<void> {
        auto first_failure = std::optional<AnalysisFailure>();
        for (const auto& symbol : catalog.symbols()) {
            auto result = resolve(symbol.symbol_id, symbol.module_id, symbol.declaration_span);
            if (!result.has_value() && !first_failure.has_value()) {
                first_failure = result.error();
            }
        }
        for (const auto& symbol : catalog.symbols()) {
            if (!std::holds_alternative<CatalogEnumForm>(symbol.form)) {
                continue;
            }
            auto result = validate_enum_codes(symbol);
            if (!result.has_value() && !first_failure.has_value()) {
                first_failure = result.error();
            }
        }
        if (std::ranges::any_of(states, [](const State& state) noexcept {
                return std::holds_alternative<Unvisited>(state)
                    || std::holds_alternative<Resolving>(state);
            })) {
            invariant_violation("declaration resolution left a non-terminal symbol state");
        }
        if (first_failure.has_value()) {
            return std::unexpected(*first_failure);
        }
        finish_capabilities();
        publish();
        return {};
    }

private:
    struct Unvisited final {};

    struct Resolving final {};

    struct Resolved final {};

    struct Failed final {
        AnalysisFailure failure;
    };

    using State = std::variant<Unvisited, Resolving, Resolved, Failed>;

    auto module_declaration(ProgramModuleID module_id) const noexcept -> ModuleID {
        const auto* record = catalog.find_module(module_id);
        if (record == nullptr) {
            invariant_violation("declaration references an unknown source module");
        }
        return record->declaration;
    }

    auto diagnose_cycle(
        const CatalogSymbol& target,
        ProgramModuleID requester,
        Span origin
    ) noexcept -> AnalysisFailure {
        const auto first = std::ranges::find(active_path, target.symbol_id);
        if (first == active_path.end()) {
            invariant_violation("resolving declaration is absent from its active path");
        }
        auto diagnostic =
            DiagnosticBuilder(DiagnosticCode::ConstCycle, "declaration dependency cycle");
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
                edge == first
                    ? std::format("cycle starts at declaration '{}'", declaration.name)
                    : std::format("cycle passes through declaration '{}'", declaration.name)
            );
        }
        return draft.diagnostics().error(diagnostic.build());
    }

    auto resolve(CatalogSymbolID id, ProgramModuleID requester, Span origin) noexcept
        -> AnalysisResult<void> {
        const auto& symbol = require_catalog_symbol(catalog, id);
        auto& state = states[id.index()];
        if (std::holds_alternative<Resolved>(state)) {
            return {};
        }
        if (const auto* failed = std::get_if<Failed>(&state)) {
            return std::unexpected(failed->failure);
        }
        if (std::holds_alternative<Resolving>(state)) {
            const auto failure = diagnose_cycle(symbol, requester, origin);
            state = Failed {.failure = failure};
            return std::unexpected(failure);
        }

        state = Resolving {};
        active_path.push_back(id);
        auto result = resolve_fresh(symbol);
        active_path.pop_back();
        if (const auto* failed = std::get_if<Failed>(&state)) {
            return std::unexpected(failed->failure);
        }
        if (!result.has_value()) {
            const auto failure = result.error();
            state = Failed {.failure = failure};
            return std::unexpected(failure);
        }
        state = Resolved {};
        return {};
    }

    auto resolve_fresh(const CatalogSymbol& symbol) noexcept -> AnalysisResult<void> {
        if (const auto* form = std::get_if<CatalogEnumCaseForm>(&symbol.form)) {
            return resolve_enum_case(symbol, *form);
        }
        const auto syntax = draft.syntax_tree(symbol.module_id).view();
        const auto& item = syntax.item(symbol.item_id);
        return std::visit(
            Overloaded {
                [&](const CatalogFunctionForm& form) noexcept -> AnalysisResult<void> {
                    const auto* source = std::get_if<ASTFunctionDecl>(&item.value);
                    if (source == nullptr) {
                        invariant_violation("function catalog row does not match source syntax");
                    }
                    return resolve_function(symbol, form, syntax, *source, item.span);
                },
                [&](const CatalogStructForm& form) noexcept -> AnalysisResult<void> {
                    const auto* source = std::get_if<ASTStructDecl>(&item.value);
                    if (source == nullptr) {
                        invariant_violation("struct catalog row does not match source syntax");
                    }
                    return resolve_struct(symbol, form, syntax, *source, item.span);
                },
                [&](const CatalogEnumForm& form) noexcept -> AnalysisResult<void> {
                    const auto* source = std::get_if<ASTEnumDecl>(&item.value);
                    if (source == nullptr) {
                        invariant_violation("enum catalog row does not match source syntax");
                    }
                    return resolve_enum(symbol, form, syntax, *source, item.span);
                },
                [&](const CatalogConstantForm& form) noexcept -> AnalysisResult<void> {
                    const auto* source = std::get_if<ASTConstantDecl>(&item.value);
                    if (source == nullptr) {
                        invariant_violation("constant catalog row does not match source syntax");
                    }
                    return resolve_module_constant(symbol, form, syntax, *source, item.span);
                },
                [](const CatalogEnumCaseForm&) static noexcept -> AnalysisResult<void> {
                    invariant_violation("enum case entered top-level declaration resolution");
                },
            },
            symbol.form
        );
    }

    auto select_symbol(ProgramModuleID module_id, std::string_view name, Span origin) noexcept
        -> AnalysisResult<const CatalogSymbol*> {
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
                    locate(
                        declaration_source_id(draft, selected.module_id),
                        selected.declaration_span
                    ),
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

    struct ConstantScope final {
        DeclResolver& resolver;
        ProgramModuleID module;
        ASTView syntax;

        auto resolve_name(std::string_view name, Span span) noexcept
            -> AnalysisResult<ResolvedConstantName> {
            return resolver.resolve_constant_name(module, name, span);
        }

        auto resolve_enum_qualifier(ASTExprID expression) noexcept
            -> AnalysisResult<std::optional<TypeID>> {
            return resolver.resolve_enum_qualifier(module, syntax, expression);
        }

        auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
            -> AnalysisResult<ResolvedEnumCase> {
            return resolver.resolve_constant_enum_case(module, type, name, span);
        }

        auto resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef> {
            return resolver.resolve_type(module, syntax, type);
        }

        auto supports_equality(ConstructionTypeRef type) noexcept -> bool {
            auto visiting = std::flat_set<TypeID>();
            return resolver.supports_equality(type, visiting);
        }

        auto is_numeric_enum(TypeID type) const noexcept -> bool {
            const auto canonical = resolver.draft.type_copy(type);
            const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
            return nominal != nullptr
                && nominal->enumeration.index() < resolver.enumerations.size()
                && resolver.enumerations[nominal->enumeration.index()].has_value()
                && std::holds_alternative<NumericEnumRepresentation>(
                       resolver.enumerations[nominal->enumeration.index()]->representation
                );
        }
    };

    auto resolve_type(ProgramModuleID module_id, ASTView syntax, ASTTypeID type) noexcept
        -> AnalysisResult<ConstructionTypeRef> {
        auto scope = ConstantScope {*this, module_id, syntax};
        const auto extent = [&](ASTExprID expression) noexcept {
            return evaluate_array_extent(draft, module_id, syntax, scope, expression);
        };
        return resolve_source_type(draft, catalog, import_usage, module_id, syntax, type, extent);
    }

    auto resolve_value_type(
        ProgramModuleID module_id,
        ASTView syntax,
        ASTTypeID type,
        std::string_view role
    ) noexcept -> AnalysisResult<ConstructionTypeRef> {
        auto resolved = resolve_type(module_id, syntax, type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        return require_source_value_type(draft, *resolved, module_id, syntax.type(type).span, role);
    }

    auto resolve_failures(
        ProgramModuleID module_id,
        ASTView syntax,
        const ASTThrowClause& clause
    ) noexcept -> AnalysisResult<std::vector<TypeID>> {
        auto scope = ConstantScope {*this, module_id, syntax};
        const auto extent = [&](ASTExprID expression) noexcept {
            return evaluate_array_extent(draft, module_id, syntax, scope, expression);
        };
        return resolve_failure_types(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            clause,
            extent
        );
    }

    auto resolve_function(
        const CatalogSymbol& symbol,
        const CatalogFunctionForm& form,
        ASTView syntax,
        const ASTFunctionDecl& function,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_struct(
        const CatalogSymbol& symbol,
        const CatalogStructForm& form,
        ASTView syntax,
        const ASTStructDecl& structure,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_enum(
        const CatalogSymbol& symbol,
        const CatalogEnumForm& form,
        ASTView syntax,
        const ASTEnumDecl& enumeration,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_enum_case(const CatalogSymbol& symbol, const CatalogEnumCaseForm& form) noexcept
        -> AnalysisResult<void>;
    auto resolve_module_constant(
        const CatalogSymbol& symbol,
        const CatalogConstantForm& form,
        ASTView syntax,
        const ASTConstantDecl& declaration,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_constant_name(
        ProgramModuleID module_id,
        std::string_view name,
        Span origin
    ) noexcept -> AnalysisResult<ResolvedConstantName>;
    auto resolve_enum_qualifier(
        ProgramModuleID module_id,
        ASTView syntax,
        ASTExprID expression
    ) noexcept -> AnalysisResult<std::optional<TypeID>>;
    auto resolve_constant_enum_case(
        ProgramModuleID module_id,
        TypeID type,
        std::string_view name,
        Span origin
    ) noexcept -> AnalysisResult<ResolvedEnumCase>;
    auto supports_equality(ConstructionTypeRef type, std::flat_set<TypeID>& visiting) noexcept
        -> bool;
    auto validate_enum_codes(const CatalogSymbol& symbol) noexcept -> AnalysisResult<void>;
    auto finish_capabilities() noexcept -> void;
    auto publish() noexcept -> void;

    ProgramDraft& draft;
    AnalysisCatalogView catalog;
    ImportUsage& import_usage;
    std::vector<State> states;
    std::vector<CatalogSymbolID> active_path;
    std::vector<std::optional<ModuleDeclaration>> modules;
    std::vector<std::optional<FunctionDeclaration>> functions;
    std::vector<std::optional<ConstructionStructDeclaration>> structures;
    std::vector<std::optional<EnumDeclaration>> enumerations;
    std::vector<std::optional<ConstructionEnumCaseDeclaration>> enum_cases;
    std::vector<std::optional<ModuleConstantDeclaration>> module_constants;
    std::vector<std::optional<ConstructionCallableContract>> callable_contracts;
    std::vector<std::optional<ProgramOriginID>> cpp_import_origins;
};
