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

struct HIRNominalStorage final {
    std::vector<HIRNominalDeclRef> order;
    std::vector<std::vector<HIRNominalDeclRef>> direct_dependencies;
};

class SemanticConstruction;

class HIRStorage final {
    HIRStorage() = default;
    HIRStorage(const HIRStorage&) = delete;
    HIRStorage(HIRStorage&&) = default;
    ~HIRStorage() = default;

    auto operator=(const HIRStorage&) -> HIRStorage& = delete;
    auto operator=(HIRStorage&&) -> HIRStorage& = default;

    IDTable<HIRType, HIRTypeID> types;
    std::flat_map<HIRTypeValue, HIRTypeID> type_index;
    IDTable<HIRExpr, HIRExprID> expressions;
    IDTable<HIRExpressionFacts, HIRExprID> expression_facts;
    IDTable<HIRConstantFact, HIRConstantID> constants;
    IDTable<HIRStmt, HIRStmtID> statements;
    IDTable<HIRPattern, HIRPatternID> patterns;
    IDTable<HIRBlock, HIRBlockID> blocks;
    IDTable<HIRBlockFacts, HIRBlockID> block_facts;
    IDTable<SemanticScope, SemanticScopeID> scopes;
    IDTable<SemanticPlace, SemanticPlaceID> places;
    IDTable<HIRFunctionDecl, FunctionID> functions;
    IDTable<HIRBody, BodyID> bodies;
    IDTable<HIRTestDecl, TestID> tests;
    IDTable<HIRStructDecl, StructID> structures;
    IDTable<HIREnumDecl, EnumID> enumerations;
    IDTable<HIREnumCase, EnumCaseID> enum_cases;
    IDTable<HIRModule, ProgramModuleID> modules;
    IDTable<HIRSymbol, SymbolID> symbols;
    IDTable<HIRFailureSet, FailureSetID> failure_sets;
    std::flat_map<HIRFailureSet, FailureSetID> failure_set_index;
    IDTable<HIRCallableSignature, CallableSignatureID> callable_signatures;
    std::flat_map<HIRCallableSignature, CallableSignatureID> callable_signature_index;
    IDTable<HIRCallable, CallableID> callables;
    HIRNominalStorage nominal_storage;

    friend class SemanticConstruction;
    friend class SemanticProgram;
};

class SemanticProgram final {
public:
    SemanticProgram(const SemanticProgram&) = delete;
    SemanticProgram(SemanticProgram&&) = default;
    ~SemanticProgram() = default;

    auto operator=(const SemanticProgram&) -> SemanticProgram& = delete;
    auto operator=(SemanticProgram&&) -> SemanticProgram& = default;

    auto provenance() const noexcept -> CompilationProvenanceView;
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
    auto bodies() const noexcept -> std::span<const HIRBody>;
    auto tests() const noexcept -> std::span<const HIRTestDecl>;
    auto structures() const noexcept -> std::span<const HIRStructDecl>;
    auto enumerations() const noexcept -> std::span<const HIREnumDecl>;
    auto enum_cases() const noexcept -> std::span<const HIREnumCase>;
    auto modules() const noexcept -> std::span<const HIRModule>;
    auto expressions() const noexcept -> std::span<const HIRExpr>;
    auto constants() const noexcept -> std::span<const HIRConstantFact>;
    auto types() const noexcept -> std::span<const HIRType>;
    auto statements() const noexcept -> std::span<const HIRStmt>;
    auto patterns() const noexcept -> std::span<const HIRPattern>;
    auto blocks() const noexcept -> std::span<const HIRBlock>;
    auto scopes() const noexcept -> std::span<const SemanticScope>;
    auto places() const noexcept -> std::span<const SemanticPlace>;
    auto symbols() const noexcept -> std::span<const HIRSymbol>;
    auto failure_sets() const noexcept -> std::span<const HIRFailureSet>;
    auto callable_signatures() const noexcept -> std::span<const HIRCallableSignature>;
    auto callables() const noexcept -> std::span<const HIRCallable>;
    auto nominal_storage_order() const noexcept -> std::span<const HIRNominalDeclRef>;
    auto nominal_storage_dependencies(HIRNominalDeclRef declaration) const noexcept
        -> std::span<const HIRNominalDeclRef>;

private:
    SemanticProgram(CompilationProvenance provenance, HIRStorage storage) noexcept;

    CompilationProvenance compilation_provenance;
    HIRStorage storage;

    friend class SemanticConstruction;
};
