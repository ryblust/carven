module carven:semantic.analysis.program.impl;

import :diagnostics.sink;
import :frontend.ast.tree;
import :frontend.program;
import :semantic.analysis.diagnostics;
import :semantic.analysis.failure;
import :semantic.analysis.program;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import std;

auto ProgramDraft::begin(SyntaxProgram&& syntax, DiagnosticSink& sink) noexcept -> ProgramDraft {
    return ProgramDraft(std::move(syntax).decompose(), sink);
}

ProgramDraft::ProgramDraft(SyntaxProgramParts parts, DiagnosticSink& sink) noexcept
    : program_identity(ProgramIdentity::fresh()),
      provenance_appender(std::move(parts.provenance)),
      analysis_diagnostics(sink),
      state(State::Declarations),
      storage(
          program_identity,
          provenance_appender.reader().identity(),
          std::move(parts.syntax_by_module),
          std::move(parts.resolved_import_graph)
      ) {}

auto ProgramDraft::syntax_tree(ProgramModuleID id) const noexcept -> const SyntaxTree& {
    if (!owns(id)) {
        invariant_violation("semantic syntax lookup used a foreign module identity");
    }
    return storage.syntax_by_module[id.index()];
}

auto ProgramDraft::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    return storage.syntax_by_module;
}

auto ProgramDraft::resolved_imports(ProgramModuleID id) const noexcept
    -> std::span<const ResolvedModuleImport> {
    if (!owns(id)) {
        invariant_violation("semantic import lookup used a foreign module identity");
    }
    return storage.resolved_import_graph[id.index()];
}

auto ProgramDraft::module_count() const noexcept -> std::size_t {
    return provenance_appender.module_count();
}

auto ProgramDraft::provenance_module_at(std::size_t index) const noexcept -> ProgramModuleID {
    return provenance_appender.reader().module_id_at(index);
}

auto ProgramDraft::intern_type(const CanonicalType& type) noexcept -> TypeID {
    return storage.types.intern(type);
}

auto ProgramDraft::intern_builtin_type(BuiltinType type) noexcept -> TypeID {
    return storage.types.intern_builtin(type);
}

auto ProgramDraft::canonicalize_declared_type(ConstructionTypeRef type) noexcept -> TypeID {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return *concrete;
    }
    return ConstructionTypeStore::canonicalize_type(
        construction_type_copy(std::get<TypeTermID>(type)),
        storage.types,
        storage.callable_signatures,
        [&](ConstructionTypeRef child) noexcept { return canonicalize_declared_type(child); },
        [&](FailureTermID term) noexcept {
            const auto failures = construction_failure_term_copy(term);
            if (!failures.inputs.empty()
                || !failures.guarded_inputs.empty()
                || !failures.excluded_members.empty()
                || failures.retained_members) {
                invariant_violation("declared type contains an inferred callable contract");
            }
            return intern_failure_set(failures.direct_members);
        }
    );
}

auto ProgramDraft::type_copy(TypeID type) const noexcept -> CanonicalType {
    return storage.types.copy(type);
}

auto ProgramDraft::intern_constant(ConstantFact fact) noexcept -> ConstantID {
    return storage.constants.intern(std::move(fact));
}

auto ProgramDraft::constant_copy(ConstantID constant) const noexcept -> ConstantFact {
    return storage.constants.copy(constant);
}

auto ProgramDraft::intern_failure_set(std::vector<TypeID> members) noexcept -> FailureSetID {
    return storage.failure_sets.intern(std::move(members));
}

auto ProgramDraft::empty_failure_set() noexcept -> FailureSetID {
    return storage.failure_sets.empty_set();
}

auto ProgramDraft::append_construction_type(ConstructionType type) noexcept -> TypeTermID {
    return storage.construction_types.append(std::move(type));
}

auto ProgramDraft::construction_type_copy(TypeTermID type) const noexcept -> ConstructionType {
    return storage.construction_types.copy(type);
}

auto ProgramDraft::reserve_module_declaration() noexcept -> ModuleID {
    require_state(State::Declarations, "reserve module declaration");
    return storage.declarations.reserve_module();
}

auto ProgramDraft::reserve_function_declaration() noexcept -> FunctionID {
    require_state(State::Declarations, "reserve function declaration");
    return storage.declarations.reserve_function();
}

auto ProgramDraft::reserve_struct_declaration() noexcept -> StructID {
    require_state(State::Declarations, "reserve struct declaration");
    return storage.declarations.reserve_struct();
}

auto ProgramDraft::reserve_enum_declaration() noexcept -> EnumID {
    require_state(State::Declarations, "reserve enum declaration");
    return storage.declarations.reserve_enum();
}

auto ProgramDraft::reserve_enum_case_declaration() noexcept -> EnumCaseID {
    require_state(State::Declarations, "reserve enum-case declaration");
    return storage.declarations.reserve_enum_case();
}

auto ProgramDraft::reserve_module_constant_declaration() noexcept -> ModuleConstantID {
    require_state(State::Declarations, "reserve module-constant declaration");
    return storage.declarations.reserve_module_constant();
}

auto ProgramDraft::reserve_callable_declaration() noexcept -> CallableID {
    require_state(State::Declarations, "reserve callable declaration");
    return storage.declarations.reserve_callable();
}

auto ProgramDraft::define_declaration(ModuleID id, ModuleDeclaration declaration) noexcept -> void {
    require_state(State::Declarations, "define module declaration");
    storage.declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(FunctionID id, FunctionDeclaration declaration) noexcept
    -> void {
    require_state(State::Declarations, "define function declaration");
    storage.declarations.define(id, declaration);
}

auto ProgramDraft::define_declaration(
    StructID id,
    ConstructionStructDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define struct declaration");
    storage.declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(EnumID id, EnumDeclaration declaration) noexcept -> void {
    require_state(State::Declarations, "define enum declaration");
    storage.declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(
    EnumCaseID id,
    ConstructionEnumCaseDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define enum-case declaration");
    storage.declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(
    ModuleConstantID id,
    ModuleConstantDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define module-constant declaration");
    storage.declarations.define(id, declaration);
}

auto ProgramDraft::define_callable_contract(
    CallableID id,
    ConstructionCallableContract contract
) noexcept -> void {
    require_state(State::Declarations, "define callable contract");
    storage.declarations.define_callable_contract(id, std::move(contract));
}

auto ProgramDraft::define_pending_function_contract(
    CallableID id,
    PendingFunctionContract contract
) noexcept -> void {
    require_state(State::Declarations, "define pending function contract");
    if (id.owner() != program_identity
        || !storage.pending_function_contracts.emplace(id, std::move(contract)).second) {
        invariant_violation("invalid pending function contract reservation");
    }
}

auto ProgramDraft::pending_function_contract_copy(CallableID id) const noexcept
    -> std::optional<PendingFunctionContract> {
    require_declarations_available("read pending function contract");
    if (id.owner() != program_identity) {
        invariant_violation("pending function contract used a foreign callable");
    }
    const auto& pending = storage.pending_function_contracts;
    const auto found = pending.find(id);
    return found == pending.end() ? std::nullopt : std::optional(found->second);
}

auto ProgramDraft::complete_function_result(CallableID id, ConstructionTypeRef result) noexcept
    -> void {
    require_state(State::Bodies, "complete function result");
    auto& pending = storage.pending_function_contracts;
    const auto found = pending.find(id);
    if (found == pending.end()) {
        invariant_violation("function result completed without a pending contract");
    }
    storage.declarations.define_callable_contract(
        id,
        {
            .parameters = std::move(found->second.parameters),
            .result = result,
            .failures = found->second.failures,
            .policy = found->second.policy,
        }
    );
    pending.erase(found);
}

auto ProgramDraft::append_body_callable(ConstructionCallableContract contract) noexcept
    -> CallableID {
    require_state(State::Bodies, "append body callable");
    return storage.declarations.append_body_callable(std::move(contract));
}

auto ProgramDraft::complete_callable(CallableID id, CallableImplementation implementation) noexcept
    -> void {
    require_state(State::Bodies, "complete callable");
    storage.declarations.complete_callable(id, implementation);
}

auto ProgramDraft::module_declaration_copy(ModuleID id) const noexcept -> ModuleDeclaration {
    require_declarations_available("read module declaration");
    return storage.declarations.construction_view().module_decl(id);
}

auto ProgramDraft::function_declaration_copy(FunctionID id) const noexcept -> FunctionDeclaration {
    require_declarations_available("read function declaration");
    return storage.declarations.construction_view().function(id);
}

auto ProgramDraft::construction_struct_declaration_copy(StructID id) const noexcept
    -> ConstructionStructDeclaration {
    require_declarations_available("read construction struct declaration");
    return storage.declarations.construction_view().structure(id);
}

auto ProgramDraft::enum_declaration_copy(EnumID id) const noexcept -> EnumDeclaration {
    require_declarations_available("read construction enum declaration");
    return storage.declarations.construction_view().enumeration(id);
}

auto ProgramDraft::construction_enum_case_declaration_copy(EnumCaseID id) const noexcept
    -> ConstructionEnumCaseDeclaration {
    require_declarations_available("read construction enum-case declaration");
    return storage.declarations.construction_view().enum_case(id);
}

auto ProgramDraft::module_constant_declaration_copy(ModuleConstantID id) const noexcept
    -> ModuleConstantDeclaration {
    require_declarations_available("read construction module-constant declaration");
    return storage.declarations.construction_view().module_constant(id);
}

auto ProgramDraft::construction_callable_contract_copy(CallableID id) const noexcept
    -> ConstructionCallableContract {
    require_declarations_available("read construction callable contract");
    return storage.declarations.construction_view().callable_contract(id);
}

auto ProgramDraft::construction_failure_term_copy(FailureTermID failures) const noexcept
    -> FailureTerm {
    return storage.failure_constraints.copy(failures);
}

auto ProgramDraft::module_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read module declaration count");
    return storage.declarations.construction_view().module_count();
}

auto ProgramDraft::function_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read function declaration count");
    return storage.declarations.construction_view().function_count();
}

auto ProgramDraft::struct_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read struct declaration count");
    return storage.declarations.construction_view().struct_count();
}

auto ProgramDraft::enum_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read enum declaration count");
    return storage.declarations.construction_view().enum_count();
}

auto ProgramDraft::enum_case_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read enum-case declaration count");
    return storage.declarations.construction_view().enum_case_count();
}

auto ProgramDraft::module_constant_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read module-constant declaration count");
    return storage.declarations.construction_view().module_constant_count();
}

auto ProgramDraft::callable_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read callable declaration count");
    return storage.declarations.construction_view().callable_count();
}

auto ProgramDraft::module_declaration_ids() const noexcept -> std::vector<ModuleID> {
    require_declarations_available("read module declaration identities");
    return storage.declarations.construction_view().module_ids();
}

auto ProgramDraft::function_declaration_ids() const noexcept -> std::vector<FunctionID> {
    require_declarations_available("read function declaration identities");
    return storage.declarations.construction_view().function_ids();
}

auto ProgramDraft::struct_declaration_ids() const noexcept -> std::vector<StructID> {
    require_declarations_available("read struct declaration identities");
    return storage.declarations.construction_view().struct_ids();
}

auto ProgramDraft::enum_declaration_ids() const noexcept -> std::vector<EnumID> {
    require_declarations_available("read enum declaration identities");
    return storage.declarations.construction_view().enum_ids();
}

auto ProgramDraft::enum_case_declaration_ids() const noexcept -> std::vector<EnumCaseID> {
    require_declarations_available("read enum-case declaration identities");
    return storage.declarations.construction_view().enum_case_ids();
}

auto ProgramDraft::module_constant_declaration_ids() const noexcept
    -> std::vector<ModuleConstantID> {
    require_declarations_available("read module-constant declaration identities");
    return storage.declarations.construction_view().module_constant_ids();
}

auto ProgramDraft::callable_declaration_ids() const noexcept -> std::vector<CallableID> {
    require_declarations_available("read callable declaration identities");
    return storage.declarations.construction_view().callable_ids();
}

auto ProgramDraft::add_empty_failure_term() noexcept -> FailureTermID {
    return storage.failure_constraints.add_empty_term();
}

auto ProgramDraft::add_concrete_failure_term(std::vector<TypeID> members) noexcept
    -> FailureTermID {
    return storage.failure_constraints.add_concrete_term(std::move(members));
}

auto ProgramDraft::add_union_failure_term(std::vector<FailureTermID> inputs) noexcept
    -> FailureTermID {
    return storage.failure_constraints.add_union_term(std::move(inputs));
}

auto ProgramDraft::add_residual_failure_term(
    FailureTermID input,
    std::vector<TypeID> handled_members
) noexcept -> FailureTermID {
    return storage.failure_constraints.add_residual_term(input, std::move(handled_members));
}

auto ProgramDraft::add_intersection_failure_term(
    FailureTermID input,
    std::vector<TypeID> retained_members
) noexcept -> FailureTermID {
    return storage.failure_constraints.add_intersection_term(input, std::move(retained_members));
}

auto ProgramDraft::add_failure_member(FailureTermID destination, TypeID member) noexcept -> void {
    storage.failure_constraints.add_member(destination, member);
}

auto ProgramDraft::add_failure_contribution(
    FailureTermID destination,
    FailureTermID source
) noexcept -> void {
    storage.failure_constraints.add_contribution(destination, source);
}

auto ProgramDraft::add_guarded_failure_contribution(
    FailureTermID destination,
    FailureTermID gate,
    FailureTermID source
) noexcept -> void {
    storage.failure_constraints.add_guarded_contribution(destination, gate, source);
}

auto ProgramDraft::equate_failures(FailureTermID left, FailureTermID right) noexcept -> void {
    storage.failure_constraints.equate(left, right);
}

auto ProgramDraft::require_empty_failures(
    FailureTermID term,
    ProgramOriginID origin,
    EmptyFailureRequirementKind kind
) noexcept -> void {
    storage.failure_constraints.require_empty(term, origin, kind);
}

auto ProgramDraft::require_non_empty_failures(FailureTermID term, ProgramOriginID origin) noexcept
    -> void {
    storage.failure_constraints.require_non_empty(term, origin);
}

auto ProgramDraft::require_failure_subset(
    FailureTermID actual,
    FailureTermID allowed,
    ProgramOriginID origin,
    FailureSubsetRequirementKind kind
) noexcept -> void {
    storage.failure_constraints.require_subset(actual, allowed, origin, kind);
}

auto ProgramDraft::require_equal_failures(
    FailureTermID left,
    FailureTermID right,
    ProgramOriginID origin
) noexcept -> void {
    storage.failure_constraints.require_equal(left, right, origin);
}

auto ProgramDraft::require_declared_failure_contract(
    FailureTermID actual,
    ProgramOriginID origin
) noexcept -> void {
    storage.failure_constraints.require_declared_contract(actual, origin);
}

auto ProgramDraft::owns(ProgramModuleID id) const noexcept -> bool {
    return provenance_appender.reader().contains(id);
}

auto ProgramDraft::owns(ProgramSpellingID id) const noexcept -> bool {
    return provenance_appender.reader().contains(id);
}

auto ProgramDraft::owns(ProgramOriginID id) const noexcept -> bool {
    return provenance_appender.reader().contains(id);
}

auto ProgramDraft::source_span(ProgramOriginID id) const noexcept -> SourceSpan {
    return provenance_appender.reader().source_span(id);
}

auto ProgramDraft::spelling_copy(ProgramSpellingID id) const noexcept -> std::string {
    return provenance_appender.reader().spelling_copy(id);
}

auto ProgramDraft::origin_slice_copy(ProgramOriginID id) const noexcept -> std::string {
    return provenance_appender.reader().slice_copy(id);
}

auto ProgramDraft::module_source(ProgramModuleID id) const noexcept -> ProgramSourceID {
    if (!owns(id)) {
        invariant_violation("semantic module source lookup used a foreign module");
    }
    return provenance_appender.reader().module_source(id);
}

auto ProgramDraft::module_path_copy(ProgramModuleID id) const noexcept -> CanonicalModulePath {
    if (!owns(id)) {
        invariant_violation("semantic module path lookup used a foreign module");
    }
    return provenance_appender.reader().module_path_copy(id);
}

auto ProgramDraft::source_slice_copy(ProgramSourceID source, Span span) const noexcept
    -> std::string {
    const auto reader = provenance_appender.reader();
    if (!reader.contains(source)) {
        invariant_violation("semantic source slice used a foreign source");
    }
    return reader.source_slice_copy(source, span);
}

auto ProgramDraft::source_slice_copy(ProgramModuleID id, Span span) const noexcept -> std::string {
    return source_slice_copy(module_source(id), span);
}

auto ProgramDraft::intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID {
    return provenance_appender.intern_spelling(spelling);
}

auto ProgramDraft::append_source_origin(ProgramSourceID source, Span span) noexcept
    -> ProgramOriginID {
    if (!provenance_appender.reader().contains(source)) {
        invariant_violation("semantic source origin used a foreign source");
    }
    return provenance_appender.append_origin(
        ProgramOrigin {
            .value = ProgramSourceOrigin {
                .source_id = source,
                .span = span,
            },
        }
    );
}

auto ProgramDraft::append_expansion_origin(
    ProgramOriginID parent,
    ProgramExpansionReason reason
) noexcept -> ProgramOriginID {
    if (!owns(parent)) {
        invariant_violation("semantic expansion used a foreign parent origin");
    }
    return provenance_appender.append_origin(
        ProgramOrigin {
            .value = ProgramExpansionOrigin {
                .parent_origin_id = parent,
                .reason = reason,
            },
        }
    );
}

auto ProgramDraft::finish_declaration_heads() noexcept -> void {
    require_state(State::Declarations, "finish declaration heads");
    const auto heads = storage.declarations.finish_heads();
    auto pending_count = 0uz;
    for (const auto callable_id : heads.callable_ids()) {
        const auto pending = storage.pending_function_contracts.contains(callable_id);
        pending_count += pending;
        if (heads.callable_contract_defined(callable_id) == pending) {
            invariant_violation(
                "function head must have either a complete contract or a pending result"
            );
        }
    }
    if (pending_count != storage.pending_function_contracts.size()) {
        invariant_violation("pending function result has no reserved callable");
    }
    state = State::Bodies;
}

auto ProgramDraft::reserve_body(BodyKind kind) noexcept -> BodyReservation {
    require_state(State::Bodies, "reserve body");
    if (storage.bodies.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("body reservations exhausted their 32-bit identity space");
    }
    const auto id = BodyID(program_identity, static_cast<std::uint32_t>(storage.bodies.size()));
    storage.bodies.push_back({kind, std::nullopt, std::nullopt});
    return BodyReservation(id, kind, provenance_appender.reader().identity());
}

auto ProgramDraft::add_body_draft(StructuredBodyDraft body) noexcept -> void {
    require_state(State::Bodies, "add body draft");
    const auto id = body.id;
    if (id.owner() != program_identity
        || id.index() >= storage.bodies.size()
        || body.lifetime_regions.owner().program() != program_identity
        || body.provenance_identity != provenance_appender.reader().identity()
        || storage.bodies[id.index()].definition.has_value()) {
        invariant_violation("body draft disagreed with its program reservation");
    }
    storage.bodies[id.index()].definition.emplace(std::move(body));
}

auto ProgramDraft::reserve_test() noexcept -> TestID {
    require_state(State::Declarations, "reserve test declaration");
    return storage.test_slots.reserve();
}

auto ProgramDraft::define_test(TestID id, TestDeclaration test) noexcept -> void {
    require_state(State::Bodies, "define test");
    if (test.module_id.owner() != program_identity
        || test.body.owner() != program_identity
        || test.name.owner() != provenance_appender.reader().identity()
        || test.origin.owner() != provenance_appender.reader().identity()) {
        invariant_violation("test declaration mixed semantic or provenance owners");
    }
    static_cast<void>(storage.declarations.construction_view().module_decl(test.module_id));
    if (test.body.index() >= storage.bodies.size()
        || storage.bodies[test.body.index()].kind != BodyKind::Test) {
        invariant_violation("test declaration used a non-test body reservation");
    }
    if (!storage.test_slots.contains(id) || storage.test_slots.is_defined(id)) {
        invariant_violation("test declaration used an invalid or defined reservation");
    }
    auto& assigned_test = storage.bodies[test.body.index()].test;
    if (assigned_test.has_value()) {
        invariant_violation("test body was assigned more than one test declaration");
    }
    assigned_test = id;
    storage.test_slots.define(id, test);
}

auto ProgramDraft::require_state(State expected, std::string_view operation) const noexcept
    -> void {
    if (state != expected) {
        invariant_violation(std::format("invalid compilation lifecycle operation: {}", operation));
    }
}

auto ProgramDraft::require_declarations_available(std::string_view operation) const noexcept
    -> void {
    require_state(State::Bodies, operation);
}

auto BodyReservation::id() const noexcept -> BodyID {
    return body_id;
}

auto BodyReservation::kind() const noexcept -> BodyKind {
    return body_kind;
}

BodyReservation::BodyReservation(BodyID id, BodyKind kind, ProvenanceIdentity provenance) noexcept
    : body_id(id),
      body_kind(kind),
      provenance_identity(provenance) {}

auto ProgramDraft::identity() const noexcept -> ProgramIdentity {
    return program_identity;
}

auto ProgramDraft::provenance_identity() const noexcept -> ProvenanceIdentity {
    return provenance_appender.reader().identity();
}

auto ProgramDraft::diagnostics() const noexcept -> AnalysisDiagnostics {
    return analysis_diagnostics;
}

ProgramDraft::ConstructionStorage::ConstructionStorage(
    ProgramIdentity identity,
    ProvenanceIdentity provenance,
    std::vector<SyntaxTree> syntax,
    ResolvedModuleImportGraph imports
) noexcept
    : syntax_by_module(std::move(syntax)),
      resolved_import_graph(std::move(imports)),
      types(identity),
      constants(identity, provenance),
      failure_sets(identity),
      callable_signatures(identity),
      construction_types(identity),
      declarations(identity, provenance),
      failure_constraints(identity, provenance),
      test_slots(identity) {}
