module carven:semantic.analysis.program;

import :diagnostics.sink;
import :frontend.ast.tree;
import :frontend.program;
import :semantic.analysis.diagnostics;
import :semantic.analysis.failure;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

class BodyReservation final {
public:
    BodyReservation(const BodyReservation&) = delete;
    BodyReservation(BodyReservation&& other) noexcept;
    ~BodyReservation() = default;

    auto operator=(const BodyReservation&) -> BodyReservation& = delete;
    auto operator=(BodyReservation&&) -> BodyReservation& = delete;

    auto id() const noexcept -> BodyID;
    auto kind() const noexcept -> BodyKind;

private:
    struct Consumed final {
        BodyID id;
        BodyKind kind;
        ProvenanceIdentity provenance;
    };

    BodyReservation(BodyID id, BodyKind kind, ProvenanceIdentity provenance) noexcept
        : body_id(id),
          body_kind(kind),
          provenance_identity(provenance),
          active(true) {}
    auto consume() noexcept -> Consumed;

    BodyID body_id;
    BodyKind body_kind;
    ProvenanceIdentity provenance_identity;
    bool active;

    friend class BodyBuilder;
    friend class ProgramDraft;
};

class ProgramDraft final {
public:
    static auto begin(SyntaxProgram&& syntax, DiagnosticSink& sink) noexcept -> ProgramDraft;

    ProgramDraft(const ProgramDraft&) = delete;
    ProgramDraft(ProgramDraft&& other) noexcept;
    ~ProgramDraft() = default;

    auto operator=(const ProgramDraft&) -> ProgramDraft& = delete;
    auto operator=(ProgramDraft&&) -> ProgramDraft& = delete;

    auto identity() const noexcept -> ProgramIdentity {
        require_not_failed("read program identity");
        return program_identity;
    }
    auto provenance_identity() const noexcept -> ProvenanceIdentity {
        require_not_failed("read provenance identity");
        return provenance_appender.reader().identity();
    }
    auto diagnostics() const noexcept -> AnalysisDiagnostics {
        require_not_failed("read diagnostics");
        return analysis_diagnostics;
    }

    auto syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree&;
    auto syntax_trees() const noexcept -> std::span<const SyntaxTree>;
    auto resolved_imports(ProgramModuleID module_id) const noexcept
        -> std::span<const ResolvedModuleImport>;
    auto module_count() const noexcept -> std::size_t;
    auto provenance_module_at(std::size_t index) const noexcept -> ProgramModuleID;

    auto owns(ProgramModuleID id) const noexcept -> bool;
    auto owns(ProgramSpellingID id) const noexcept -> bool;
    auto owns(ProgramOriginID id) const noexcept -> bool;
    auto source_span(ProgramOriginID id) const noexcept -> SourceSpan;
    auto spelling_copy(ProgramSpellingID id) const noexcept -> std::string;
    auto origin_slice_copy(ProgramOriginID id) const noexcept -> std::string;
    auto module_source(ProgramModuleID id) const noexcept -> ProgramSourceID;
    auto module_path_copy(ProgramModuleID id) const noexcept -> CanonicalModulePath;
    auto source_slice_copy(ProgramSourceID source, Span span) const noexcept -> std::string;
    auto source_slice_copy(ProgramModuleID module_id, Span span) const noexcept -> std::string;
    auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID;
    auto append_source_origin(ProgramSourceID source, Span span) noexcept -> ProgramOriginID;
    auto append_expansion_origin(ProgramOriginID parent, ProgramExpansionReason reason) noexcept
        -> ProgramOriginID;

    auto intern_type(const CanonicalType& type) noexcept -> TypeID;
    auto intern_builtin_type(BuiltinType type) noexcept -> TypeID;
    auto type_copy(TypeID type) const noexcept -> CanonicalType;
    auto intern_constant(ConstantFact fact) noexcept -> ConstantID;
    auto constant_copy(ConstantID constant) const noexcept -> ConstantFact;
    auto intern_failure_set(std::vector<TypeID> members) noexcept -> FailureSetID;
    auto empty_failure_set() noexcept -> FailureSetID;
    auto append_construction_type(ConstructionType type) noexcept -> TypeTermID;
    auto construction_type_copy(TypeTermID type) const noexcept -> ConstructionType;

    auto reserve_module_declaration() noexcept -> ModuleID;
    auto reserve_function_declaration() noexcept -> FunctionID;
    auto reserve_struct_declaration() noexcept -> StructID;
    auto reserve_enum_declaration() noexcept -> EnumID;
    auto reserve_enum_case_declaration() noexcept -> EnumCaseID;
    auto reserve_module_constant_declaration() noexcept -> ModuleConstantID;
    auto reserve_callable_declaration() noexcept -> CallableID;
    auto define_declaration(ModuleID id, ModuleDeclaration declaration) noexcept -> void;
    auto define_declaration(FunctionID id, FunctionDeclaration declaration) noexcept -> void;
    auto define_declaration(StructID id, ConstructionStructDeclaration declaration) noexcept
        -> void;
    auto define_declaration(EnumID id, EnumDeclaration declaration) noexcept -> void;
    auto define_declaration(EnumCaseID id, ConstructionEnumCaseDeclaration declaration) noexcept
        -> void;
    auto define_declaration(ModuleConstantID id, ModuleConstantDeclaration declaration) noexcept
        -> void;
    auto define_callable_contract(CallableID id, ConstructionCallableContract contract) noexcept
        -> void;
    auto append_body_callable(ConstructionCallableContract contract) noexcept -> CallableID;
    auto complete_callable(CallableID id, CallableImplementation implementation) noexcept -> void;

    auto module_declaration_copy(ModuleID id) const noexcept -> ModuleDeclaration;
    auto function_declaration_copy(FunctionID id) const noexcept -> FunctionDeclaration;
    auto construction_struct_declaration_copy(StructID id) const noexcept
        -> ConstructionStructDeclaration;
    auto enum_declaration_copy(EnumID id) const noexcept -> EnumDeclaration;
    auto construction_enum_case_declaration_copy(EnumCaseID id) const noexcept
        -> ConstructionEnumCaseDeclaration;
    auto module_constant_declaration_copy(ModuleConstantID id) const noexcept
        -> ModuleConstantDeclaration;
    auto construction_callable_contract_copy(CallableID id) const noexcept
        -> ConstructionCallableContract;
    auto construction_failure_term_copy(FailureTermID failures) const noexcept -> FailureTerm;
    auto module_declaration_count() const noexcept -> std::size_t;
    auto function_declaration_count() const noexcept -> std::size_t;
    auto struct_declaration_count() const noexcept -> std::size_t;
    auto enum_declaration_count() const noexcept -> std::size_t;
    auto enum_case_declaration_count() const noexcept -> std::size_t;
    auto module_constant_declaration_count() const noexcept -> std::size_t;
    auto callable_declaration_count() const noexcept -> std::size_t;
    auto module_declaration_ids() const noexcept -> std::vector<ModuleID>;
    auto function_declaration_ids() const noexcept -> std::vector<FunctionID>;
    auto struct_declaration_ids() const noexcept -> std::vector<StructID>;
    auto enum_declaration_ids() const noexcept -> std::vector<EnumID>;
    auto enum_case_declaration_ids() const noexcept -> std::vector<EnumCaseID>;
    auto module_constant_declaration_ids() const noexcept -> std::vector<ModuleConstantID>;
    auto callable_declaration_ids() const noexcept -> std::vector<CallableID>;

    auto add_empty_failure_term() noexcept -> FailureTermID;
    auto add_concrete_failure_term(std::vector<TypeID> members) noexcept -> FailureTermID;
    auto add_union_failure_term(std::vector<FailureTermID> inputs) noexcept -> FailureTermID;
    auto add_residual_failure_term(
        FailureTermID input,
        std::vector<TypeID> handled_members
    ) noexcept -> FailureTermID;
    auto add_intersection_failure_term(
        FailureTermID input,
        std::vector<TypeID> retained_members
    ) noexcept -> FailureTermID;
    auto add_failure_member(FailureTermID destination, TypeID member) noexcept -> void;
    auto add_failure_contribution(FailureTermID destination, FailureTermID source) noexcept -> void;
    auto add_guarded_failure_contribution(
        FailureTermID destination,
        FailureTermID gate,
        FailureTermID source
    ) noexcept -> void;
    auto equate_failures(FailureTermID left, FailureTermID right) noexcept -> void;
    auto require_empty_failures(
        FailureTermID term,
        ProgramOriginID origin,
        EmptyFailureRequirementKind kind
    ) noexcept -> void;
    auto require_non_empty_failures(FailureTermID term, ProgramOriginID origin) noexcept -> void;
    auto require_failure_subset(
        FailureTermID actual,
        FailureTermID allowed,
        ProgramOriginID origin,
        FailureSubsetRequirementKind kind
    ) noexcept -> void;
    auto require_equal_failures(
        FailureTermID left,
        FailureTermID right,
        ProgramOriginID origin
    ) noexcept -> void;
    auto require_declared_failure_contract(FailureTermID actual, ProgramOriginID origin) noexcept
        -> void;

    auto finish_declarations() noexcept -> void;

    auto reserve_body(BodyKind kind) noexcept -> BodyReservation;
    auto add_body_draft(StructuredBodyDraft body) noexcept -> void;
    auto reserve_test() noexcept -> TestID;
    auto define_test(TestID id, TestDeclaration test) noexcept -> void;

    auto solve_construction() noexcept -> AnalysisResult<void>;
    auto callable_signature(CallableID callable) const noexcept -> CallableSignatureID;
    auto callable_crosses_cpp_boundary(CallableID callable) const noexcept -> bool;
    auto body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID>;
    auto callable_for_body(BodyID body) const noexcept -> std::optional<CallableID>;

    auto types() const noexcept -> const CanonicalTypeStore&;
    auto constants() const noexcept -> const ConstantStore&;
    auto failure_sets() const noexcept -> const FailureSetStore&;
    auto callable_signatures() const noexcept -> const CallableSignatureStore&;
    auto declarations() const noexcept -> const DeclarationStore&;
    auto bodies() const noexcept -> const BodyStore&;
    auto tests() const noexcept -> const TestStore&;

    auto seal() && noexcept -> SemIRProgram;

private:
    enum class State {
        Declarations,
        Bodies,
        Solved,
        Failed,
        Sealed,
    };

    ProgramDraft(SyntaxProgramParts parts, DiagnosticSink& sink) noexcept;
    auto finalize_callable_signatures(
        const TypeResolution& types,
        const FailureSolution& failures
    ) noexcept -> void;
    auto complete_body(SemIRBody body, const DeclarationStore& declarations) noexcept -> void;
    auto require_not_failed(std::string_view operation) const noexcept -> void;
    auto require_construction_open(std::string_view operation) const noexcept -> void;
    auto require_declarations_available(std::string_view operation) const noexcept -> void;
    auto require_state(State expected, std::string_view operation) const noexcept -> void;

    ProgramIdentity program_identity;
    CompilationProvenanceAppender provenance_appender;
    AnalysisDiagnostics analysis_diagnostics;
    State state;

    struct ConstructionStorage final {
        ConstructionStorage(
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
              body_slots(identity),
              test_slots(identity) {}
        std::vector<SyntaxTree> syntax_by_module;
        ResolvedModuleImportGraph resolved_import_graph;
        CanonicalTypeStoreBuilder types;
        ConstantStoreBuilder constants;
        FailureSetStoreBuilder failure_sets;
        CallableSignatureStoreBuilder callable_signatures;
        ConstructionTypeStore construction_types;
        DeclarationBuilder declarations;
        FailureConstraintStore failure_constraints;
        std::vector<StructuredBodyDraft> body_drafts;
        ReservedProgramTable<SemIRBody, BodyID> body_slots;
        ReservedProgramTable<TestDeclaration, TestID> test_slots;
        std::vector<BodyKind> reserved_body_kinds;
        std::vector<std::optional<TestID>> test_by_body;
    };
    struct FinalStorage final {
        CanonicalTypeStore types;
        ConstantStore constants;
        FailureSetStore failure_sets;
        CallableSignatureStore callable_signatures;
        DeclarationStore declarations;
        BodyStore bodies;
        TestStore tests;
    };
    auto construction() noexcept -> ConstructionStorage&;
    auto construction() const noexcept -> const ConstructionStorage&;
    auto final() const noexcept -> const FinalStorage&;
    std::variant<ConstructionStorage, FinalStorage> storage;
};
