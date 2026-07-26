module carven:semantic.hir.impl;

import :semantic.hir;
import :support.visit;
import std;

SemanticProgram::SemanticProgram(
    CompilationProvenance provenance,
    HIRStorage storage_value
) noexcept
    : compilation_provenance(std::move(provenance)),
      storage(std::move(storage_value)) {}

auto SemanticProgram::provenance() const noexcept -> CompilationProvenanceView {
    return compilation_provenance.view();
}

auto SemanticProgram::type(HIRTypeID id) const noexcept -> const HIRType& {
    return storage.types.get(id);
}

auto SemanticProgram::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return storage.expressions.get(id);
}

auto SemanticProgram::expression_facts(HIRExprID id) const noexcept -> const HIRExpressionFacts& {
    return storage.expression_facts.get(id);
}

auto SemanticProgram::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return storage.constants.get(id);
}

auto SemanticProgram::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return storage.statements.get(id);
}

auto SemanticProgram::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return storage.patterns.get(id);
}

auto SemanticProgram::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return storage.blocks.get(id);
}

auto SemanticProgram::block_facts(HIRBlockID id) const noexcept -> const HIRBlockFacts& {
    return storage.block_facts.get(id);
}

auto SemanticProgram::scope(SemanticScopeID id) const noexcept -> const SemanticScope& {
    return storage.scopes.get(id);
}

auto SemanticProgram::place(SemanticPlaceID id) const noexcept -> const SemanticPlace& {
    return storage.places.get(id);
}

auto SemanticProgram::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    return storage.functions.get(id);
}

auto SemanticProgram::body(BodyID id) const noexcept -> const HIRBody& {
    return storage.bodies.get(id);
}

auto SemanticProgram::test(TestID id) const noexcept -> const HIRTestDecl& {
    return storage.tests.get(id);
}

auto SemanticProgram::structure(StructID id) const noexcept -> const HIRStructDecl& {
    return storage.structures.get(id);
}

auto SemanticProgram::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return storage.enumerations.get(id);
}

auto SemanticProgram::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return storage.enum_cases.get(id);
}

auto SemanticProgram::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return storage.symbols.get(id);
}

auto SemanticProgram::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return storage.modules.get(id);
}

auto SemanticProgram::failure_set(FailureSetID id) const noexcept -> const HIRFailureSet& {
    return storage.failure_sets.get(id);
}

auto SemanticProgram::callable_signature(CallableSignatureID id) const noexcept
    -> const HIRCallableSignature& {
    return storage.callable_signatures.get(id);
}

auto SemanticProgram::callable(CallableID id) const noexcept -> const HIRCallable& {
    return storage.callables.get(id);
}

auto SemanticProgram::functions() const noexcept -> std::span<const HIRFunctionDecl> {
    return storage.functions.values();
}

auto SemanticProgram::bodies() const noexcept -> std::span<const HIRBody> {
    return storage.bodies.values();
}

auto SemanticProgram::tests() const noexcept -> std::span<const HIRTestDecl> {
    return storage.tests.values();
}

auto SemanticProgram::structures() const noexcept -> std::span<const HIRStructDecl> {
    return storage.structures.values();
}

auto SemanticProgram::enumerations() const noexcept -> std::span<const HIREnumDecl> {
    return storage.enumerations.values();
}

auto SemanticProgram::enum_cases() const noexcept -> std::span<const HIREnumCase> {
    return storage.enum_cases.values();
}

auto SemanticProgram::modules() const noexcept -> std::span<const HIRModule> {
    return storage.modules.values();
}

auto SemanticProgram::expressions() const noexcept -> std::span<const HIRExpr> {
    return storage.expressions.values();
}

auto SemanticProgram::constants() const noexcept -> std::span<const HIRConstantFact> {
    return storage.constants.values();
}

auto SemanticProgram::types() const noexcept -> std::span<const HIRType> {
    return storage.types.values();
}

auto SemanticProgram::statements() const noexcept -> std::span<const HIRStmt> {
    return storage.statements.values();
}

auto SemanticProgram::patterns() const noexcept -> std::span<const HIRPattern> {
    return storage.patterns.values();
}

auto SemanticProgram::blocks() const noexcept -> std::span<const HIRBlock> {
    return storage.blocks.values();
}

auto SemanticProgram::scopes() const noexcept -> std::span<const SemanticScope> {
    return storage.scopes.values();
}

auto SemanticProgram::places() const noexcept -> std::span<const SemanticPlace> {
    return storage.places.values();
}

auto SemanticProgram::symbols() const noexcept -> std::span<const HIRSymbol> {
    return storage.symbols.values();
}

auto SemanticProgram::failure_sets() const noexcept -> std::span<const HIRFailureSet> {
    return storage.failure_sets.values();
}

auto SemanticProgram::callable_signatures() const noexcept
    -> std::span<const HIRCallableSignature> {
    return storage.callable_signatures.values();
}

auto SemanticProgram::callables() const noexcept -> std::span<const HIRCallable> {
    return storage.callables.values();
}

auto SemanticProgram::nominal_storage_order() const noexcept -> std::span<const HIRNominalDeclRef> {
    return storage.nominal_storage.order;
}

auto SemanticProgram::nominal_storage_dependencies(HIRNominalDeclRef declaration) const noexcept
    -> std::span<const HIRNominalDeclRef> {
    const auto index = std::visit(
        Overloaded {
            [](StructID id) static noexcept -> std::size_t { return id.index(); },
            [&](EnumID id) noexcept -> std::size_t {
                return storage.structures.size() + id.index();
            },
        },
        declaration
    );
    if (index >= storage.nominal_storage.direct_dependencies.size()) {
        std::unreachable();
    }
    return storage.nominal_storage.direct_dependencies[index];
}
