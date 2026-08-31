module carven:semantic.analysis.session.read;

import :semantic.analysis.session;
import std;

class SemanticDraftView final {
public:
    SemanticDraftView(const SemanticDraft& draft) noexcept;

    auto provenance() const noexcept -> CompilationProvenanceView;
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
    auto symbol_constant(SymbolID id) const noexcept -> std::optional<HIRConstantID>;
    auto symbol_role(SymbolID id) const noexcept -> SemanticSymbolRole;
    auto symbol_write_eligible(SymbolID id) const noexcept -> bool;
    auto symbol_states() const noexcept -> std::span<const SemanticSymbolState>;
    auto place_use_candidate(HIRExprID id) const noexcept -> const std::optional<SemanticPlaceUse>&;
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
    auto nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
        -> const HIRNominalCapabilities&;
    auto structure_capabilities() const noexcept -> std::span<const HIRNominalCapabilities>;
    auto enumeration_capabilities() const noexcept -> std::span<const HIRNominalCapabilities>;
    auto nominal_dependency_sets() const noexcept
        -> std::span<const std::vector<HIRNominalDeclRef>>;
    auto nominal_containment(HIRNominalDeclRef declaration) const noexcept
        -> std::span<const HIRNominalDeclRef>;

private:
    const SemanticDraft* semantic_draft;
};
