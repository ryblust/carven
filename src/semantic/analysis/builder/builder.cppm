module carven:semantic.analysis.builder;

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
import std;

class ProgramAnalyzer;

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

class SemanticConstruction final {
public:
    explicit SemanticConstruction(CompilationProvenance provenance) noexcept;
    SemanticConstruction(const SemanticConstruction&) = delete;
    SemanticConstruction(SemanticConstruction&&) = delete;
    ~SemanticConstruction() = default;

    auto operator=(const SemanticConstruction&) -> SemanticConstruction& = delete;
    auto operator=(SemanticConstruction&&) -> SemanticConstruction& = delete;

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
    auto append_callable(
        std::vector<HIRFunctionParameterType> parameters,
        HIRTypeID result,
        std::vector<HIRTypeID> failures,
        HIRFailureContractKind failure_contract
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
    auto append_symbol(SemanticSymbolSpec spec) noexcept -> SymbolID;
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
    auto derive_places() noexcept -> void;
    auto derive_place_uses() noexcept -> void;
    auto place_use(HIRExprID expression) noexcept -> std::optional<SemanticPlaceUse>&;
    auto place_use(HIRExprID expression) const noexcept -> const std::optional<SemanticPlaceUse>&;
    auto publish_control_facts(
        std::vector<HIRExpressionFacts> expressions,
        std::vector<HIRBlockFacts> blocks
    ) noexcept -> void;
    auto normalize_void_block(HIRBlockID id) noexcept -> HIRBlockID;
    auto publish_declaration_contracts(
        std::vector<HIRFunctionDecl> functions,
        std::vector<HIRStructDecl> structures,
        std::vector<HIREnumDecl> enumerations,
        std::vector<HIREnumCase> enum_cases
    ) noexcept -> void;
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
    auto set_callable_failures(CallableID id, FailureSetID failures) noexcept -> void;
    auto publish_nominal_storage(HIRNominalStorage storage) noexcept -> void;
    auto type(HIRTypeID id) const noexcept -> const HIRType&;
    auto expression(HIRExprID id) const noexcept -> const HIRExpr&;
    auto expression_facts(HIRExprID id) const noexcept -> const HIRExpressionFacts&;
    auto constant(HIRConstantID id) const noexcept -> const HIRConstantFact&;
    auto statement(HIRStmtID id) const noexcept -> const HIRStmt&;
    auto pattern(HIRPatternID id) const noexcept -> const HIRPattern&;
    auto block(HIRBlockID id) const noexcept -> const HIRBlock&;
    auto block_facts(HIRBlockID id) const noexcept -> const HIRBlockFacts&;
    auto scope(SemanticScopeID id) const noexcept -> const SemanticScope&;
    auto place(SemanticPlaceID id) const noexcept -> const SemanticPlace&;
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
    auto functions() const noexcept -> std::span<const HIRFunctionDecl>;
    auto bodies() const noexcept -> std::span<const std::optional<HIRBody>>;
    auto tests() const noexcept -> std::span<const HIRTestDecl>;
    auto structures() const noexcept -> std::span<const HIRStructDecl>;
    auto enumerations() const noexcept -> std::span<const HIREnumDecl>;
    auto enum_cases() const noexcept -> std::span<const HIREnumCase>;
    auto modules() const noexcept -> std::span<const HIRModule>;
    auto expressions() const noexcept -> std::span<const HIRExpr>;
    auto expression_facts() const noexcept -> std::span<const HIRExpressionFacts>;
    auto constants() const noexcept -> std::span<const HIRConstantFact>;
    auto types() const noexcept -> std::span<const HIRType>;
    auto statements() const noexcept -> std::span<const HIRStmt>;
    auto patterns() const noexcept -> std::span<const HIRPattern>;
    auto blocks() const noexcept -> std::span<const HIRBlock>;
    auto block_facts() const noexcept -> std::span<const HIRBlockFacts>;
    auto scopes() const noexcept -> std::span<const SemanticScope>;
    auto places() const noexcept -> std::span<const SemanticPlace>;
    auto symbol_count() const noexcept -> std::size_t;
    auto failure_sets() const noexcept -> std::span<const HIRFailureSet>;
    auto callable_signatures() const noexcept -> std::span<const HIRCallableSignature>;
    auto callables() const noexcept -> std::span<const HIRCallable>;
    auto nominal_storage_order() const noexcept -> std::span<const HIRNominalDeclRef>;
    auto provenance() const noexcept -> CompilationProvenanceView;

private:
    auto finish() && noexcept -> SemanticProgram;
    auto symbol_state(SymbolID symbol) noexcept -> SemanticSymbolState&;
    auto symbol_state(SymbolID symbol) const noexcept -> const SemanticSymbolState&;
    auto publish_place(
        SymbolID symbol,
        SemanticPlaceStorage storage,
        SemanticPlaceCapabilities capabilities
    ) noexcept -> SemanticPlaceID;

    CompilationProvenanceAppender provenance_builder;
    HIRStorage storage;
    std::vector<SemanticSymbolState> symbol_construction;
    std::vector<std::optional<SemanticPlaceUse>> expression_place_uses;
    std::vector<std::optional<HIRBody>> body_slots;

    friend class ProgramAnalyzer;
};
