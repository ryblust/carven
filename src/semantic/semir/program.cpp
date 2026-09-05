module carven:semantic.semir.program.impl;

import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace {

template<typename TypeLookup, typename StructFieldsLookup, typename EnumCasesLookup>
auto compute_type_inhabitance(
    ProgramIdentity owner,
    TypeID root,
    TypeLookup type_lookup,
    StructFieldsLookup struct_fields_lookup,
    EnumCasesLookup enum_cases_lookup
) noexcept -> bool {
    if (root.owner() != owner) {
        invariant_violation("type inhabitance query used a foreign semantic type");
    }

    enum class VisitState : std::uint8_t {
        Visiting,
        Inhabited,
        Uninhabited,
    };
    auto states = std::unordered_map<std::uint32_t, VisitState>();
    return [&](this auto&& inspect, TypeID type) noexcept -> bool {
        if (type.owner() != owner) {
            invariant_violation("type inhabitance traversal crossed semantic program owners");
        }
        if (const auto found = states.find(type.index()); found != states.end()) {
            if (found->second == VisitState::Visiting) {
                invariant_violation("type inhabitance observed recursive by-value containment");
            }
            return found->second == VisitState::Inhabited;
        }
        states.emplace(type.index(), VisitState::Visiting);
        const auto inhabited = std::visit(
            Overloaded {
                [](const BuiltinTypeValue&) static noexcept { return true; },
                [&](const StructTypeValue& value) noexcept {
                    const auto fields = std::invoke(struct_fields_lookup, value.structure);
                    return std::ranges::all_of(fields, inspect);
                },
                [&](const EnumTypeValue& value) noexcept {
                    const auto cases = std::invoke(enum_cases_lookup, value.enumeration);
                    return std::ranges::any_of(cases, [&](const auto& payload) noexcept {
                        return std::ranges::all_of(payload, inspect);
                    });
                },
                [&](const ArrayTypeValue& value) noexcept {
                    return value.extent == 0u || inspect(value.element);
                },
                [](const FunctionTypeValue&) static noexcept { return true; },
                [](const ClosureTypeValue&) static noexcept { return true; },
                [](const CallableViewTypeValue&) static noexcept { return true; },
            },
            std::invoke(type_lookup, type).value
        );
        states[type.index()] = inhabited ? VisitState::Inhabited : VisitState::Uninhabited;
        return inhabited;
    }(root);
}

} // namespace

SemIRProgram::SemIRProgram(
    ProgramIdentity identity,
    CompilationProvenance provenance,
    CanonicalTypeStore types,
    ConstantStore constants,
    FailureSetStore failure_sets,
    CallableSignatureStore callable_signatures,
    DeclarationStore declarations,
    BodyStore bodies,
    TestStore tests
) noexcept
    : program_identity(identity),
      compilation_provenance(std::move(provenance)),
      type_store(std::move(types)),
      constant_store(std::move(constants)),
      failure_set_store(std::move(failure_sets)),
      callable_signature_store(std::move(callable_signatures)),
      declaration_store(std::move(declarations)),
      body_store(std::move(bodies)),
      test_store(std::move(tests)),
      active(true) {}

SemIRProgram::SemIRProgram(SemIRProgram&& other) noexcept
    : program_identity(other.program_identity),
      compilation_provenance(std::move(other.compilation_provenance)),
      type_store(std::move(other.type_store)),
      constant_store(std::move(other.constant_store)),
      failure_set_store(std::move(other.failure_set_store)),
      callable_signature_store(std::move(other.callable_signature_store)),
      declaration_store(std::move(other.declaration_store)),
      body_store(std::move(other.body_store)),
      test_store(std::move(other.test_store)),
      active(std::exchange(other.active, false)) {
    if (!active) {
        invariant_violation("inactive semantic program was moved");
    }
}

auto SemIRProgram::operator=(SemIRProgram&& other) noexcept -> SemIRProgram& {
    if (this == std::addressof(other)) {
        invariant_violation("semantic program was moved into itself");
    }
    other.require_active();
    program_identity = other.program_identity;
    compilation_provenance = std::move(other.compilation_provenance);
    type_store = std::move(other.type_store);
    constant_store = std::move(other.constant_store);
    failure_set_store = std::move(other.failure_set_store);
    callable_signature_store = std::move(other.callable_signature_store);
    declaration_store = std::move(other.declaration_store);
    body_store = std::move(other.body_store);
    test_store = std::move(other.test_store);
    active = true;
    other.active = false;
    return *this;
}

auto SemIRProgram::require_active() const noexcept -> void {
    if (!active) {
        invariant_violation("semantic program was used after consumption");
    }
}

auto SemIRProgram::body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID> {
    require_active();
    const auto body = declaration_store.body_for_callable(callable);
    if (body.has_value() && !body_store.contains(*body)) {
        invariant_violation("callable referred to an unpublished body");
    }
    return body;
}
auto SemIRProgram::callable_for_body(BodyID body) const noexcept -> std::optional<CallableID> {
    require_active();
    if (!body_store.contains(body)) {
        invariant_violation("callable lookup used a foreign or unpublished body");
    }
    return declaration_store.callable_for_body(body);
}

auto SemIRProgram::is_inhabited(TypeID type) const noexcept -> bool {
    require_active();
    return compute_type_inhabitance(
        program_identity,
        type,
        [&](TypeID id) noexcept { return type_store.type(id); },
        [&](StructID id) noexcept {
            const auto& declaration = declaration_store.structure(id);
            auto fields = std::vector<TypeID>();
            fields.reserve(declaration.fields.size());
            for (const auto& field : declaration.fields) {
                fields.push_back(field.type);
            }
            return fields;
        },
        [&](EnumID id) noexcept {
            const auto& declaration = declaration_store.enumeration(id);
            auto cases = std::vector<std::vector<TypeID>>();
            cases.reserve(declaration.cases.size());
            for (const auto case_id : declaration.cases) {
                cases.push_back(declaration_store.enum_case(case_id).payload_types);
            }
            return cases;
        }
    );
}

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
      syntax_by_module(std::move(parts.syntax_by_module)),
      resolved_import_graph(std::move(parts.resolved_import_graph)),
      analysis_diagnostics(sink),
      state(State::Declarations),
      types(program_identity),
      constants(program_identity, provenance_appender.reader().identity()),
      failure_sets(program_identity),
      callable_signatures(program_identity),
      construction_types(program_identity),
      declarations(program_identity, provenance_appender.reader().identity()),
      failure_constraints(program_identity, provenance_appender.reader().identity()),
      body_drafts(),
      body_slots(program_identity),
      test_slots(program_identity),
      reserved_body_kinds(),
      test_by_body(),
      solved_failures(),
      resolved_types() {}

ProgramDraft::ProgramDraft(ProgramDraft&& other) noexcept
    : program_identity(other.program_identity),
      provenance_appender(std::move(other.provenance_appender)),
      syntax_by_module(std::move(other.syntax_by_module)),
      resolved_import_graph(std::move(other.resolved_import_graph)),
      analysis_diagnostics(other.analysis_diagnostics),
      state(other.state),
      types(std::move(other.types)),
      constants(std::move(other.constants)),
      failure_sets(std::move(other.failure_sets)),
      callable_signatures(std::move(other.callable_signatures)),
      construction_types(std::move(other.construction_types)),
      declarations(std::move(other.declarations)),
      failure_constraints(std::move(other.failure_constraints)),
      body_drafts(std::move(other.body_drafts)),
      body_slots(std::move(other.body_slots)),
      test_slots(std::move(other.test_slots)),
      reserved_body_kinds(std::move(other.reserved_body_kinds)),
      test_by_body(std::move(other.test_by_body)),
      solved_failures(std::move(other.solved_failures)),
      resolved_types(std::move(other.resolved_types)) {
    other.state = State::Sealed;
}

auto ProgramDraft::syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree& {
    require_not_failed("read syntax tree");
    if (!owns(module_id)) {
        invariant_violation("semantic syntax lookup used a foreign module identity");
    }
    return syntax_by_module[module_id.index()];
}
auto ProgramDraft::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    require_not_failed("read syntax trees");
    return syntax_by_module;
}
auto ProgramDraft::resolved_imports(ProgramModuleID module_id) const noexcept
    -> std::span<const ResolvedModuleImport> {
    require_not_failed("read resolved imports");
    if (!owns(module_id)) {
        invariant_violation("semantic import lookup used a foreign module identity");
    }
    return resolved_import_graph[module_id.index()];
}
auto ProgramDraft::module_count() const noexcept -> std::size_t {
    require_not_failed("read module count");
    return syntax_by_module.size();
}
auto ProgramDraft::provenance_module_at(std::size_t index) const noexcept -> ProgramModuleID {
    require_not_failed("read provenance module");
    return provenance_appender.reader().module_id_at(index);
}

auto ProgramDraft::intern_type(CanonicalType type) noexcept -> TypeID {
    require_construction_open("intern type");
    return types.intern(type);
}
auto ProgramDraft::intern_builtin_type(BuiltinType type) noexcept -> TypeID {
    require_not_failed("intern builtin type");
    return types.intern_builtin(type);
}
auto ProgramDraft::type_copy(TypeID type) const noexcept -> CanonicalType {
    require_not_failed("read type");
    return types.copy(type);
}
auto ProgramDraft::intern_constant(ConstantFact fact) noexcept -> ConstantID {
    require_construction_open("intern constant");
    return constants.intern(std::move(fact));
}
auto ProgramDraft::constant_copy(ConstantID constant) const noexcept -> ConstantFact {
    require_not_failed("read constant");
    return constants.copy(constant);
}
auto ProgramDraft::intern_failure_set(std::vector<TypeID> members) noexcept -> FailureSetID {
    require_not_failed("intern failure set");
    return failure_sets.intern(std::move(members));
}
auto ProgramDraft::empty_failure_set() noexcept -> FailureSetID {
    require_not_failed("intern empty failure set");
    return failure_sets.empty_set();
}
auto ProgramDraft::append_construction_type(ConstructionType type) noexcept -> TypeTermID {
    require_construction_open("append construction type");
    return construction_types.append(std::move(type));
}
auto ProgramDraft::construction_type_copy(TypeTermID type) const noexcept -> ConstructionType {
    require_construction_open("read construction type");
    return construction_types.copy(type);
}

auto ProgramDraft::reserve_module_declaration() noexcept -> ModuleID {
    require_state(State::Declarations, "reserve module declaration");
    return declarations.reserve_module();
}
auto ProgramDraft::reserve_function_declaration() noexcept -> FunctionID {
    require_state(State::Declarations, "reserve function declaration");
    return declarations.reserve_function();
}
auto ProgramDraft::reserve_struct_declaration() noexcept -> StructID {
    require_state(State::Declarations, "reserve struct declaration");
    return declarations.reserve_struct();
}
auto ProgramDraft::reserve_enum_declaration() noexcept -> EnumID {
    require_state(State::Declarations, "reserve enum declaration");
    return declarations.reserve_enum();
}
auto ProgramDraft::reserve_enum_case_declaration() noexcept -> EnumCaseID {
    require_state(State::Declarations, "reserve enum-case declaration");
    return declarations.reserve_enum_case();
}
auto ProgramDraft::reserve_module_constant_declaration() noexcept -> ModuleConstantID {
    require_state(State::Declarations, "reserve module-constant declaration");
    return declarations.reserve_module_constant();
}
auto ProgramDraft::reserve_callable_declaration() noexcept -> CallableID {
    require_state(State::Declarations, "reserve callable declaration");
    return declarations.reserve_callable();
}
auto ProgramDraft::define_declaration(ModuleID id, ModuleDeclaration declaration) noexcept -> void {
    require_state(State::Declarations, "define module declaration");
    declarations.define(id, std::move(declaration));
}
auto ProgramDraft::define_declaration(FunctionID id, FunctionDeclaration declaration) noexcept
    -> void {
    require_state(State::Declarations, "define function declaration");
    declarations.define(id, declaration);
}
auto ProgramDraft::define_declaration(
    StructID id,
    ConstructionStructDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define struct declaration");
    declarations.define(id, std::move(declaration));
}
auto ProgramDraft::define_declaration(EnumID id, ConstructionEnumDeclaration declaration) noexcept
    -> void {
    require_state(State::Declarations, "define enum declaration");
    declarations.define(id, std::move(declaration));
}
auto ProgramDraft::define_declaration(
    EnumCaseID id,
    ConstructionEnumCaseDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define enum-case declaration");
    declarations.define(id, std::move(declaration));
}
auto ProgramDraft::define_declaration(
    ModuleConstantID id,
    ConstructionModuleConstantDeclaration declaration
) noexcept -> void {
    require_state(State::Declarations, "define module-constant declaration");
    declarations.define(id, declaration);
}
auto ProgramDraft::define_callable_contract(
    CallableID id,
    ConstructionCallableContract contract
) noexcept -> void {
    require_state(State::Declarations, "define callable contract");
    declarations.define_callable_contract(id, std::move(contract));
}
auto ProgramDraft::append_body_callable(ConstructionCallableContract contract) noexcept
    -> CallableID {
    require_state(State::Bodies, "append body callable");
    return declarations.append_body_callable(std::move(contract));
}
auto ProgramDraft::complete_callable(CallableID id, CallableImplementation implementation) noexcept
    -> void {
    require_state(State::Bodies, "complete callable");
    declarations.complete_callable(id, implementation);
}

auto ProgramDraft::module_declaration_copy(ModuleID id) const noexcept -> ModuleDeclaration {
    require_declarations_available("read module declaration");
    return declarations.resolved_view().module_decl(id);
}
auto ProgramDraft::function_declaration_copy(FunctionID id) const noexcept -> FunctionDeclaration {
    require_declarations_available("read function declaration");
    return declarations.resolved_view().function(id);
}
auto ProgramDraft::construction_struct_declaration_copy(StructID id) const noexcept
    -> ConstructionStructDeclaration {
    require_declarations_available("read construction struct declaration");
    return declarations.resolved_view().structure(id);
}
auto ProgramDraft::construction_enum_declaration_copy(EnumID id) const noexcept
    -> ConstructionEnumDeclaration {
    require_declarations_available("read construction enum declaration");
    return declarations.resolved_view().enumeration(id);
}
auto ProgramDraft::construction_enum_case_declaration_copy(EnumCaseID id) const noexcept
    -> ConstructionEnumCaseDeclaration {
    require_declarations_available("read construction enum-case declaration");
    return declarations.resolved_view().enum_case(id);
}
auto ProgramDraft::construction_module_constant_declaration_copy(ModuleConstantID id) const noexcept
    -> ConstructionModuleConstantDeclaration {
    require_declarations_available("read construction module-constant declaration");
    return declarations.resolved_view().module_constant(id);
}
auto ProgramDraft::construction_callable_contract_copy(CallableID id) const noexcept
    -> ConstructionCallableContract {
    require_declarations_available("read construction callable contract");
    return declarations.resolved_view().callable_contract(id);
}
auto ProgramDraft::construction_failure_set_copy(FailureSetID failures) const noexcept
    -> FailureSet {
    require_declarations_available("read construction failure set");
    return failure_sets.copy(failures);
}

auto ProgramDraft::construction_failure_term_copy(FailureTermID failures) const noexcept
    -> FailureTerm {
    require_construction_open("read construction failure term");
    return failure_constraints.copy(failures);
}
auto ProgramDraft::module_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read module declaration count");
    return declarations.resolved_view().module_count();
}
auto ProgramDraft::function_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read function declaration count");
    return declarations.resolved_view().function_count();
}
auto ProgramDraft::struct_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read struct declaration count");
    return declarations.resolved_view().struct_count();
}
auto ProgramDraft::enum_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read enum declaration count");
    return declarations.resolved_view().enum_count();
}
auto ProgramDraft::enum_case_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read enum-case declaration count");
    return declarations.resolved_view().enum_case_count();
}
auto ProgramDraft::module_constant_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read module-constant declaration count");
    return declarations.resolved_view().module_constant_count();
}
auto ProgramDraft::callable_declaration_count() const noexcept -> std::size_t {
    require_declarations_available("read callable declaration count");
    return declarations.resolved_view().callable_count();
}
auto ProgramDraft::module_declaration_ids() const noexcept -> std::vector<ModuleID> {
    require_declarations_available("read module declaration identities");
    return declarations.resolved_view().module_ids();
}
auto ProgramDraft::function_declaration_ids() const noexcept -> std::vector<FunctionID> {
    require_declarations_available("read function declaration identities");
    return declarations.resolved_view().function_ids();
}
auto ProgramDraft::struct_declaration_ids() const noexcept -> std::vector<StructID> {
    require_declarations_available("read struct declaration identities");
    return declarations.resolved_view().struct_ids();
}
auto ProgramDraft::enum_declaration_ids() const noexcept -> std::vector<EnumID> {
    require_declarations_available("read enum declaration identities");
    return declarations.resolved_view().enum_ids();
}
auto ProgramDraft::enum_case_declaration_ids() const noexcept -> std::vector<EnumCaseID> {
    require_declarations_available("read enum-case declaration identities");
    return declarations.resolved_view().enum_case_ids();
}
auto ProgramDraft::module_constant_declaration_ids() const noexcept
    -> std::vector<ModuleConstantID> {
    require_declarations_available("read module-constant declaration identities");
    return declarations.resolved_view().module_constant_ids();
}
auto ProgramDraft::callable_declaration_ids() const noexcept -> std::vector<CallableID> {
    require_declarations_available("read callable declaration identities");
    return declarations.resolved_view().callable_ids();
}

auto ProgramDraft::add_empty_failure_term() noexcept -> FailureTermID {
    require_construction_open("add empty failure term");
    return failure_constraints.add_empty_term();
}
auto ProgramDraft::add_concrete_failure_term(std::vector<TypeID> members) noexcept
    -> FailureTermID {
    require_construction_open("add concrete failure term");
    return failure_constraints.add_concrete_term(std::move(members));
}
auto ProgramDraft::add_union_failure_term(std::vector<FailureTermID> inputs) noexcept
    -> FailureTermID {
    require_construction_open("add union failure term");
    return failure_constraints.add_union_term(std::move(inputs));
}
auto ProgramDraft::add_residual_failure_term(
    FailureTermID input,
    std::vector<TypeID> handled_members
) noexcept -> FailureTermID {
    require_construction_open("add residual failure term");
    return failure_constraints.add_residual_term(input, std::move(handled_members));
}
auto ProgramDraft::add_intersection_failure_term(
    FailureTermID input,
    std::vector<TypeID> retained_members
) noexcept -> FailureTermID {
    require_construction_open("add intersection failure term");
    return failure_constraints.add_intersection_term(input, std::move(retained_members));
}
auto ProgramDraft::add_failure_member(FailureTermID destination, TypeID member) noexcept -> void {
    require_construction_open("add failure member");
    failure_constraints.add_member(destination, member);
}
auto ProgramDraft::add_failure_contribution(
    FailureTermID destination,
    FailureTermID source
) noexcept -> void {
    require_construction_open("add failure contribution");
    failure_constraints.add_contribution(destination, source);
}
auto ProgramDraft::add_guarded_failure_contribution(
    FailureTermID destination,
    FailureTermID gate,
    FailureTermID source
) noexcept -> void {
    require_construction_open("add guarded failure contribution");
    failure_constraints.add_guarded_contribution(destination, gate, source);
}
auto ProgramDraft::equate_failures(FailureTermID left, FailureTermID right) noexcept -> void {
    require_construction_open("equate failure terms");
    failure_constraints.equate(left, right);
}
auto ProgramDraft::require_empty_failures(
    FailureTermID term,
    ProgramOriginID origin,
    EmptyFailureRequirementKind kind
) noexcept -> void {
    require_construction_open("require empty failures");
    failure_constraints.require_empty(term, origin, kind);
}
auto ProgramDraft::require_non_empty_failures(FailureTermID term, ProgramOriginID origin) noexcept
    -> void {
    require_construction_open("require non-empty failures");
    failure_constraints.require_non_empty(term, origin);
}
auto ProgramDraft::require_failure_subset(
    FailureTermID actual,
    FailureTermID allowed,
    ProgramOriginID origin,
    FailureSubsetRequirementKind kind
) noexcept -> void {
    require_construction_open("require failure subset");
    failure_constraints.require_subset(actual, allowed, origin, kind);
}
auto ProgramDraft::require_equal_failures(
    FailureTermID left,
    FailureTermID right,
    ProgramOriginID origin
) noexcept -> void {
    require_construction_open("require equal failures");
    failure_constraints.require_equal(left, right, origin);
}
auto ProgramDraft::require_declared_failure_contract(
    FailureTermID actual,
    ProgramOriginID origin
) noexcept -> void {
    require_construction_open("require declared failure contract");
    failure_constraints.require_declared_contract(actual, origin);
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
auto ProgramDraft::source_slice_copy(ProgramModuleID module, Span span) const noexcept
    -> std::string {
    return source_slice_copy(module_source(module), span);
}
auto ProgramDraft::intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID {
    require_not_failed("intern spelling");
    return provenance_appender.intern_spelling(spelling);
}
auto ProgramDraft::append_source_origin(ProgramSourceID source, Span span) noexcept
    -> ProgramOriginID {
    require_not_failed("append source origin");
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
    require_not_failed("append expansion origin");
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

auto ProgramDraft::finish_declarations() noexcept -> void {
    require_state(State::Declarations, "finish declarations");
    static_cast<void>(declarations.finish_resolution());
    state = State::Bodies;
}

auto ProgramDraft::reserve_body(BodyKind kind) noexcept -> BodyReservation {
    require_state(State::Bodies, "reserve body");
    const auto id = body_slots.reserve();
    if (static_cast<std::size_t>(id.index()) != reserved_body_kinds.size()
        || reserved_body_kinds.size() != test_by_body.size()) {
        invariant_violation("body reservation metadata lost table alignment");
    }
    reserved_body_kinds.push_back(kind);
    test_by_body.emplace_back();
    return BodyReservation(id, kind, provenance_appender.reader().identity());
}
auto ProgramDraft::add_body_draft(StructuredBodyDraft body) noexcept -> void {
    require_state(State::Bodies, "add body draft");
    const auto id = body.id;
    if (!body_slots.contains(id)
        || body.scopes.owner().program() != program_identity
        || body.provenance_identity != provenance_appender.reader().identity()
        || std::ranges::any_of(body_drafts, [&](const StructuredBodyDraft& existing) noexcept {
               return existing.id == id;
           })) {
        invariant_violation("body draft disagreed with its program reservation");
    }
    body_drafts.push_back(std::move(body));
}
auto ProgramDraft::take_body_drafts() noexcept -> std::vector<StructuredBodyDraft> {
    require_state(State::Solved, "take body drafts for publication");
    if (body_drafts.size() != body_slots.size()) {
        invariant_violation("body elaboration did not define every reservation");
    }
    return std::move(body_drafts);
}
auto ProgramDraft::publish_bodies(std::vector<SemIRBody> bodies) noexcept -> void {
    require_state(State::Solved, "publish bodies");
    if (bodies.size() != body_slots.size()
        || reserved_body_kinds.size() != body_slots.size()
        || test_by_body.size() != body_slots.size()) {
        invariant_violation("atomic body publication did not cover every reservation");
    }

    auto by_index = std::vector<SemIRBody*>(body_slots.size(), nullptr);
    const auto declaration_view = declarations.resolved_view();
    for (auto& body : bodies) {
        const auto body_id = body.id();
        const auto identity = body.identity();
        const auto kind = body.kind();
        if (body_id.owner() != program_identity
            || identity.program() != program_identity
            || body_id.index() != identity.body_index()) {
            invariant_violation("published body has inconsistent program/body identity evidence");
        }
        if (body.provenance_identity() != provenance_appender.reader().identity()) {
            invariant_violation("published body has foreign provenance identity evidence");
        }
        if (!body_slots.contains(body_id)
            || static_cast<std::size_t>(body_id.index()) >= reserved_body_kinds.size()
            || reserved_body_kinds[body_id.index()] != kind
            || by_index[body_id.index()] != nullptr) {
            invariant_violation("published body disagreed with its reservation metadata");
        }
        const auto callable = declaration_view.callable_for_body(body_id);
        if (kind == BodyKind::Test) {
            if (callable.has_value() || !test_by_body[body_id.index()].has_value()) {
                invariant_violation("test body was not assigned exactly one test declaration");
            }
        } else {
            if (!callable.has_value()) {
                invariant_violation("function or closure body had no owning callable declaration");
            }
            const auto implementation = declaration_view.callable_implementation(*callable);
            const auto kind_matches =
                (kind == BodyKind::Function
                 && std::holds_alternative<FunctionBodyImplementation>(implementation))
                || (kind == BodyKind::Closure
                    && std::holds_alternative<ClosureBodyImplementation>(implementation));
            if (!kind_matches) {
                invariant_violation("body kind disagreed with its callable implementation");
            }
        }
        by_index[body_id.index()] = std::addressof(body);
    }

    // No published table is mutated until the complete batch has passed every
    // program/body relationship check above.
    for (auto index = 0uz; index < by_index.size(); ++index) {
        if (by_index[index] == nullptr) {
            invariant_violation("atomic body publication omitted a reservation");
        }
        const auto body_id = by_index[index]->id();
        body_slots.define(body_id, std::move(*by_index[index]));
    }
}
auto ProgramDraft::reserve_test() noexcept -> TestID {
    require_state(State::Declarations, "reserve test declaration");
    return test_slots.reserve();
}
auto ProgramDraft::define_test(TestID id, TestDeclaration test) noexcept -> void {
    require_state(State::Bodies, "define test");
    if (test.module_id.owner() != program_identity
        || test.body.owner() != program_identity
        || test.name.owner() != provenance_appender.reader().identity()
        || test.origin.owner() != provenance_appender.reader().identity()) {
        invariant_violation("test declaration mixed semantic or provenance owners");
    }
    static_cast<void>(declarations.resolved_view().module_decl(test.module_id));
    if (!body_slots.contains(test.body)
        || static_cast<std::size_t>(test.body.index()) >= reserved_body_kinds.size()
        || reserved_body_kinds.size() != test_by_body.size()
        || reserved_body_kinds[test.body.index()] != BodyKind::Test) {
        invariant_violation("test declaration used a non-test body reservation");
    }
    if (!test_slots.contains(id) || test_slots.is_defined(id)) {
        invariant_violation("test declaration used an invalid or defined reservation");
    }
    auto& assigned_test = test_by_body[test.body.index()];
    if (assigned_test.has_value()) {
        invariant_violation("test body was assigned more than one test declaration");
    }
    assigned_test = id;
    test_slots.define(id, test);
}

auto ProgramDraft::solve_construction() noexcept -> AnalysisResult<void> {
    require_state(State::Bodies, "solve construction");
    if (!declarations.resolved_view().callable_implementations_complete()) {
        invariant_violation("construction solving began before all callable bodies were assigned");
    }
    state = State::Failed;
    auto failure_result = solve_failure_constraints(
        std::move(failure_constraints).finish(),
        failure_sets,
        provenance_appender.reader(),
        analysis_diagnostics
    );
    if (!failure_result.has_value()) {
        return std::unexpected(failure_result.error());
    }
    solved_failures.emplace(std::move(*failure_result));
    resolved_types.emplace(
        std::move(construction_types).canonicalize(*solved_failures, types, callable_signatures)
    );
    finalize_callable_signatures();
    state = State::Solved;
    return {};
}

auto ProgramDraft::concrete_type(ConstructionTypeRef type) const noexcept -> TypeID {
    require_state(State::Solved, "resolve construction type");
    return resolve_type(type);
}
auto ProgramDraft::concrete_failure_set(FailureTermID failures) const noexcept -> FailureSetID {
    require_state(State::Solved, "resolve construction failure set");
    if (failures.owner() != program_identity) {
        invariant_violation("failure resolution used a foreign failure term");
    }
    return solved_failures->failure_set(failures);
}
auto ProgramDraft::canonical_type_copy(TypeID type) const noexcept -> CanonicalType {
    require_state(State::Solved, "read canonical type");
    return types.copy(type);
}
auto ProgramDraft::callable_signature(CallableID callable) const noexcept -> CallableSignatureID {
    require_state(State::Solved, "read callable signature");
    return declarations.resolved_view().callable_signature(callable);
}
auto ProgramDraft::callable_signature_copy(CallableSignatureID signature) const noexcept
    -> CallableSignature {
    require_state(State::Solved, "read callable signature fact");
    return callable_signatures.copy(signature);
}
auto ProgramDraft::callable_crosses_cpp_boundary(CallableID callable) const noexcept -> bool {
    require_state(State::Solved, "read callable implementation effect");
    if (callable.owner() != program_identity) {
        invariant_violation("callable effect lookup used a foreign callable");
    }
    return std::holds_alternative<CppImportImplementation>(
        declarations.resolved_view().callable_implementation(callable)
    );
}
auto ProgramDraft::body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID> {
    require_declarations_available("find body for callable");
    return declarations.resolved_view().body_for_callable(callable);
}
auto ProgramDraft::callable_for_body(BodyID body) const noexcept -> std::optional<CallableID> {
    require_declarations_available("find callable for body");
    return declarations.resolved_view().callable_for_body(body);
}
auto ProgramDraft::failure_set_copy(FailureSetID failures) const noexcept -> FailureSet {
    require_state(State::Solved, "read failure set");
    return failure_sets.copy(failures);
}
auto ProgramDraft::is_inhabited(TypeID type) const noexcept -> bool {
    require_state(State::Solved, "query type inhabitance");
    const auto view = declarations.resolved_view();
    return compute_type_inhabitance(
        program_identity,
        type,
        [&](TypeID id) noexcept { return types.copy(id); },
        [&](StructID id) noexcept {
            const auto declaration = view.structure(id);
            auto fields = std::vector<TypeID>();
            fields.reserve(declaration.fields.size());
            for (const auto& field : declaration.fields) {
                fields.push_back(resolve_type(field.type));
            }
            return fields;
        },
        [&](EnumID id) noexcept {
            const auto declaration = view.enumeration(id);
            auto cases = std::vector<std::vector<TypeID>>();
            cases.reserve(declaration.cases.size());
            for (const auto case_id : declaration.cases) {
                const auto enum_case = view.enum_case(case_id);
                auto payload = std::vector<TypeID>();
                payload.reserve(enum_case.payload_types.size());
                for (const auto field : enum_case.payload_types) {
                    payload.push_back(resolve_type(field));
                }
                cases.push_back(std::move(payload));
            }
            return cases;
        }
    );
}

auto ProgramDraft::resolve_type(ConstructionTypeRef type) const noexcept -> TypeID {
    return std::visit(
        [&](const auto id) noexcept -> TypeID {
            using ID = std::remove_cvref_t<decltype(id)>;
            if (id.owner() != program_identity) {
                invariant_violation("callable contract used a foreign type identity");
            }
            if constexpr (std::same_as<ID, TypeID>) {
                return id;
            } else if constexpr (std::same_as<ID, TypeTermID>) {
                return resolved_types->type(id);
            } else {
                static_assert(std::same_as<ID, void>, "unhandled construction type reference");
            }
        },
        type
    );
}
auto ProgramDraft::resolve_failures(FailureTermID failures) const noexcept -> FailureSetID {
    if (failures.owner() != program_identity) {
        invariant_violation("callable contract used a foreign failure identity");
    }
    return solved_failures->failure_set(failures);
}
auto ProgramDraft::finalize_callable_signatures() noexcept -> void {
    const auto view = declarations.resolved_view();
    for (const auto callable_id : view.callable_ids()) {
        const auto contract = view.callable_contract(callable_id);
        auto parameters = std::vector<CallableParameter>();
        parameters.reserve(contract.parameters.size());
        for (const auto& parameter : contract.parameters) {
            parameters.push_back(
                CallableParameter {
                    .access = parameter.access,
                    .type = resolve_type(parameter.type),
                }
            );
        }
        const auto signature = callable_signatures.intern(
            CallableSignature {
                .parameters = std::move(parameters),
                .result = resolve_type(contract.result),
                .failures = resolve_failures(contract.failures),
            }
        );
        declarations.define_callable_signature(callable_id, signature);
    }
    declarations.finish_callable_signatures();
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
