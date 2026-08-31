module carven:semantic.analysis.decl.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :semantic.analysis.decl;
import :semantic.analysis.elaboration.decl;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.types;
import :semantic.analysis.elaboration.types.relations;
import :semantic.hir.type;
import :support.invariant;
import std;

namespace {

auto catalog_symbol(AnalysisCatalogView catalog, SymbolID symbol) noexcept -> const CatalogSymbol& {
    const auto* result = catalog.symbol(symbol);
    if (result == nullptr) {
        invariant_violation("typed declaration identity has no catalog symbol");
    }
    return *result;
}

} // namespace

class DeclarationResolverState final {
public:
    enum class Phase {
        Resolving,
        Closed,
        Failed,
    };

    enum class ElaborationState {
        Unresolved,
        Resolving,
        Resolved,
        Failed,
    };

    DeclarationResolverState(
        AnalysisCatalogView source_catalog,
        SemanticDeclarationCapabilities capabilities
    ) noexcept
        : catalog(source_catalog),
          builder(capabilities),
          declaration_states(source_catalog.symbols().size(), ElaborationState::Unresolved) {}

    auto definition_sink() noexcept -> DeclarationDefinitionSink {
        return DeclarationDefinitionSink(*this);
    }

    auto define(FunctionID id, HIRFunctionDecl declaration) noexcept -> void {
        builder.define(id, std::move(declaration));
    }

    auto define(StructID id, HIRStructDecl declaration) noexcept -> void {
        builder.define(id, std::move(declaration));
    }

    auto define(EnumID id, HIREnumDecl declaration) noexcept -> void {
        builder.define(id, std::move(declaration));
    }

    auto define(EnumCaseID id, SemanticEnumCaseContract declaration) noexcept -> void {
        builder.define(id, std::move(declaration));
    }

    auto function(FunctionID id) const noexcept -> FunctionContractView {
        require_accessible();
        const auto& declaration = builder.function(id);
        return {
            .callable = declaration.callable,
            .symbol = declaration.symbol,
            .entry_point = declaration.entry_point,
        };
    }

    auto structure(StructID id) const noexcept -> StructContractView {
        require_accessible();
        const auto& declaration = builder.structure(id);
        return {.fields = declaration.fields, .symbol = declaration.symbol};
    }

    auto enumeration(EnumID id) const noexcept -> EnumContractView {
        require_accessible();
        const auto& declaration = builder.enumeration(id);
        return {
            .profile = declaration.profile,
            .underlying_type = declaration.underlying_type,
            .cases = declaration.cases,
            .symbol = declaration.symbol,
        };
    }

    auto enum_case(EnumCaseID id) const noexcept -> SemanticEnumCaseView {
        require_accessible();
        return builder.enum_case(id);
    }

    auto enum_count() const noexcept -> std::size_t {
        require_accessible();
        return catalog.enum_count();
    }

    auto enum_case_count() const noexcept -> std::size_t {
        require_accessible();
        return catalog.enum_case_count();
    }

    auto supports_equality(HIRTypeID id) const noexcept -> bool {
        require_accessible();
        auto visiting = std::flat_set<HIRTypeID>();
        const auto check = [&](this const auto& self, HIRTypeID candidate) noexcept -> bool {
            const auto& value = builder.type(candidate).value;
            if (const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&value)) {
                return builtin_type_supports_equality(builtin->kind);
            }
            if (const auto* array = std::get_if<HIRArrayTypeValue>(&value)) {
                return self(array->element_type_id);
            }
            if (const auto* nominal = std::get_if<HIRStructTypeValue>(&value)) {
                if (!visiting.insert(candidate).second) {
                    return true;
                }
                const auto contract = structure(nominal->structure);
                const auto result =
                    std::ranges::all_of(contract.fields, [&](const auto& field) noexcept {
                        return self(field.type);
                    });
                visiting.erase(candidate);
                return result;
            }
            if (const auto* nominal = std::get_if<HIREnumTypeValue>(&value)) {
                if (!visiting.insert(candidate).second) {
                    return true;
                }
                const auto contract = enumeration(nominal->enumeration);
                const auto result =
                    contract.profile == HIREnumProfile::Numeric
                    || std::ranges::all_of(contract.cases, [&](EnumCaseID enum_case_id) noexcept {
                           return std::ranges::all_of(enum_case(enum_case_id).payload_types, self);
                       });
                visiting.erase(candidate);
                return result;
            }
            return std::holds_alternative<HIRErrorTypeValue>(value);
        };
        return check(id);
    }

    auto resolve(SymbolID symbol, ProgramModuleID requester_module, Span origin) noexcept -> bool {
        const auto* declaration = catalog.symbol(symbol);
        if (declaration == nullptr) {
            invariant_violation("declaration resolver references an unknown catalog symbol");
        }
        return std::holds_alternative<CatalogEnumCaseForm>(declaration->form)
            ? resolve_enum_case(*declaration, requester_module, origin)
            : resolve_declaration(*declaration, requester_module, origin);
    }

    auto resolve_from_view(SymbolID symbol, ProgramModuleID requester_module, Span origin) noexcept
        -> bool {
        if (phase == Phase::Resolving) {
            return resolve(symbol, requester_module, origin);
        }
        if (phase == Phase::Closed) {
            return resolved(symbol);
        }
        invariant_violation("declaration resolution used a failed resolver");
    }

    auto resolve_all(std::span<ModuleAnalysis> modules) noexcept -> void {
        if (phase != Phase::Resolving || !active_modules.empty() || !active_path.empty()) {
            invariant_violation("declaration resolver was already active");
        }
        active_modules = modules;
        for (const auto& declaration : catalog.symbols()) {
            static_cast<void>(
                resolve(declaration.symbol_id, declaration.module_id, declaration.declaration_span)
            );
        }
        for (const auto& declaration : catalog.symbols()) {
            if (std::holds_alternative<CatalogEnumForm>(declaration.form)) {
                validate_enum_codes(source(declaration.module_id), declaration.symbol_id);
            }
        }
        if (!active_path.empty()
            || std::ranges::any_of(declaration_states, [](ElaborationState state) static noexcept {
                   return state == ElaborationState::Unresolved
                       || state == ElaborationState::Resolving;
               })) {
            invariant_violation("declaration resolution did not reach a terminal state");
        }
        const auto failed =
            std::ranges::any_of(declaration_states, [](ElaborationState state) static noexcept {
                return state == ElaborationState::Failed;
            });
        if (!failed) {
            close_contracts();
        }
        active_modules = {};
        phase = failed ? Phase::Failed : Phase::Closed;
    }

private:
    auto require_accessible() const noexcept -> void {
        if (phase == Phase::Failed) {
            invariant_violation("declaration contract used after resolution failed");
        }
    }

    auto declared_symbol_type(SymbolID symbol) const noexcept -> HIRTypeID {
        const auto type = builder.symbol(symbol).type;
        if (!type.has_value()) {
            invariant_violation("resolved declaration symbol has no type");
        }
        return *type;
    }

    auto close_contracts() noexcept -> void {
        auto struct_capabilities = std::vector<HIRNominalCapabilities>();
        struct_capabilities.reserve(catalog.struct_count());
        for (auto index = 0uz; index < catalog.struct_count(); ++index) {
            const auto id = StructID::from_index(static_cast<std::uint32_t>(index));
            struct_capabilities.push_back({
                .equality = supports_equality(declared_symbol_type(structure(id).symbol)),
            });
        }

        for (auto index = 0uz; index < catalog.enum_case_count(); ++index) {
            const auto id = EnumCaseID::from_index(static_cast<std::uint32_t>(index));
            const auto contract = enum_case(id);
            const auto& definition = catalog_symbol(catalog, contract.symbol);
            const auto constant =
                source(definition.module_id).builder().symbol_constant(contract.symbol);
            const auto owner = enumeration(contract.owner);
            if ((owner.profile == HIREnumProfile::Numeric || contract.payload_types.empty())
                && !constant.has_value()) {
                invariant_violation("constant enum case has no normalized fact");
            }
            builder.complete_enum_case(id, constant);
        }

        auto enum_capabilities = std::vector<HIRNominalCapabilities>();
        enum_capabilities.reserve(catalog.enum_count());
        for (auto index = 0uz; index < catalog.enum_count(); ++index) {
            const auto id = EnumID::from_index(static_cast<std::uint32_t>(index));
            enum_capabilities.push_back({
                .equality = supports_equality(declared_symbol_type(enumeration(id).symbol)),
            });
        }
        builder.seal_declarations(std::move(struct_capabilities), std::move(enum_capabilities));
    }

    auto source(ProgramModuleID id) noexcept -> ModuleAnalysis& {
        if (id.index() >= active_modules.size()) {
            invariant_violation("declaration dependency requires an inactive module context");
        }
        return active_modules[id.index()];
    }

    auto known_result(ElaborationState state) const noexcept -> std::optional<bool> {
        if (state == ElaborationState::Resolved) {
            return true;
        }
        if (state == ElaborationState::Failed) {
            return false;
        }
        return std::nullopt;
    }

    auto resolved(SymbolID symbol) const noexcept -> bool {
        if (catalog.symbol(symbol) == nullptr || symbol.index() >= declaration_states.size()) {
            invariant_violation("declaration result references an unknown symbol");
        }
        const auto state = declaration_states[symbol.index()];
        if (state == ElaborationState::Resolved) {
            return true;
        }
        if (state == ElaborationState::Failed) {
            return false;
        }
        invariant_violation("declaration result was queried before resolution closed");
    }

    auto diagnose_cycle(SymbolID target, ProgramModuleID requester_module, Span origin) noexcept
        -> void {
        const auto first = std::ranges::find(active_path, target);
        if (first == active_path.end()) {
            invariant_violation("resolving declaration is absent from its active path");
        }
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::ConstCycle,
            "declaration elaboration dependency cycle"
        );
        diagnostic.primary(
            locate(source(requester_module).source_id(), origin),
            "this dependency closes the cycle"
        );
        for (auto edge = first; edge != active_path.end(); ++edge) {
            const auto& declaration = catalog_symbol(catalog, *edge);
            diagnostic.related(
                locate(source(declaration.module_id).source_id(), declaration.declaration_span),
                edge == first
                    ? std::format("cycle starts at declaration '{}'", declaration.name)
                    : std::format("cycle passes through declaration '{}'", declaration.name)
            );
        }
        source(requester_module).emit(diagnostic.build());
    }

    auto begin_resolution(
        const CatalogSymbol& declaration,
        ProgramModuleID requester_module,
        Span origin
    ) noexcept -> std::optional<bool> {
        auto& state = declaration_states[declaration.symbol_id.index()];
        if (const auto known = known_result(state)) {
            return *known;
        }
        if (state == ElaborationState::Resolving) {
            diagnose_cycle(declaration.symbol_id, requester_module, origin);
            state = ElaborationState::Failed;
            return false;
        }
        state = ElaborationState::Resolving;
        active_path.push_back(declaration.symbol_id);
        return std::nullopt;
    }

    auto finish_resolution(const CatalogSymbol& declaration, bool success) noexcept -> bool {
        active_path.pop_back();
        auto& state = declaration_states[declaration.symbol_id.index()];
        if (state == ElaborationState::Failed || !success) {
            state = ElaborationState::Failed;
            return false;
        }
        state = ElaborationState::Resolved;
        return true;
    }

    auto resolve_declaration(
        const CatalogSymbol& declaration,
        ProgramModuleID requester_module,
        Span origin
    ) noexcept -> bool {
        if (std::holds_alternative<CatalogEnumCaseForm>(declaration.form)) {
            invariant_violation("enum case entered declaration-signature elaboration");
        }
        if (const auto known = begin_resolution(declaration, requester_module, origin)) {
            return *known;
        }
        auto& owner = source(declaration.module_id);
        const auto& item = owner.syntax().item(declaration.item_id);
        const auto success = [&]() noexcept {
            if (std::holds_alternative<CatalogConstantForm>(declaration.form)) {
                const auto* constant = std::get_if<ASTConstantDecl>(&item.value);
                if (constant == nullptr) {
                    invariant_violation("constant catalog form does not match its syntax item");
                }
                return elaborate_module_constant(owner, declaration.symbol_id, *constant);
            }
            auto definitions = definition_sink();
            return elaborate_declaration_contract(
                owner,
                definitions,
                declaration.symbol_id,
                declaration.item_id
            );
        }();
        return finish_resolution(declaration, success);
    }

    auto resolve_enum_case(
        const CatalogSymbol& declaration,
        ProgramModuleID requester_module,
        Span origin
    ) noexcept -> bool {
        const auto* form = std::get_if<CatalogEnumCaseForm>(&declaration.form);
        if (form == nullptr) {
            invariant_violation("catalog enum case is missing its owner identity");
        }
        if (const auto known = begin_resolution(declaration, requester_module, origin)) {
            return *known;
        }
        const auto owner_symbol = catalog.enum_symbol(form->owner);
        const auto& owner_declaration = catalog_symbol(catalog, owner_symbol);
        if (!std::holds_alternative<CatalogEnumForm>(owner_declaration.form)) {
            invariant_violation("catalog enum case owner is not an enum declaration");
        }
        auto success = resolve(owner_symbol, declaration.module_id, declaration.declaration_span);
        if (success) {
            success = elaborate_enum_case(source(declaration.module_id), owner_symbol, form->index);
        }
        return finish_resolution(declaration, success);
    }

    AnalysisCatalogView catalog;
    SemanticDeclarationCapabilities builder;
    std::span<ModuleAnalysis> active_modules;
    std::vector<ElaborationState> declaration_states;
    std::vector<SymbolID> active_path;
    Phase phase = Phase::Resolving;
};

DeclarationContractView::DeclarationContractView(DeclarationResolverState& source) noexcept
    : state(std::addressof(source)) {}

auto DeclarationContractView::function(FunctionID id) const noexcept -> FunctionContractView {
    return state->function(id);
}

auto DeclarationContractView::structure(StructID id) const noexcept -> StructContractView {
    return state->structure(id);
}

auto DeclarationContractView::enumeration(EnumID id) const noexcept -> EnumContractView {
    return state->enumeration(id);
}

auto DeclarationContractView::enum_case(EnumCaseID id) const noexcept -> SemanticEnumCaseView {
    return state->enum_case(id);
}

auto DeclarationContractView::enum_count() const noexcept -> std::size_t {
    return state->enum_count();
}

auto DeclarationContractView::enum_case_count() const noexcept -> std::size_t {
    return state->enum_case_count();
}

auto DeclarationContractView::supports_equality(HIRTypeID id) const noexcept -> bool {
    return state->supports_equality(id);
}

auto DeclarationContractView::resolve(
    SymbolID symbol,
    ProgramModuleID requester_module,
    Span origin
) const noexcept -> bool {
    return state->resolve_from_view(symbol, requester_module, origin);
}

DeclarationDefinitionSink::DeclarationDefinitionSink(DeclarationResolverState& source) noexcept
    : state(std::addressof(source)) {}

auto DeclarationDefinitionSink::define(FunctionID id, HIRFunctionDecl declaration) noexcept
    -> void {
    state->define(id, std::move(declaration));
}

auto DeclarationDefinitionSink::define(StructID id, HIRStructDecl declaration) noexcept -> void {
    state->define(id, std::move(declaration));
}

auto DeclarationDefinitionSink::define(EnumID id, HIREnumDecl declaration) noexcept -> void {
    state->define(id, std::move(declaration));
}

auto DeclarationDefinitionSink::define(EnumCaseID id, SemanticEnumCaseContract declaration) noexcept
    -> void {
    state->define(id, std::move(declaration));
}

DeclarationResolver::DeclarationResolver(
    AnalysisCatalogView catalog,
    SemanticDeclarationCapabilities capabilities
) noexcept
    : state(std::make_unique<DeclarationResolverState>(catalog, capabilities)) {}

DeclarationResolver::~DeclarationResolver() noexcept = default;

auto DeclarationResolver::view() noexcept -> DeclarationContractView {
    return DeclarationContractView(*state);
}

auto DeclarationResolver::resolve_all(std::span<ModuleAnalysis> modules) noexcept -> void {
    state->resolve_all(modules);
}
