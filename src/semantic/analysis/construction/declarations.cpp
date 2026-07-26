module carven:semantic.analysis.declaration_construction.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :semantic.analysis.declaration_construction;
import :semantic.analysis.elaboration.declarations;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.types;
import :semantic.analysis.elaboration.types.relations;
import :semantic.hir.type;
import :support.invariant;
import std;

namespace {

template<typename Value, typename ID>
auto define_state(std::vector<std::optional<Value>>& states, ID id, Value value) noexcept -> void {
    if (id.index() >= states.size()) {
        invariant_violation("declaration contract references an unreserved typed identity");
    }
    auto& state = states[id.index()];
    if (state.has_value()) {
        invariant_violation("declaration contract was defined more than once");
    }
    state = std::move(value);
}

template<typename Value, typename ID>
auto resolved_state(const std::vector<std::optional<Value>>& states, ID id) noexcept
    -> const Value& {
    if (id.index() >= states.size() || !states[id.index()].has_value()) {
        invariant_violation("declaration contract lookup used an unresolved identity");
    }
    return *states[id.index()];
}

auto complete(const auto& states) noexcept -> bool {
    return std::ranges::all_of(states, [](const auto& state) static noexcept {
        return state.has_value();
    });
}

auto catalog_symbol(AnalysisCatalogView catalog, SymbolID symbol) noexcept -> const CatalogSymbol& {
    const auto* result = catalog.symbol(symbol);
    if (result == nullptr) {
        invariant_violation("typed declaration identity has no catalog symbol");
    }
    return *result;
}

struct FrozenDeclarationTables final {
    std::vector<HIRFunctionDecl> functions;
    std::vector<HIRStructDecl> structures;
    std::vector<HIREnumDecl> enumerations;
    std::vector<HIREnumCase> enum_cases;
};

} // namespace

class DeclarationSessionState final {
public:
    enum class Phase {
        Resolving,
        Closed,
        Failed,
        Finished,
    };

    enum class ElaborationState {
        Unresolved,
        Resolving,
        Resolved,
        Failed,
    };

    DeclarationSessionState(
        AnalysisCatalogView source_catalog,
        SemanticConstruction& builder
    ) noexcept
        : catalog(source_catalog),
          hir_builder(builder),
          declaration_states(source_catalog.symbols().size(), ElaborationState::Unresolved),
          function_states(source_catalog.function_count()),
          struct_states(source_catalog.struct_count()),
          enum_states(source_catalog.enum_count()),
          enum_case_states(source_catalog.enum_case_count()) {}

    auto definition_sink() noexcept -> DeclarationDefinitionSink {
        return DeclarationDefinitionSink(*this);
    }

    auto define(FunctionID id, FunctionContractState contract) noexcept -> void {
        define_state(function_states, id, std::move(contract));
    }

    auto define(StructID id, StructContractState contract) noexcept -> void {
        define_state(struct_states, id, std::move(contract));
    }

    auto define(EnumID id, EnumContractState contract) noexcept -> void {
        define_state(enum_states, id, std::move(contract));
    }

    auto define(EnumCaseID id, EnumCaseContractState contract) noexcept -> void {
        define_state(enum_case_states, id, std::move(contract));
    }

    auto function(FunctionID id) const noexcept -> FunctionContractView {
        require_accessible();
        const auto& state = resolved_state(function_states, id);
        return {
            .callable = state.callable,
            .symbol = state.symbol,
            .entry_point = state.entry_point,
        };
    }

    auto structure(StructID id) const noexcept -> StructContractView {
        require_accessible();
        const auto& state = resolved_state(struct_states, id);
        return {.fields = state.fields, .symbol = state.symbol};
    }

    auto enumeration(EnumID id) const noexcept -> EnumContractView {
        require_accessible();
        const auto& state = resolved_state(enum_states, id);
        return {
            .profile = state.profile,
            .underlying_type = state.underlying_type,
            .cases = state.cases,
            .symbol = state.symbol,
        };
    }

    auto enum_case(EnumCaseID id) const noexcept -> EnumCaseContractView {
        require_accessible();
        const auto& state = resolved_state(enum_case_states, id);
        return {
            .owner = state.owner,
            .name = state.name,
            .payload_types = state.payload_types,
            .symbol = state.symbol,
            .origin = state.origin,
        };
    }

    auto enum_count() const noexcept -> std::size_t {
        require_accessible();
        return catalog.enum_count();
    }

    auto enum_case_count() const noexcept -> std::size_t {
        require_accessible();
        return catalog.enum_case_count();
    }

    auto resolved(SymbolID symbol_id) const noexcept -> bool {
        if (catalog.symbol(symbol_id) == nullptr
            || symbol_id.index() >= declaration_states.size()) {
            invariant_violation("declaration resolution result references an unknown symbol");
        }
        const auto state = declaration_states[symbol_id.index()];
        if (state == ElaborationState::Resolved) {
            return true;
        }
        if (state == ElaborationState::Failed) {
            return false;
        }
        invariant_violation("declaration resolution result was queried before resolution closed");
    }

    auto supports_equality(HIRTypeID id) const noexcept -> bool {
        require_accessible();
        auto visiting = std::flat_set<HIRTypeID>();
        const auto check = [&](this const auto& self, HIRTypeID candidate) noexcept -> bool {
            const auto& value = hir_builder.type(candidate).value;
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

    auto resolve(SymbolID symbol_id, ProgramModuleID requester_module_id, Span origin) noexcept
        -> bool {
        const auto* declaration = catalog.symbol(symbol_id);
        if (declaration == nullptr) {
            invariant_violation("declaration resolver references an unknown catalog symbol");
        }
        return std::holds_alternative<CatalogEnumCaseForm>(declaration->form)
            ? resolve_enum_case(*declaration, requester_module_id, origin)
            : resolve_declaration(*declaration, requester_module_id, origin);
    }

    auto resolve_from_view(
        SymbolID symbol_id,
        ProgramModuleID requester_module_id,
        Span origin
    ) noexcept -> bool {
        if (phase == Phase::Resolving) {
            return resolve(symbol_id, requester_module_id, origin);
        }
        if (phase == Phase::Closed) {
            return resolved(symbol_id);
        }
        invariant_violation("declaration resolution used an inaccessible session");
    }

    auto resolve_all(std::span<ModuleAnalysis> modules) noexcept -> void {
        if (phase != Phase::Resolving || !active_modules.empty() || !active_path.empty()) {
            invariant_violation("declaration resolution session was already active");
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
            freeze_declarations();
        }
        active_modules = {};
        phase = failed ? Phase::Failed : Phase::Closed;
    }

    auto finish() noexcept -> void {
        if (phase != Phase::Closed
            || !active_modules.empty()
            || !active_path.empty()
            || !frozen_declarations.has_value()) {
            invariant_violation("declaration contracts did not close exactly once and completely");
        }
        auto declarations = std::move(*frozen_declarations);
        frozen_declarations.reset();
        hir_builder.publish_declaration_contracts(
            std::move(declarations.functions),
            std::move(declarations.structures),
            std::move(declarations.enumerations),
            std::move(declarations.enum_cases)
        );
        phase = Phase::Finished;
    }

private:
    auto require_accessible() const noexcept -> void {
        if (phase == Phase::Failed || phase == Phase::Finished) {
            invariant_violation("declaration contract used an inaccessible session");
        }
    }

    auto declared_symbol_type(SymbolID symbol) const noexcept -> HIRTypeID {
        const auto type = hir_builder.symbol(symbol).type;
        if (!type.has_value()) {
            invariant_violation("resolved declaration symbol has no type");
        }
        return *type;
    }

    auto freeze_declarations() noexcept -> void {
        if (frozen_declarations.has_value()
            || !complete(function_states)
            || !complete(struct_states)
            || !complete(enum_states)
            || !complete(enum_case_states)) {
            invariant_violation("declaration contracts were not frozen exactly once");
        }

        auto declarations = FrozenDeclarationTables();
        declarations.functions.reserve(function_states.size());
        declarations.structures.reserve(struct_states.size());
        declarations.enumerations.reserve(enum_states.size());
        declarations.enum_cases.reserve(enum_case_states.size());

        for (const auto& slot : function_states) {
            const auto& contract = *slot;
            declarations.functions.push_back({
                .origin = contract.origin,
                .visibility = contract.visibility,
                .name = contract.name,
                .callable = contract.callable,
                .result = contract.result,
                .result_origin = contract.result_origin,
                .symbol = contract.symbol,
                .entry_point = contract.entry_point,
            });
        }
        for (const auto& slot : struct_states) {
            const auto& contract = *slot;
            declarations.structures.push_back({
                .origin = contract.origin,
                .visibility = contract.visibility,
                .name = contract.name,
                .fields = contract.fields,
                .symbol = contract.symbol,
                .supports_equality = supports_equality(declared_symbol_type(contract.symbol)),
            });
        }
        for (const auto& slot : enum_case_states) {
            const auto& contract = *slot;
            const auto& definition = catalog_symbol(catalog, contract.symbol);
            const auto constant =
                source(definition.module_id).builder().symbol_constant(contract.symbol);
            const auto owner = enumeration(contract.owner);
            if ((owner.profile == HIREnumProfile::Numeric || contract.payload_types.empty())
                && !constant.has_value()) {
                invariant_violation("constant enum case has no normalized fact");
            }
            declarations.enum_cases.push_back({
                .owner = contract.owner,
                .name = contract.name,
                .payload_types = contract.payload_types,
                .constant = constant,
                .symbol = contract.symbol,
                .origin = contract.origin,
            });
        }
        for (const auto& slot : enum_states) {
            const auto& contract = *slot;
            declarations.enumerations.push_back({
                .origin = contract.origin,
                .visibility = contract.visibility,
                .name = contract.name,
                .underlying_type = contract.underlying_type,
                .cases = contract.cases,
                .profile = contract.profile,
                .symbol = contract.symbol,
                .supports_equality = supports_equality(declared_symbol_type(contract.symbol)),
            });
        }
        frozen_declarations = std::move(declarations);
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

    auto diagnose_cycle(
        SymbolID target_symbol_id,
        ProgramModuleID requester_module_id,
        Span origin
    ) noexcept -> void {
        const auto first = std::ranges::find(active_path, target_symbol_id);
        if (first == active_path.end()) {
            invariant_violation("resolving elaboration node is absent from its active path");
        }
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::ConstCycle,
            "declaration elaboration dependency cycle"
        );
        diagnostic.primary(
            locate(source(requester_module_id).source_id(), origin),
            "this dependency closes the cycle"
        );
        for (auto edge = first; edge != active_path.end(); ++edge) {
            const auto* declaration = catalog.symbol(*edge);
            if (declaration == nullptr) {
                invariant_violation("cycle path references an unknown declaration");
            }
            diagnostic.related(
                locate(source(declaration->module_id).source_id(), declaration->declaration_span),
                edge == first
                    ? std::format("cycle starts at declaration '{}'", declaration->name)
                    : std::format("cycle passes through declaration '{}'", declaration->name)
            );
        }
        source(requester_module_id).emit(diagnostic.build());
    }

    auto begin_resolution(
        const CatalogSymbol& declaration,
        ProgramModuleID requester_module_id,
        Span origin
    ) noexcept -> std::optional<bool> {
        auto& state = declaration_states[declaration.symbol_id.index()];
        if (const auto known = known_result(state)) {
            return *known;
        }
        if (state == ElaborationState::Resolving) {
            diagnose_cycle(declaration.symbol_id, requester_module_id, origin);
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
        ProgramModuleID requester_module_id,
        Span origin
    ) noexcept -> bool {
        if (std::holds_alternative<CatalogEnumCaseForm>(declaration.form)) {
            invariant_violation("enum case entered declaration-signature elaboration");
        }
        if (const auto known = begin_resolution(declaration, requester_module_id, origin)) {
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
        ProgramModuleID requester_module_id,
        Span origin
    ) noexcept -> bool {
        const auto* enum_case_form = std::get_if<CatalogEnumCaseForm>(&declaration.form);
        if (enum_case_form == nullptr) {
            invariant_violation("catalog enum case is missing its owner identity");
        }
        if (const auto known = begin_resolution(declaration, requester_module_id, origin)) {
            return *known;
        }
        const auto module_id = declaration.module_id;
        const auto owner = catalog.enum_symbol(enum_case_form->owner);
        const auto* owner_declaration = catalog.symbol(owner);
        if (owner_declaration == nullptr
            || !std::holds_alternative<CatalogEnumForm>(owner_declaration->form)) {
            invariant_violation("catalog enum case owner is not an enum declaration");
        }
        auto success = resolve(owner, module_id, declaration.declaration_span);
        if (success) {
            success = elaborate_enum_case(source(module_id), owner, enum_case_form->index);
        }
        return finish_resolution(declaration, success);
    }

    AnalysisCatalogView catalog;
    SemanticConstruction& hir_builder;
    std::span<ModuleAnalysis> active_modules;
    std::vector<ElaborationState> declaration_states;
    std::vector<SymbolID> active_path;
    std::vector<std::optional<FunctionContractState>> function_states;
    std::vector<std::optional<StructContractState>> struct_states;
    std::vector<std::optional<EnumContractState>> enum_states;
    std::vector<std::optional<EnumCaseContractState>> enum_case_states;
    std::optional<FrozenDeclarationTables> frozen_declarations;
    Phase phase = Phase::Resolving;
};

DeclarationSessionView::DeclarationSessionView(DeclarationSessionState& state) noexcept
    : session_state(std::addressof(state)) {}

auto DeclarationSessionView::function(FunctionID id) const noexcept -> FunctionContractView {
    return session_state->function(id);
}

auto DeclarationSessionView::structure(StructID id) const noexcept -> StructContractView {
    return session_state->structure(id);
}

auto DeclarationSessionView::enumeration(EnumID id) const noexcept -> EnumContractView {
    return session_state->enumeration(id);
}

auto DeclarationSessionView::enum_case(EnumCaseID id) const noexcept -> EnumCaseContractView {
    return session_state->enum_case(id);
}

auto DeclarationSessionView::enum_count() const noexcept -> std::size_t {
    return session_state->enum_count();
}

auto DeclarationSessionView::enum_case_count() const noexcept -> std::size_t {
    return session_state->enum_case_count();
}

auto DeclarationSessionView::supports_equality(HIRTypeID id) const noexcept -> bool {
    return session_state->supports_equality(id);
}

auto DeclarationSessionView::resolve(
    SymbolID symbol_id,
    ProgramModuleID requester_module_id,
    Span origin
) const noexcept -> bool {
    return session_state->resolve_from_view(symbol_id, requester_module_id, origin);
}

DeclarationDefinitionSink::DeclarationDefinitionSink(DeclarationSessionState& state) noexcept
    : session_state(std::addressof(state)) {}

auto DeclarationDefinitionSink::define(FunctionID id, FunctionContractState contract) noexcept
    -> void {
    session_state->define(id, std::move(contract));
}

auto DeclarationDefinitionSink::define(StructID id, StructContractState contract) noexcept -> void {
    session_state->define(id, std::move(contract));
}

auto DeclarationDefinitionSink::define(EnumID id, EnumContractState contract) noexcept -> void {
    session_state->define(id, std::move(contract));
}

auto DeclarationDefinitionSink::define(EnumCaseID id, EnumCaseContractState contract) noexcept
    -> void {
    session_state->define(id, std::move(contract));
}

ResolvedDeclarations::ResolvedDeclarations(std::unique_ptr<DeclarationSessionState> state) noexcept
    : session_state(std::move(state)) {}

ResolvedDeclarations::ResolvedDeclarations(ResolvedDeclarations&& other) noexcept = default;
ResolvedDeclarations::~ResolvedDeclarations() noexcept = default;

auto ResolvedDeclarations::operator=(ResolvedDeclarations&& other) noexcept
    -> ResolvedDeclarations& = default;

auto ResolvedDeclarations::view() const noexcept -> DeclarationSessionView {
    if (session_state == nullptr) {
        invariant_violation("resolved declaration view outlived its owner");
    }
    return DeclarationSessionView(*session_state);
}

auto ResolvedDeclarations::finish() && noexcept -> void {
    if (session_state == nullptr) {
        invariant_violation("resolved declarations were consumed more than once");
    }
    session_state->finish();
    session_state.reset();
}

DeclarationConstruction::DeclarationConstruction(
    AnalysisCatalogView catalog,
    SemanticConstruction& builder
) noexcept
    : session_state(std::make_unique<DeclarationSessionState>(catalog, builder)) {}

DeclarationConstruction::DeclarationConstruction(DeclarationConstruction&& other) noexcept =
    default;
DeclarationConstruction::~DeclarationConstruction() noexcept = default;

auto DeclarationConstruction::operator=(DeclarationConstruction&& other) noexcept
    -> DeclarationConstruction& = default;

auto DeclarationConstruction::view() noexcept -> DeclarationSessionView {
    if (session_state == nullptr) {
        invariant_violation("declaration construction view outlived its owner");
    }
    return DeclarationSessionView(*session_state);
}

auto DeclarationConstruction::resolve_all(std::span<ModuleAnalysis> modules) && noexcept
    -> ResolvedDeclarations {
    if (session_state == nullptr) {
        invariant_violation("declaration construction was consumed more than once");
    }
    session_state->resolve_all(modules);
    return ResolvedDeclarations(std::move(session_state));
}
