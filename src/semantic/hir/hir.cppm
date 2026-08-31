module carven:semantic.hir;

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

struct HIRModule final {
    std::vector<HIRModuleItem> items;
};

struct HIRNominalContainment final {
    std::vector<std::vector<HIRNominalDeclRef>> direct_dependencies;
};

class SemanticSession;

class SemanticProgramStorage final {
    SemanticProgramStorage() = default;
    SemanticProgramStorage(const SemanticProgramStorage&) = delete;
    SemanticProgramStorage(SemanticProgramStorage&&) = default;
    ~SemanticProgramStorage() = default;

    auto operator=(const SemanticProgramStorage&) -> SemanticProgramStorage& = delete;
    auto operator=(SemanticProgramStorage&&) -> SemanticProgramStorage& = default;

    IDTable<HIRType, HIRTypeID> types;
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
    IDTable<HIRBody, BodyID> bodies;
    IDTable<HIRTestDecl, TestID> tests;
    IDTable<HIRStructDecl, StructID> structures;
    IDTable<HIREnumDecl, EnumID> enumerations;
    IDTable<HIREnumCase, EnumCaseID> enum_cases;
    IDTable<HIRModule, ProgramModuleID> modules;
    IDTable<HIRSymbol, SymbolID> symbols;
    IDTable<HIRFailureSet, FailureSetID> failure_sets;
    IDTable<HIRCallableSignature, CallableSignatureID> callable_signatures;
    IDTable<HIRCallable, CallableID> callables;
    IDTable<HIRCallableFlow, CallableID> callable_flows;
    IDTable<HIRNominalCapabilities, StructID> struct_capabilities;
    IDTable<HIRNominalCapabilities, EnumID> enum_capabilities;
    HIRNominalContainment nominal_containment;

    friend class SemanticSession;
    friend class SemanticProgram;
    friend class SemanticProgramView;
};

class SemanticProgramView final {
public:
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
    auto nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
        -> const HIRNominalCapabilities&;
    auto nominal_containment(HIRNominalDeclRef declaration) const noexcept
        -> std::span<const HIRNominalDeclRef>;
    auto structure_capabilities() const noexcept -> std::span<const HIRNominalCapabilities>;
    auto enumeration_capabilities() const noexcept -> std::span<const HIRNominalCapabilities>;
    auto nominal_dependency_sets() const noexcept
        -> std::span<const std::vector<HIRNominalDeclRef>>;
    auto functions() const noexcept -> std::span<const HIRFunctionDecl>;
    auto bodies() const noexcept -> std::span<const HIRBody>;
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
    auto symbols() const noexcept -> std::span<const HIRSymbol>;
    auto failure_sets() const noexcept -> std::span<const HIRFailureSet>;
    auto callable_signatures() const noexcept -> std::span<const HIRCallableSignature>;
    auto callables() const noexcept -> std::span<const HIRCallable>;
    auto callable_flows() const noexcept -> std::span<const HIRCallableFlow>;

private:
    SemanticProgramView(
        CompilationProvenanceView provenance,
        const SemanticProgramStorage& storage
    ) noexcept;

    CompilationProvenanceView compilation_provenance;
    const SemanticProgramStorage* semantic_storage;

    friend class SemanticSession;
    friend class SemanticProgram;
};

class SemanticProgram final {
public:
    SemanticProgram(const SemanticProgram&) = delete;
    SemanticProgram(SemanticProgram&&) = default;
    ~SemanticProgram() = default;

    auto operator=(const SemanticProgram&) -> SemanticProgram& = delete;
    auto operator=(SemanticProgram&&) -> SemanticProgram& = default;

    auto view() const noexcept -> SemanticProgramView;
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
    auto nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
        -> const HIRNominalCapabilities&;
    auto nominal_containment(HIRNominalDeclRef declaration) const noexcept
        -> std::span<const HIRNominalDeclRef>;
    auto functions() const noexcept -> std::span<const HIRFunctionDecl>;
    auto bodies() const noexcept -> std::span<const HIRBody>;
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
    auto symbols() const noexcept -> std::span<const HIRSymbol>;
    auto failure_sets() const noexcept -> std::span<const HIRFailureSet>;
    auto callable_signatures() const noexcept -> std::span<const HIRCallableSignature>;
    auto callables() const noexcept -> std::span<const HIRCallable>;
    auto callable_flows() const noexcept -> std::span<const HIRCallableFlow>;

private:
    SemanticProgram(CompilationProvenance provenance, SemanticProgramStorage storage) noexcept;

    CompilationProvenance compilation_provenance;
    SemanticProgramStorage semantic_storage;

    friend class SemanticSession;
};
