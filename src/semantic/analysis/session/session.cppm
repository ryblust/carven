module carven:semantic.analysis.session;

import :semantic.hir;
import :semantic.hir.constant;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.pattern;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :source.provenance;
import :support.id_table;
import std;

class ProgramAnalyzer;
class SemanticSession;
class SemanticDraft;
class RecordedControlAnalysis;

struct SemanticEnumCaseContract final {
    EnumID owner;
    ProgramSpellingID name;
    std::vector<HIRTypeID> payload_types;
    SymbolID symbol;
    ProgramOriginID origin;
};

struct SemanticEnumCaseView final {
    EnumID owner;
    ProgramSpellingID name;
    std::span<const HIRTypeID> payload_types;
    SymbolID symbol;
    ProgramOriginID origin;
};

class SemanticDeclarationCapabilities final {
public:
    auto define(FunctionID id, HIRFunctionDecl declaration) noexcept -> void;
    auto define(StructID id, HIRStructDecl declaration) noexcept -> void;
    auto define(EnumID id, HIREnumDecl declaration) noexcept -> void;
    auto define(EnumCaseID id, SemanticEnumCaseContract declaration) noexcept -> void;
    auto function(FunctionID id) const noexcept -> const HIRFunctionDecl&;
    auto structure(StructID id) const noexcept -> const HIRStructDecl&;
    auto enumeration(EnumID id) const noexcept -> const HIREnumDecl&;
    auto enum_case(EnumCaseID id) const noexcept -> SemanticEnumCaseView;
    auto symbol(SymbolID id) const noexcept -> const HIRSymbol&;
    auto type(HIRTypeID id) const noexcept -> const HIRType&;
    auto complete_enum_case(EnumCaseID id, std::optional<HIRConstantID> constant) noexcept -> void;
    auto seal_declarations(
        std::vector<HIRNominalCapabilities> structures,
        std::vector<HIRNominalCapabilities> enumerations
    ) noexcept -> void;

private:
    explicit SemanticDeclarationCapabilities(SemanticDraft& draft) noexcept;

    SemanticDraft* semantic_draft;

    friend class SemanticDraft;
};

class SemanticEntityReservations final {
public:
    auto reserve_symbol() noexcept -> SymbolID;
    auto reserve_function() noexcept -> FunctionID;
    auto reserve_struct() noexcept -> StructID;
    auto reserve_enum() noexcept -> EnumID;
    auto reserve_enum_case() noexcept -> EnumCaseID;

private:
    explicit SemanticEntityReservations(SemanticDraft& construction) noexcept;

    SemanticDraft* construction;

    friend class SemanticDraft;
};

enum class SemanticSymbolRole {
    Module,
    Local,
    Parameter,
    LoopBinding,
};

enum class SemanticBindingRole {
    None,
    Owner,
    ReadAlias,
    WriteAlias,
    ClosureState,
    CompileTime,
};

enum class SemanticFailureContractKind {
    Inferred,
    Declared,
    UndeclaredPublished,
};

struct SemanticCallableFailureInput final {
    FailureSetID declared_failure_set;
    SemanticFailureContractKind policy;
};

struct SemanticSymbolSpec final {
    ProgramSpellingID name;
    std::optional<ProgramModuleID> module_id;
    SemanticSymbolRole role;
    std::optional<SymbolID> parent;
};

struct AnalysisExpressionProofCheckpoint final {
    std::size_t expression_count;
};

struct SemanticBindingPosition final {
    SemanticScopeID scope;
    std::uint32_t declaration_order;
};

struct SemanticSymbolState final {
    HIRSymbol symbol;
    std::optional<ProgramOriginID> unused_candidate;
    std::optional<HIRConstantID> constant;
    std::optional<SemanticBindingPosition> binding_position;
    SemanticSymbolRole role;
    SemanticBindingRole binding_role;
    bool explicit_capture;
    bool write_eligible;
};

class SemanticDraftStorage final {
private:
    SemanticDraftStorage() = default;
    SemanticDraftStorage(const SemanticDraftStorage&) = delete;
    SemanticDraftStorage(SemanticDraftStorage&&) = default;
    ~SemanticDraftStorage() = default;

    auto operator=(const SemanticDraftStorage&) -> SemanticDraftStorage& = delete;
    auto operator=(SemanticDraftStorage&&) -> SemanticDraftStorage& = default;

    IDTable<HIRType, HIRTypeID> types;
    std::flat_map<HIRTypeValue, HIRTypeID> type_index;
    IDTable<HIRExpr, HIRExprID> expressions;
    IDTable<HIRExpressionControl, HIRExprID> expression_controls;
    IDTable<EvaluationEffect, HIRExprID> evaluation_effects;
    IDTable<std::optional<SemanticPlaceUse>, HIRExprID> place_uses;
    IDTable<std::optional<HIRTryFacts>, HIRExprID> try_facts;
    IDTable<HIRConstantFact, HIRConstantID> constants;
    IDTable<HIRStmt, HIRStmtID> statements;
    IDTable<HIRPattern, HIRPatternID> patterns;
    IDTable<HIRBlock, HIRBlockID> blocks;
    IDTable<HIRBlockControl, HIRBlockID> block_controls;
    IDTable<SemanticScope, SemanticScopeID> scopes;
    IDTable<std::optional<SemanticBindingFacts>, SymbolID> bindings;
    IDTable<HIRFunctionDecl, FunctionID> functions;
    IDTable<HIRTestDecl, TestID> tests;
    IDTable<HIRStructDecl, StructID> structures;
    IDTable<HIREnumDecl, EnumID> enumerations;
    IDTable<HIREnumCase, EnumCaseID> enum_cases;
    IDTable<HIRModule, ProgramModuleID> modules;
    IDTable<HIRFailureSet, FailureSetID> failure_sets;
    std::flat_map<HIRFailureSet, FailureSetID> failure_set_index;
    IDTable<HIRCallableSignature, CallableSignatureID> callable_signatures;
    std::flat_map<HIRCallableSignature, CallableSignatureID> callable_signature_index;
    IDTable<HIRCallable, CallableID> callables;
    IDTable<HIRCallableFlow, CallableID> callable_flows;
    IDTable<HIRNominalCapabilities, StructID> struct_capabilities;
    IDTable<HIRNominalCapabilities, EnumID> enum_capabilities;
    HIRNominalContainment nominal_containment;

    friend class SemanticSession;
    friend class SemanticDraft;
    friend auto freeze_flow_candidate(SemanticDraft&, RecordedControlAnalysis&&) noexcept -> void;
};

class SemanticDraft final {
public:
    SemanticDraft(const SemanticDraft&) = delete;
    SemanticDraft(SemanticDraft&&) = delete;
    ~SemanticDraft() = default;

    auto operator=(const SemanticDraft&) -> SemanticDraft& = delete;
    auto operator=(SemanticDraft&&) -> SemanticDraft& = delete;

    auto intern_string(std::string_view value) noexcept -> ProgramSpellingID;
    auto append_origin(ProgramOrigin value) noexcept -> ProgramOriginID;
    auto intern_type(HIRType value) noexcept -> HIRTypeID;
    auto intern_failure_set(std::vector<HIRTypeID> failures) noexcept -> FailureSetID;
    auto intern_callable_signature(HIRCallableSignature value) noexcept -> CallableSignatureID;
    auto intern_callable_signature(
        std::vector<HIRFunctionParameterType> parameters,
        HIRTypeID result,
        std::vector<HIRTypeID> failures
    ) noexcept -> CallableSignatureID;
    auto append_body_callable(
        std::vector<HIRFunctionParameterType> parameters,
        HIRTypeID result,
        std::vector<HIRTypeID> failures,
        SemanticFailureContractKind failure_contract
    ) noexcept -> CallableID;
    auto append_cpp_import_callable(
        std::vector<HIRFunctionParameterType> parameters,
        HIRTypeID result,
        ProgramOriginID form_origin
    ) noexcept -> CallableID;
    auto intern_function_ref_type(
        std::vector<HIRFunctionParameterType> parameters,
        HIRTypeID result,
        std::vector<HIRTypeID> failures = {}
    ) noexcept -> HIRTypeID;
    auto intern_function_type(CallableID callable) noexcept -> HIRTypeID;
    auto intern_closure_type(CallableID callable, bool capturing) noexcept -> HIRTypeID;
    auto append_expression(HIRExpr value) noexcept -> HIRExprID;
    auto append_constant(HIRConstantFact value) noexcept -> HIRConstantID;
    auto begin_expression_proof() const noexcept -> AnalysisExpressionProofCheckpoint;
    auto finish_expression_proof(AnalysisExpressionProofCheckpoint checkpoint) noexcept -> void;
    auto append_statement(HIRStmt value) noexcept -> HIRStmtID;
    auto append_pattern(HIRPattern value) noexcept -> HIRPatternID;
    auto append_block(HIRBlock value) noexcept -> HIRBlockID;
    auto append_scope(std::optional<SemanticScopeID> parent) noexcept -> SemanticScopeID;
    auto entity_reservations() noexcept -> SemanticEntityReservations;
    auto declaration_capabilities() noexcept -> SemanticDeclarationCapabilities;
    auto append_symbol(SemanticSymbolSpec spec) noexcept -> SymbolID;
    auto define_reserved_symbol(SymbolID symbol, SemanticSymbolSpec spec) noexcept -> void;
    auto adopt_symbol_type(SymbolID symbol, HIRTypeID type) noexcept -> void;
    auto define_symbol_constant(SymbolID symbol, HIRConstantID constant) noexcept -> void;
    auto define_symbol_binding(
        SymbolID symbol,
        SemanticBindingRole role,
        bool write_eligible
    ) noexcept -> void;
    auto mark_symbol_referenced(SymbolID symbol) noexcept -> void;
    auto mark_explicit_capture(SymbolID symbol) noexcept -> void;
    auto record_symbol_lint_candidate(SymbolID symbol, ProgramOriginID origin) noexcept -> void;
    auto symbol_constant(SymbolID symbol) const noexcept -> std::optional<HIRConstantID>;
    auto symbol_role(SymbolID symbol) const noexcept -> SemanticSymbolRole;
    auto symbol_write_eligible(SymbolID symbol) const noexcept -> bool;
    auto symbol_states() const noexcept -> std::span<const SemanticSymbolState>;
    auto bind_symbol(SymbolID symbol, SemanticBindingPosition position) noexcept -> void;
    auto derive_binding_facts() noexcept -> void;
    auto derive_place_uses() noexcept -> void;
    auto place_use_candidate(HIRExprID expression) noexcept -> std::optional<SemanticPlaceUse>&;
    auto place_use_candidate(HIRExprID expression) const noexcept
        -> const std::optional<SemanticPlaceUse>&;
    auto normalize_void_block(HIRBlockID id) noexcept -> HIRBlockID;

private:
    auto define_function_contract(FunctionID id, HIRFunctionDecl declaration) noexcept -> void;
    auto define_struct_contract(StructID id, HIRStructDecl declaration) noexcept -> void;
    auto define_enum_contract(EnumID id, HIREnumDecl declaration) noexcept -> void;
    auto define_enum_case_contract(EnumCaseID id, SemanticEnumCaseContract declaration) noexcept
        -> void;
    auto complete_enum_case_contract(EnumCaseID id, std::optional<HIRConstantID> constant) noexcept
        -> void;
    auto enum_case_contract_view(EnumCaseID id) const noexcept -> SemanticEnumCaseView;
    auto freeze_nominal_capabilities(
        std::vector<HIRNominalCapabilities> structures,
        std::vector<HIRNominalCapabilities> enumerations
    ) noexcept -> void;
    auto freeze_declaration_contracts() noexcept -> void;

public:
    auto define_callable_body(
        CallableID callable,
        std::vector<HIRParameter> parameters,
        SemanticScopeID scope,
        HIRBlockID root
    ) noexcept -> void;
    auto append_test(
        ProgramOriginID origin,
        ProgramSpellingID name,
        SemanticScopeID scope,
        HIRBlockID root
    ) noexcept -> TestID;
    auto append_module(HIRModule value) noexcept -> ProgramModuleID;
    auto expression(HIRExprID id) noexcept -> HIRExpr&;
    auto statement(HIRStmtID id) noexcept -> HIRStmt&;
    auto block(HIRBlockID id) noexcept -> HIRBlock&;
    auto body(BodyID id) noexcept -> HIRBody&;
    auto hir_module(ProgramModuleID id) noexcept -> HIRModule&;
    auto publish_nominal_containment(HIRNominalContainment containment) noexcept -> void;
    auto type(HIRTypeID id) const noexcept -> const HIRType&;
    auto expression(HIRExprID id) const noexcept -> const HIRExpr&;
    auto expression_control(HIRExprID id) const noexcept -> const HIRExpressionControl&;
    auto evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect&;
    auto place_use(HIRExprID id) const noexcept -> const std::optional<SemanticPlaceUse>&;
    auto try_facts(HIRExprID id) const noexcept -> const std::optional<HIRTryFacts>&;
    auto constant(HIRConstantID id) const noexcept -> const HIRConstantFact&;
    auto statement(HIRStmtID id) const noexcept -> const HIRStmt&;
    auto pattern(HIRPatternID id) const noexcept -> const HIRPattern&;
    auto block(HIRBlockID id) const noexcept -> const HIRBlock&;
    auto block_control(HIRBlockID id) const noexcept -> const HIRBlockControl&;
    auto scope(SemanticScopeID id) const noexcept -> const SemanticScope&;
    auto binding(SymbolID id) const noexcept -> const std::optional<SemanticBindingFacts>&;
    auto function(FunctionID id) const noexcept -> const HIRFunctionDecl&;
    auto has_body(BodyID id) const noexcept -> bool;
    auto body(BodyID id) const noexcept -> const HIRBody&;
    auto test(TestID id) const noexcept -> const HIRTestDecl&;
    auto structure(StructID id) const noexcept -> const HIRStructDecl&;
    auto enumeration(EnumID id) const noexcept -> const HIREnumDecl&;
    auto enum_case(EnumCaseID id) const noexcept -> const HIREnumCase&;
    auto symbol(SymbolID id) const noexcept -> const HIRSymbol&;
    auto hir_module(ProgramModuleID id) const noexcept -> const HIRModule&;
    auto failure_set(FailureSetID id) const noexcept -> const HIRFailureSet&;
    auto callable_signature(CallableSignatureID id) const noexcept -> const HIRCallableSignature&;
    auto callable(CallableID id) const noexcept -> const HIRCallable&;
    auto callable_flow(CallableID id) const noexcept -> const HIRCallableFlow&;
    auto callable_failure_input(CallableID id) const noexcept
        -> const SemanticCallableFailureInput&;
    auto functions() const noexcept -> std::span<const HIRFunctionDecl>;
    auto bodies() const noexcept -> std::span<const std::optional<HIRBody>>;
    auto tests() const noexcept -> std::span<const HIRTestDecl>;
    auto structures() const noexcept -> std::span<const HIRStructDecl>;
    auto enumerations() const noexcept -> std::span<const HIREnumDecl>;
    auto enum_cases() const noexcept -> std::span<const HIREnumCase>;
    auto modules() const noexcept -> std::span<const HIRModule>;
    auto expressions() const noexcept -> std::span<const HIRExpr>;
    auto expression_controls() const noexcept -> std::span<const HIRExpressionControl>;
    auto evaluation_effects() const noexcept -> std::span<const EvaluationEffect>;
    auto place_uses() const noexcept -> std::span<const std::optional<SemanticPlaceUse>>;
    auto try_facts() const noexcept -> std::span<const std::optional<HIRTryFacts>>;
    auto constants() const noexcept -> std::span<const HIRConstantFact>;
    auto types() const noexcept -> std::span<const HIRType>;
    auto statements() const noexcept -> std::span<const HIRStmt>;
    auto patterns() const noexcept -> std::span<const HIRPattern>;
    auto blocks() const noexcept -> std::span<const HIRBlock>;
    auto block_controls() const noexcept -> std::span<const HIRBlockControl>;
    auto scopes() const noexcept -> std::span<const SemanticScope>;
    auto bindings() const noexcept -> std::span<const std::optional<SemanticBindingFacts>>;
    auto symbol_count() const noexcept -> std::size_t;
    auto failure_sets() const noexcept -> std::span<const HIRFailureSet>;
    auto callable_signatures() const noexcept -> std::span<const HIRCallableSignature>;
    auto callables() const noexcept -> std::span<const HIRCallable>;
    auto callable_flows() const noexcept -> std::span<const HIRCallableFlow>;
    auto callable_failure_inputs() const noexcept -> std::span<const SemanticCallableFailureInput>;
    auto nominal_containment(HIRNominalDeclRef declaration) const noexcept
        -> std::span<const HIRNominalDeclRef>;
    auto nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
        -> const HIRNominalCapabilities&;
    auto structure_capabilities() const noexcept -> std::span<const HIRNominalCapabilities>;
    auto enumeration_capabilities() const noexcept -> std::span<const HIRNominalCapabilities>;
    auto nominal_dependency_sets() const noexcept
        -> std::span<const std::vector<HIRNominalDeclRef>>;
    auto provenance() const noexcept -> CompilationProvenanceView;

private:
    struct EnumCaseSlot final {
        SemanticEnumCaseContract contract;
        std::optional<std::optional<HIRConstantID>> constant;
    };

    explicit SemanticDraft(CompilationProvenance provenance) noexcept;
    auto symbol_state(SymbolID symbol) noexcept -> SemanticSymbolState&;
    auto symbol_state(SymbolID symbol) const noexcept -> const SemanticSymbolState&;
    auto reserve_symbol() noexcept -> SymbolID;
    auto reserve_function() noexcept -> FunctionID;
    auto reserve_struct() noexcept -> StructID;
    auto reserve_enum() noexcept -> EnumID;
    auto reserve_enum_case() noexcept -> EnumCaseID;
    auto make_binding_facts(
        SymbolID symbol,
        SemanticBindingStorage storage,
        SemanticBindingCapabilities capabilities
    ) const noexcept -> SemanticBindingFacts;

    CompilationProvenanceAppender provenance_builder;
    SemanticDraftStorage storage;
    std::vector<SemanticSymbolState> symbol_construction;
    std::size_t reserved_symbol_count = 0;
    std::vector<std::optional<SemanticPlaceUse>> expression_place_uses;
    std::vector<SemanticCallableFailureInput> callable_failure_input_storage;
    std::vector<std::optional<HIRBody>> body_slots;
    std::vector<std::optional<HIRFunctionDecl>> function_slots;
    std::vector<std::optional<HIRStructDecl>> struct_slots;
    std::vector<std::optional<HIREnumDecl>> enum_slots;
    std::vector<std::optional<EnumCaseSlot>> enum_case_slots;

    friend class SemanticSession;
    friend class SemanticDeclarationCapabilities;
    friend class SemanticEntityReservations;
    friend auto freeze_flow_candidate(SemanticDraft&, RecordedControlAnalysis&&) noexcept -> void;
};

class SemanticSession final {
public:
    explicit SemanticSession(CompilationProvenance provenance) noexcept;
    SemanticSession(const SemanticSession&) = delete;
    SemanticSession(SemanticSession&&) = delete;
    ~SemanticSession() = default;

    auto operator=(const SemanticSession&) -> SemanticSession& = delete;
    auto operator=(SemanticSession&&) -> SemanticSession& = delete;

    auto draft() noexcept -> SemanticDraft&;
    auto draft() const noexcept -> const SemanticDraft&;
    auto finish() && noexcept -> SemanticProgram;

private:
    SemanticDraft semantic_draft;
};
