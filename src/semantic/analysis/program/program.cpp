module carven:semantic.analysis.program.impl;

import :semantic.analysis.program;
import :support.invariant;
import std;

BodyReservation::BodyReservation(BodyReservation&& other) noexcept
    : body_id(other.body_id),
      body_kind(other.body_kind),
      provenance_identity(other.provenance_identity),
      active(std::exchange(other.active, false)) {}

auto BodyReservation::id() const noexcept -> BodyID {
    if (!active) {
        invariant_violation("body reservation was inspected after consumption");
    }
    return body_id;
}

auto BodyReservation::kind() const noexcept -> BodyKind {
    if (!active) {
        invariant_violation("body reservation was inspected after consumption");
    }
    return body_kind;
}

auto BodyReservation::consume() noexcept -> Consumed {
    if (!active) {
        invariant_violation("body reservation was consumed more than once");
    }
    active = false;
    return Consumed {
        .id = body_id,
        .kind = body_kind,
        .provenance = provenance_identity,
    };
}

auto ProgramDraft::begin(SyntaxProgram&& syntax, DiagnosticSink& sink) noexcept -> ProgramDraft {
    return ProgramDraft(std::move(syntax).decompose(), sink);
}

ProgramDraft::ProgramDraft(SyntaxProgramParts parts, DiagnosticSink& sink) noexcept
    : program_identity(ProgramIdentity::fresh()),
      provenance_appender(std::move(parts.provenance)),
      analysis_diagnostics(sink),
      state(State::Declarations),
      storage(
          std::in_place_type<ConstructionStorage>,
          program_identity,
          provenance_appender.reader().identity(),
          std::move(parts.syntax_by_module),
          std::move(parts.resolved_import_graph)
      ) {}

ProgramDraft::ProgramDraft(ProgramDraft&& other) noexcept
    : program_identity(other.program_identity),
      provenance_appender(std::move(other.provenance_appender)),
      analysis_diagnostics(other.analysis_diagnostics),
      state(other.state),
      storage(std::move(other.storage)) {
    other.state = State::Sealed;
}

auto ProgramDraft::syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree& {
    require_not_failed("read syntax tree");
    if (!owns(module_id)) {
        invariant_violation("semantic syntax lookup used a foreign module identity");
    }
    return construction().syntax_by_module[module_id.index()];
}

auto ProgramDraft::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    require_not_failed("read syntax trees");
    return construction().syntax_by_module;
}

auto ProgramDraft::resolved_imports(ProgramModuleID module_id) const noexcept
    -> std::span<const ResolvedModuleImport> {
    require_not_failed("read resolved imports");
    if (!owns(module_id)) {
        invariant_violation("semantic import lookup used a foreign module identity");
    }
    return construction().resolved_import_graph[module_id.index()];
}

auto ProgramDraft::module_count() const noexcept -> std::size_t {
    require_not_failed("read module count");
    return provenance_appender.module_count();
}

auto ProgramDraft::provenance_module_at(std::size_t index) const noexcept -> ProgramModuleID {
    require_not_failed("read provenance module");
    return provenance_appender.reader().module_id_at(index);
}

auto ProgramDraft::intern_type(const CanonicalType& type) noexcept -> TypeID {
    require_construction_open("intern type");
    return construction().types.intern(type);
}

auto ProgramDraft::intern_builtin_type(BuiltinType type) noexcept -> TypeID {
    require_construction_open("intern builtin type");
    return construction().types.intern_builtin(type);
}

auto ProgramDraft::type_copy(TypeID type) const noexcept -> CanonicalType {
    require_not_failed("read type");
    return construction().types.copy(type);
}

auto ProgramDraft::intern_constant(ConstantFact fact) noexcept -> ConstantID {
    require_construction_open("intern constant");
    return construction().constants.intern(std::move(fact));
}

auto ProgramDraft::constant_copy(ConstantID constant) const noexcept -> ConstantFact {
    require_not_failed("read constant");
    return construction().constants.copy(constant);
}

auto ProgramDraft::intern_failure_set(std::vector<TypeID> members) noexcept -> FailureSetID {
    require_construction_open("intern failure set");
    return construction().failure_sets.intern(std::move(members));
}

auto ProgramDraft::empty_failure_set() noexcept -> FailureSetID {
    require_construction_open("intern empty failure set");
    return construction().failure_sets.empty_set();
}

auto ProgramDraft::append_construction_type(ConstructionType type) noexcept -> TypeTermID {
    require_construction_open("append construction type");
    return construction().construction_types.append(std::move(type));
}

auto ProgramDraft::construction_type_copy(TypeTermID type) const noexcept -> ConstructionType {
    require_construction_open("read construction type");
    return construction().construction_types.copy(type);
}

auto ProgramDraft::reserve_module_declaration() noexcept -> ModuleID {
    require_state(State::Declarations, "reserve module declaration");
    return construction().declarations.reserve_module();
}

auto ProgramDraft::reserve_function_declaration() noexcept -> FunctionID {
    require_state(State::Declarations, "reserve function declaration");
    return construction().declarations.reserve_function();
}

auto ProgramDraft::reserve_struct_declaration() noexcept -> StructID {
    require_state(State::Declarations, "reserve struct declaration");
    return construction().declarations.reserve_struct();
}

auto ProgramDraft::reserve_enum_declaration() noexcept -> EnumID {
    require_state(State::Declarations, "reserve enum declaration");
    return construction().declarations.reserve_enum();
}

auto ProgramDraft::reserve_enum_case_declaration() noexcept -> EnumCaseID {
    require_state(State::Declarations, "reserve enum-case declaration");
    return construction().declarations.reserve_enum_case();
}

auto ProgramDraft::reserve_module_constant_declaration() noexcept -> ModuleConstantID {
    require_state(State::Declarations, "reserve module-constant declaration");
    return construction().declarations.reserve_module_constant();
}

auto ProgramDraft::reserve_callable_declaration() noexcept -> CallableID {
    require_state(State::Declarations, "reserve callable declaration");
    return construction().declarations.reserve_callable();
}

auto ProgramDraft::define_declaration(ModuleID id, ModuleDeclaration declaration) noexcept -> void {
    require_state(State::Declarations, "define module declaration");
    construction().declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(FunctionID id, FunctionDeclaration declaration) noexcept
    -> void {
    require_state(State::Declarations, "define function declaration");
    construction().declarations.define(id, declaration);
}

auto ProgramDraft::define_declaration(
    StructID id,
    ConstructionStructDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define struct declaration");
    construction().declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(EnumID id, EnumDeclaration declaration) noexcept -> void {
    require_state(State::Declarations, "define enum declaration");
    construction().declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(
    EnumCaseID id,
    ConstructionEnumCaseDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define enum-case declaration");
    construction().declarations.define(id, std::move(declaration));
}

auto ProgramDraft::define_declaration(
    ModuleConstantID id,
    ModuleConstantDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define module-constant declaration");
    construction().declarations.define(id, declaration);
}

auto ProgramDraft::define_callable_contract(
    CallableID id,
    ConstructionCallableContract contract
) noexcept -> void {
    require_state(State::Declarations, "define callable contract");
    construction().declarations.define_callable_contract(id, std::move(contract));
}

auto ProgramDraft::define_pending_function_contract(
    CallableID id,
    PendingFunctionContract contract
) noexcept -> void {
    require_state(State::Declarations, "define pending function contract");
    if (id.owner() != program_identity
        || !construction().pending_function_contracts.emplace(id, std::move(contract)).second) {
        invariant_violation("invalid pending function contract reservation");
    }
}

auto ProgramDraft::pending_function_contract_copy(CallableID id) const noexcept
    -> std::optional<PendingFunctionContract> {
    require_declarations_available("read pending function contract");
    if (id.owner() != program_identity) {
        invariant_violation("pending function contract used a foreign callable");
    }
    const auto& pending = construction().pending_function_contracts;
    const auto found = pending.find(id);
    return found == pending.end() ? std::nullopt : std::optional(found->second);
}

auto ProgramDraft::complete_function_result(CallableID id, ConstructionTypeRef result) noexcept
    -> void {
    require_state(State::Bodies, "complete function result");
    auto& pending = construction().pending_function_contracts;
    const auto found = pending.find(id);
    if (found == pending.end()) {
        invariant_violation("function result completed without a pending contract");
    }
    construction().declarations.define_callable_contract(
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
    return construction().declarations.append_body_callable(std::move(contract));
}

auto ProgramDraft::complete_callable(CallableID id, CallableImplementation implementation) noexcept
    -> void {
    require_state(State::Bodies, "complete callable");
    construction().declarations.complete_callable(id, implementation);
}

auto ProgramDraft::module_declaration_copy(ModuleID id) const noexcept -> ModuleDeclaration {
    require_declarations_available("read module declaration");
    return construction().declarations.construction_view().module_decl(id);
}

auto ProgramDraft::function_declaration_copy(FunctionID id) const noexcept -> FunctionDeclaration {
    require_declarations_available("read function declaration");
    return construction().declarations.construction_view().function(id);
}

auto ProgramDraft::construction_struct_declaration_copy(StructID id) const noexcept
    -> ConstructionStructDeclaration {
    require_declarations_available("read construction struct declaration");
    return construction().declarations.construction_view().structure(id);
}

auto ProgramDraft::enum_declaration_copy(EnumID id) const noexcept -> EnumDeclaration {
    require_declarations_available("read construction enum declaration");
    return construction().declarations.construction_view().enumeration(id);
}

auto ProgramDraft::construction_enum_case_declaration_copy(EnumCaseID id) const noexcept
    -> ConstructionEnumCaseDeclaration {
    require_declarations_available("read construction enum-case declaration");
    return construction().declarations.construction_view().enum_case(id);
}

auto ProgramDraft::module_constant_declaration_copy(ModuleConstantID id) const noexcept
    -> ModuleConstantDeclaration {
    require_declarations_available("read construction module-constant declaration");
    return construction().declarations.construction_view().module_constant(id);
}

auto ProgramDraft::construction_callable_contract_copy(CallableID id) const noexcept
    -> ConstructionCallableContract {
    require_declarations_available("read construction callable contract");
    return construction().declarations.construction_view().callable_contract(id);
}

auto ProgramDraft::construction_failure_term_copy(FailureTermID failures) const noexcept
    -> FailureTerm {
    require_construction_open("read construction failure term");
    return construction().failure_constraints.copy(failures);
}

auto ProgramDraft::module_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read module declaration count");
    return construction().declarations.construction_view().module_count();
}

auto ProgramDraft::function_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read function declaration count");
    return construction().declarations.construction_view().function_count();
}

auto ProgramDraft::struct_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read struct declaration count");
    return construction().declarations.construction_view().struct_count();
}

auto ProgramDraft::enum_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read enum declaration count");
    return construction().declarations.construction_view().enum_count();
}

auto ProgramDraft::enum_case_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read enum-case declaration count");
    return construction().declarations.construction_view().enum_case_count();
}

auto ProgramDraft::module_constant_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read module-constant declaration count");
    return construction().declarations.construction_view().module_constant_count();
}

auto ProgramDraft::callable_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read callable declaration count");
    return construction().declarations.construction_view().callable_count();
}

auto ProgramDraft::module_declaration_ids() const noexcept -> std::vector<ModuleID> {
    require_declarations_available("read module declaration identities");
    return construction().declarations.construction_view().module_ids();
}

auto ProgramDraft::function_declaration_ids() const noexcept -> std::vector<FunctionID> {
    require_declarations_available("read function declaration identities");
    return construction().declarations.construction_view().function_ids();
}

auto ProgramDraft::struct_declaration_ids() const noexcept -> std::vector<StructID> {
    require_declarations_available("read struct declaration identities");
    return construction().declarations.construction_view().struct_ids();
}

auto ProgramDraft::enum_declaration_ids() const noexcept -> std::vector<EnumID> {
    require_declarations_available("read enum declaration identities");
    return construction().declarations.construction_view().enum_ids();
}

auto ProgramDraft::enum_case_declaration_ids() const noexcept -> std::vector<EnumCaseID> {
    require_declarations_available("read enum-case declaration identities");
    return construction().declarations.construction_view().enum_case_ids();
}

auto ProgramDraft::module_constant_declaration_ids() const noexcept
    -> std::vector<ModuleConstantID> {
    require_declarations_available("read module-constant declaration identities");
    return construction().declarations.construction_view().module_constant_ids();
}

auto ProgramDraft::callable_declaration_ids() const noexcept -> std::vector<CallableID> {
    require_declarations_available("read callable declaration identities");
    return construction().declarations.construction_view().callable_ids();
}

auto ProgramDraft::add_empty_failure_term() noexcept -> FailureTermID {
    require_construction_open("add empty failure term");
    return construction().failure_constraints.add_empty_term();
}

auto ProgramDraft::add_concrete_failure_term(std::vector<TypeID> members) noexcept
    -> FailureTermID {
    require_construction_open("add concrete failure term");
    return construction().failure_constraints.add_concrete_term(std::move(members));
}

auto ProgramDraft::add_union_failure_term(std::vector<FailureTermID> inputs) noexcept
    -> FailureTermID {
    require_construction_open("add union failure term");
    return construction().failure_constraints.add_union_term(std::move(inputs));
}

auto ProgramDraft::add_residual_failure_term(
    FailureTermID input,
    std::vector<TypeID> handled_members
) noexcept -> FailureTermID {
    require_construction_open("add residual failure term");
    return construction().failure_constraints.add_residual_term(input, std::move(handled_members));
}

auto ProgramDraft::add_intersection_failure_term(
    FailureTermID input,
    std::vector<TypeID> retained_members
) noexcept -> FailureTermID {
    require_construction_open("add intersection failure term");
    return construction().failure_constraints.add_intersection_term(
        input,
        std::move(retained_members)
    );
}

auto ProgramDraft::add_failure_member(FailureTermID destination, TypeID member) noexcept -> void {
    require_construction_open("add failure member");
    construction().failure_constraints.add_member(destination, member);
}

auto ProgramDraft::add_failure_contribution(
    FailureTermID destination,
    FailureTermID source
) noexcept -> void {
    require_construction_open("add failure contribution");
    construction().failure_constraints.add_contribution(destination, source);
}

auto ProgramDraft::add_guarded_failure_contribution(
    FailureTermID destination,
    FailureTermID gate,
    FailureTermID source
) noexcept -> void {
    require_construction_open("add guarded failure contribution");
    construction().failure_constraints.add_guarded_contribution(destination, gate, source);
}

auto ProgramDraft::equate_failures(FailureTermID left, FailureTermID right) noexcept -> void {
    require_construction_open("equate failure terms");
    construction().failure_constraints.equate(left, right);
}

auto ProgramDraft::require_empty_failures(
    FailureTermID term,
    ProgramOriginID origin,
    EmptyFailureRequirementKind kind
) noexcept -> void {
    require_construction_open("require empty failures");
    construction().failure_constraints.require_empty(term, origin, kind);
}

auto ProgramDraft::require_non_empty_failures(FailureTermID term, ProgramOriginID origin) noexcept
    -> void {
    require_construction_open("require non-empty failures");
    construction().failure_constraints.require_non_empty(term, origin);
}

auto ProgramDraft::require_failure_subset(
    FailureTermID actual,
    FailureTermID allowed,
    ProgramOriginID origin,
    FailureSubsetRequirementKind kind
) noexcept -> void {
    require_construction_open("require failure subset");
    construction().failure_constraints.require_subset(actual, allowed, origin, kind);
}

auto ProgramDraft::require_equal_failures(
    FailureTermID left,
    FailureTermID right,
    ProgramOriginID origin
) noexcept -> void {
    require_construction_open("require equal failures");
    construction().failure_constraints.require_equal(left, right, origin);
}

auto ProgramDraft::require_declared_failure_contract(
    FailureTermID actual,
    ProgramOriginID origin
) noexcept -> void {
    require_construction_open("require declared failure contract");
    construction().failure_constraints.require_declared_contract(actual, origin);
}

auto ProgramDraft::owns(ProgramModuleID id) const noexcept -> bool {
    require_not_failed("check module ownership");
    return provenance_appender.reader().contains(id);
}

auto ProgramDraft::owns(ProgramSpellingID id) const noexcept -> bool {
    require_not_failed("check spelling ownership");
    return provenance_appender.reader().contains(id);
}

auto ProgramDraft::owns(ProgramOriginID id) const noexcept -> bool {
    require_not_failed("check origin ownership");
    return provenance_appender.reader().contains(id);
}

auto ProgramDraft::source_span(ProgramOriginID id) const noexcept -> SourceSpan {
    require_not_failed("read source span");
    return provenance_appender.reader().source_span(id);
}

auto ProgramDraft::spelling_copy(ProgramSpellingID id) const noexcept -> std::string {
    require_not_failed("read spelling");
    return provenance_appender.reader().spelling_copy(id);
}

auto ProgramDraft::origin_slice_copy(ProgramOriginID id) const noexcept -> std::string {
    require_not_failed("read origin slice");
    return provenance_appender.reader().slice_copy(id);
}

auto ProgramDraft::module_source(ProgramModuleID id) const noexcept -> ProgramSourceID {
    require_not_failed("read module source");
    if (!owns(id)) {
        invariant_violation("semantic module source lookup used a foreign module");
    }
    return provenance_appender.reader().module_source(id);
}

auto ProgramDraft::module_path_copy(ProgramModuleID id) const noexcept -> CanonicalModulePath {
    require_not_failed("read module path");
    if (!owns(id)) {
        invariant_violation("semantic module path lookup used a foreign module");
    }
    return provenance_appender.reader().module_path_copy(id);
}

auto ProgramDraft::source_slice_copy(ProgramSourceID source, Span span) const noexcept
    -> std::string {
    require_not_failed("read source slice");
    const auto reader = provenance_appender.reader();
    if (!reader.contains(source)) {
        invariant_violation("semantic source slice used a foreign source");
    }
    return reader.source_slice_copy(source, span);
}

auto ProgramDraft::source_slice_copy(ProgramModuleID module_id, Span span) const noexcept
    -> std::string {
    return source_slice_copy(module_source(module_id), span);
}

auto ProgramDraft::intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID {
    require_construction_open("intern spelling");
    return provenance_appender.intern_spelling(spelling);
}

auto ProgramDraft::append_source_origin(ProgramSourceID source, Span span) noexcept
    -> ProgramOriginID {
    require_construction_open("append source origin");
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
    require_construction_open("append expansion origin");
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
    const auto heads = construction().declarations.finish_heads();
    auto pending_count = 0uz;
    for (const auto callable_id : heads.callable_ids()) {
        const auto pending = construction().pending_function_contracts.contains(callable_id);
        pending_count += pending;
        if (heads.callable_contract_defined(callable_id) == pending) {
            invariant_violation(
                "function head must have either a complete contract or a pending result"
            );
        }
    }
    if (pending_count != construction().pending_function_contracts.size()) {
        invariant_violation("pending function result has no reserved callable");
    }
    state = State::Bodies;
}

auto ProgramDraft::reserve_body(BodyKind kind) noexcept -> BodyReservation {
    require_state(State::Bodies, "reserve body");
    const auto id = construction().body_slots.reserve();
    if (static_cast<std::size_t>(id.index()) != construction().reserved_body_kinds.size()
        || construction().reserved_body_kinds.size() != construction().test_by_body.size()) {
        invariant_violation("body reservation metadata lost table alignment");
    }
    construction().reserved_body_kinds.push_back(kind);
    construction().test_by_body.emplace_back();
    return BodyReservation(id, kind, provenance_appender.reader().identity());
}

auto ProgramDraft::add_body_draft(StructuredBodyDraft body) noexcept -> void {
    require_state(State::Bodies, "add body draft");
    const auto id = body.id;
    if (!construction().body_slots.contains(id)
        || body.lifetime_regions.owner().program() != program_identity
        || body.provenance_identity != provenance_appender.reader().identity()
        || std::ranges::any_of(
            construction().body_drafts,
            [&](const StructuredBodyDraft& existing) noexcept { return existing.id == id; }
        )) {
        invariant_violation("body draft disagreed with its program reservation");
    }
    construction().body_drafts.push_back(std::move(body));
}

auto ProgramDraft::reserve_test() noexcept -> TestID {
    require_state(State::Declarations, "reserve test declaration");
    return construction().test_slots.reserve();
}

auto ProgramDraft::define_test(TestID id, TestDeclaration test) noexcept -> void {
    require_state(State::Bodies, "define test");
    if (test.module_id.owner() != program_identity
        || test.body.owner() != program_identity
        || test.name.owner() != provenance_appender.reader().identity()
        || test.origin.owner() != provenance_appender.reader().identity()) {
        invariant_violation("test declaration mixed semantic or provenance owners");
    }
    static_cast<void>(construction().declarations.construction_view().module_decl(test.module_id));
    if (!construction().body_slots.contains(test.body)
        || static_cast<std::size_t>(test.body.index()) >= construction().reserved_body_kinds.size()
        || construction().reserved_body_kinds.size() != construction().test_by_body.size()
        || construction().reserved_body_kinds[test.body.index()] != BodyKind::Test) {
        invariant_violation("test declaration used a non-test body reservation");
    }
    if (!construction().test_slots.contains(id) || construction().test_slots.is_defined(id)) {
        invariant_violation("test declaration used an invalid or defined reservation");
    }
    auto& assigned_test = construction().test_by_body[test.body.index()];
    if (assigned_test.has_value()) {
        invariant_violation("test body was assigned more than one test declaration");
    }
    assigned_test = id;
    construction().test_slots.define(id, test);
}

auto ProgramDraft::callable_signature(CallableID callable) const noexcept -> CallableSignatureID {
    require_state(State::Solved, "read callable signature");
    return final().declarations.callable(callable).signature;
}

auto ProgramDraft::callable_crosses_cpp_boundary(CallableID callable) const noexcept -> bool {
    require_state(State::Solved, "read callable implementation effect");
    if (callable.owner() != program_identity) {
        invariant_violation("callable effect lookup used a foreign callable");
    }
    return std::holds_alternative<CppImportImplementation>(
        final().declarations.callable(callable).implementation
    );
}

auto ProgramDraft::body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID> {
    require_declarations_available("find body for callable");
    return state == State::Solved
        ? final().declarations.body_for_callable(callable)
        : construction().declarations.construction_view().body_for_callable(callable);
}

auto ProgramDraft::callable_for_body(BodyID body) const noexcept -> std::optional<CallableID> {
    require_declarations_available("find callable for body");
    return state == State::Solved
        ? final().declarations.callable_for_body(body)
        : construction().declarations.construction_view().callable_for_body(body);
}

auto ProgramDraft::require_state(State expected, std::string_view operation) const noexcept
    -> void {
    if (state != expected) {
        invariant_violation(std::format("invalid compilation lifecycle operation: {}", operation));
    }
}

auto ProgramDraft::require_not_failed(std::string_view operation) const noexcept -> void {
    if (state == State::Failed || state == State::Sealed) {
        invariant_violation(std::format("cannot {} on inactive semantic builder", operation));
    }
}

auto ProgramDraft::require_construction_open(std::string_view operation) const noexcept -> void {
    if (state != State::Declarations && state != State::Bodies) {
        invariant_violation(std::format("cannot {} outside semantic construction", operation));
    }
}

auto ProgramDraft::require_declarations_available(std::string_view operation) const noexcept
    -> void {
    if (state != State::Bodies && state != State::Solved) {
        invariant_violation(
            std::format(
                "cannot {} before declaration resolution or after builder consumption",
                operation
            )
        );
    }
}

auto ProgramDraft::construction() noexcept -> ConstructionStorage& {
    if (!std::holds_alternative<ConstructionStorage>(storage)) {
        invariant_violation("construction storage is closed");
    }
    return std::get<ConstructionStorage>(storage);
}

auto ProgramDraft::construction() const noexcept -> const ConstructionStorage& {
    if (!std::holds_alternative<ConstructionStorage>(storage)) {
        invariant_violation("construction storage is closed");
    }
    return std::get<ConstructionStorage>(storage);
}

auto ProgramDraft::final() const noexcept -> const FinalStorage& {
    require_state(State::Solved, "read final semantic storage");
    return std::get<FinalStorage>(storage);
}

auto ProgramDraft::types() const noexcept -> const CanonicalTypeStore& {
    return final().types;
}

auto ProgramDraft::constants() const noexcept -> const ConstantStore& {
    return final().constants;
}

auto ProgramDraft::failure_sets() const noexcept -> const FailureSetStore& {
    return final().failure_sets;
}

auto ProgramDraft::callable_signatures() const noexcept -> const CallableSignatureStore& {
    return final().callable_signatures;
}

auto ProgramDraft::declarations() const noexcept -> const DeclarationStore& {
    return final().declarations;
}

auto ProgramDraft::bodies() const noexcept -> const BodyStore& {
    return final().bodies;
}

auto ProgramDraft::tests() const noexcept -> const TestStore& {
    return final().tests;
}
