module carven:semantic.hir.impl;

import :semantic.hir;
import :support.visit;
import std;

SemanticProgramView::SemanticProgramView(
    CompilationProvenanceView provenance,
    const SemanticProgramStorage& storage
) noexcept
    : compilation_provenance(provenance),
      semantic_storage(&storage) {}

auto SemanticProgramView::provenance() const noexcept -> CompilationProvenanceView {
    return compilation_provenance;
}

auto SemanticProgramView::type(HIRTypeID id) const noexcept -> const HIRType& {
    return semantic_storage->types.get(id);
}

auto SemanticProgramView::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return semantic_storage->expressions.get(id);
}

auto SemanticProgramView::expression_control(HIRExprID id) const noexcept
    -> const HIRExpressionControl& {
    return semantic_storage->expression_controls.get(id);
}

auto SemanticProgramView::evaluation_effect(HIRExprID id) const noexcept
    -> const EvaluationEffect& {
    return semantic_storage->evaluation_effects.get(id);
}

auto SemanticProgramView::place_use(HIRExprID id) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    return semantic_storage->place_uses.get(id);
}

auto SemanticProgramView::try_facts(HIRExprID id) const noexcept
    -> const std::optional<HIRTryFacts>& {
    return semantic_storage->try_facts.get(id);
}

auto SemanticProgramView::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return semantic_storage->constants.get(id);
}

auto SemanticProgramView::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return semantic_storage->statements.get(id);
}

auto SemanticProgramView::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return semantic_storage->patterns.get(id);
}

auto SemanticProgramView::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return semantic_storage->blocks.get(id);
}

auto SemanticProgramView::block_control(HIRBlockID id) const noexcept -> const HIRBlockControl& {
    return semantic_storage->block_controls.get(id);
}

auto SemanticProgramView::scope(SemanticScopeID id) const noexcept -> const SemanticScope& {
    return semantic_storage->scopes.get(id);
}

auto SemanticProgramView::binding(SymbolID id) const noexcept
    -> const std::optional<SemanticBindingFacts>& {
    return semantic_storage->bindings.get(id);
}

auto SemanticProgramView::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    return semantic_storage->functions.get(id);
}

auto SemanticProgramView::has_body(BodyID id) const noexcept -> bool {
    return semantic_storage->bodies.contains(id);
}

auto SemanticProgramView::body(BodyID id) const noexcept -> const HIRBody& {
    return semantic_storage->bodies.get(id);
}

auto SemanticProgramView::test(TestID id) const noexcept -> const HIRTestDecl& {
    return semantic_storage->tests.get(id);
}

auto SemanticProgramView::structure(StructID id) const noexcept -> const HIRStructDecl& {
    return semantic_storage->structures.get(id);
}

auto SemanticProgramView::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return semantic_storage->enumerations.get(id);
}

auto SemanticProgramView::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return semantic_storage->enum_cases.get(id);
}

auto SemanticProgramView::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return semantic_storage->symbols.get(id);
}

auto SemanticProgramView::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return semantic_storage->modules.get(id);
}

auto SemanticProgramView::failure_set(FailureSetID id) const noexcept -> const HIRFailureSet& {
    return semantic_storage->failure_sets.get(id);
}

auto SemanticProgramView::callable_signature(CallableSignatureID id) const noexcept
    -> const HIRCallableSignature& {
    return semantic_storage->callable_signatures.get(id);
}

auto SemanticProgramView::callable(CallableID id) const noexcept -> const HIRCallable& {
    return semantic_storage->callables.get(id);
}

auto SemanticProgramView::callable_flow(CallableID id) const noexcept -> const HIRCallableFlow& {
    return semantic_storage->callable_flows.get(id);
}

auto SemanticProgramView::nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
    -> const HIRNominalCapabilities& {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept -> const HIRNominalCapabilities& {
                return semantic_storage->struct_capabilities.get(id);
            },
            [&](EnumID id) noexcept -> const HIRNominalCapabilities& {
                return semantic_storage->enum_capabilities.get(id);
            },
        },
        declaration
    );
}

auto SemanticProgramView::nominal_containment(HIRNominalDeclRef declaration) const noexcept
    -> std::span<const HIRNominalDeclRef> {
    const auto index = std::visit(
        Overloaded {
            [](StructID id) static noexcept -> std::size_t { return id.index(); },
            [&](EnumID id) noexcept -> std::size_t {
                return semantic_storage->structures.size() + id.index();
            },
        },
        declaration
    );
    if (index >= semantic_storage->nominal_containment.direct_dependencies.size()) {
        std::unreachable();
    }
    return semantic_storage->nominal_containment.direct_dependencies[index];
}

auto SemanticProgramView::structure_capabilities() const noexcept
    -> std::span<const HIRNominalCapabilities> {
    return semantic_storage->struct_capabilities.values();
}

auto SemanticProgramView::enumeration_capabilities() const noexcept
    -> std::span<const HIRNominalCapabilities> {
    return semantic_storage->enum_capabilities.values();
}

auto SemanticProgramView::nominal_dependency_sets() const noexcept
    -> std::span<const std::vector<HIRNominalDeclRef>> {
    return semantic_storage->nominal_containment.direct_dependencies;
}

auto SemanticProgramView::functions() const noexcept -> std::span<const HIRFunctionDecl> {
    return semantic_storage->functions.values();
}

auto SemanticProgramView::bodies() const noexcept -> std::span<const HIRBody> {
    return semantic_storage->bodies.values();
}

auto SemanticProgramView::tests() const noexcept -> std::span<const HIRTestDecl> {
    return semantic_storage->tests.values();
}

auto SemanticProgramView::structures() const noexcept -> std::span<const HIRStructDecl> {
    return semantic_storage->structures.values();
}

auto SemanticProgramView::enumerations() const noexcept -> std::span<const HIREnumDecl> {
    return semantic_storage->enumerations.values();
}

auto SemanticProgramView::enum_cases() const noexcept -> std::span<const HIREnumCase> {
    return semantic_storage->enum_cases.values();
}

auto SemanticProgramView::modules() const noexcept -> std::span<const HIRModule> {
    return semantic_storage->modules.values();
}

auto SemanticProgramView::expressions() const noexcept -> std::span<const HIRExpr> {
    return semantic_storage->expressions.values();
}

auto SemanticProgramView::expression_controls() const noexcept
    -> std::span<const HIRExpressionControl> {
    return semantic_storage->expression_controls.values();
}

auto SemanticProgramView::evaluation_effects() const noexcept -> std::span<const EvaluationEffect> {
    return semantic_storage->evaluation_effects.values();
}

auto SemanticProgramView::place_uses() const noexcept
    -> std::span<const std::optional<SemanticPlaceUse>> {
    return semantic_storage->place_uses.values();
}

auto SemanticProgramView::try_facts() const noexcept
    -> std::span<const std::optional<HIRTryFacts>> {
    return semantic_storage->try_facts.values();
}

auto SemanticProgramView::constants() const noexcept -> std::span<const HIRConstantFact> {
    return semantic_storage->constants.values();
}

auto SemanticProgramView::types() const noexcept -> std::span<const HIRType> {
    return semantic_storage->types.values();
}

auto SemanticProgramView::statements() const noexcept -> std::span<const HIRStmt> {
    return semantic_storage->statements.values();
}

auto SemanticProgramView::patterns() const noexcept -> std::span<const HIRPattern> {
    return semantic_storage->patterns.values();
}

auto SemanticProgramView::blocks() const noexcept -> std::span<const HIRBlock> {
    return semantic_storage->blocks.values();
}

auto SemanticProgramView::block_controls() const noexcept -> std::span<const HIRBlockControl> {
    return semantic_storage->block_controls.values();
}

auto SemanticProgramView::scopes() const noexcept -> std::span<const SemanticScope> {
    return semantic_storage->scopes.values();
}

auto SemanticProgramView::bindings() const noexcept
    -> std::span<const std::optional<SemanticBindingFacts>> {
    return semantic_storage->bindings.values();
}

auto SemanticProgramView::symbol_count() const noexcept -> std::size_t {
    return semantic_storage->symbols.size();
}

auto SemanticProgramView::symbols() const noexcept -> std::span<const HIRSymbol> {
    return semantic_storage->symbols.values();
}

auto SemanticProgramView::failure_sets() const noexcept -> std::span<const HIRFailureSet> {
    return semantic_storage->failure_sets.values();
}

auto SemanticProgramView::callable_signatures() const noexcept
    -> std::span<const HIRCallableSignature> {
    return semantic_storage->callable_signatures.values();
}

auto SemanticProgramView::callables() const noexcept -> std::span<const HIRCallable> {
    return semantic_storage->callables.values();
}

auto SemanticProgramView::callable_flows() const noexcept -> std::span<const HIRCallableFlow> {
    return semantic_storage->callable_flows.values();
}

SemanticProgram::SemanticProgram(
    CompilationProvenance provenance,
    SemanticProgramStorage storage
) noexcept
    : compilation_provenance(std::move(provenance)),
      semantic_storage(std::move(storage)) {}

auto SemanticProgram::view() const noexcept -> SemanticProgramView {
    return SemanticProgramView(compilation_provenance.view(), semantic_storage);
}

auto SemanticProgram::provenance() const noexcept -> CompilationProvenanceView {
    return view().provenance();
}

auto SemanticProgram::type(HIRTypeID id) const noexcept -> const HIRType& {
    return view().type(id);
}

auto SemanticProgram::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return view().expression(id);
}

auto SemanticProgram::expression_control(HIRExprID id) const noexcept
    -> const HIRExpressionControl& {
    return view().expression_control(id);
}

auto SemanticProgram::evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect& {
    return view().evaluation_effect(id);
}

auto SemanticProgram::place_use(HIRExprID id) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    return view().place_use(id);
}

auto SemanticProgram::try_facts(HIRExprID id) const noexcept -> const std::optional<HIRTryFacts>& {
    return view().try_facts(id);
}

auto SemanticProgram::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return view().constant(id);
}

auto SemanticProgram::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return view().statement(id);
}

auto SemanticProgram::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return view().pattern(id);
}

auto SemanticProgram::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return view().block(id);
}

auto SemanticProgram::block_control(HIRBlockID id) const noexcept -> const HIRBlockControl& {
    return view().block_control(id);
}

auto SemanticProgram::scope(SemanticScopeID id) const noexcept -> const SemanticScope& {
    return view().scope(id);
}

auto SemanticProgram::binding(SymbolID id) const noexcept
    -> const std::optional<SemanticBindingFacts>& {
    return view().binding(id);
}

auto SemanticProgram::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    return view().function(id);
}

auto SemanticProgram::body(BodyID id) const noexcept -> const HIRBody& {
    return view().body(id);
}

auto SemanticProgram::test(TestID id) const noexcept -> const HIRTestDecl& {
    return view().test(id);
}

auto SemanticProgram::structure(StructID id) const noexcept -> const HIRStructDecl& {
    return view().structure(id);
}

auto SemanticProgram::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return view().enumeration(id);
}

auto SemanticProgram::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return view().enum_case(id);
}

auto SemanticProgram::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return view().symbol(id);
}

auto SemanticProgram::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return view().hir_module(id);
}

auto SemanticProgram::failure_set(FailureSetID id) const noexcept -> const HIRFailureSet& {
    return view().failure_set(id);
}

auto SemanticProgram::callable_signature(CallableSignatureID id) const noexcept
    -> const HIRCallableSignature& {
    return view().callable_signature(id);
}

auto SemanticProgram::callable(CallableID id) const noexcept -> const HIRCallable& {
    return view().callable(id);
}

auto SemanticProgram::callable_flow(CallableID id) const noexcept -> const HIRCallableFlow& {
    return view().callable_flow(id);
}

auto SemanticProgram::nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
    -> const HIRNominalCapabilities& {
    return view().nominal_capabilities(declaration);
}

auto SemanticProgram::nominal_containment(HIRNominalDeclRef declaration) const noexcept
    -> std::span<const HIRNominalDeclRef> {
    return view().nominal_containment(declaration);
}

auto SemanticProgram::functions() const noexcept -> std::span<const HIRFunctionDecl> {
    return view().functions();
}

auto SemanticProgram::bodies() const noexcept -> std::span<const HIRBody> {
    return view().bodies();
}

auto SemanticProgram::tests() const noexcept -> std::span<const HIRTestDecl> {
    return view().tests();
}

auto SemanticProgram::structures() const noexcept -> std::span<const HIRStructDecl> {
    return view().structures();
}

auto SemanticProgram::enumerations() const noexcept -> std::span<const HIREnumDecl> {
    return view().enumerations();
}

auto SemanticProgram::enum_cases() const noexcept -> std::span<const HIREnumCase> {
    return view().enum_cases();
}

auto SemanticProgram::modules() const noexcept -> std::span<const HIRModule> {
    return view().modules();
}

auto SemanticProgram::expressions() const noexcept -> std::span<const HIRExpr> {
    return view().expressions();
}

auto SemanticProgram::expression_controls() const noexcept
    -> std::span<const HIRExpressionControl> {
    return view().expression_controls();
}

auto SemanticProgram::evaluation_effects() const noexcept -> std::span<const EvaluationEffect> {
    return view().evaluation_effects();
}

auto SemanticProgram::place_uses() const noexcept
    -> std::span<const std::optional<SemanticPlaceUse>> {
    return view().place_uses();
}

auto SemanticProgram::try_facts() const noexcept -> std::span<const std::optional<HIRTryFacts>> {
    return view().try_facts();
}

auto SemanticProgram::constants() const noexcept -> std::span<const HIRConstantFact> {
    return view().constants();
}

auto SemanticProgram::types() const noexcept -> std::span<const HIRType> {
    return view().types();
}

auto SemanticProgram::statements() const noexcept -> std::span<const HIRStmt> {
    return view().statements();
}

auto SemanticProgram::patterns() const noexcept -> std::span<const HIRPattern> {
    return view().patterns();
}

auto SemanticProgram::blocks() const noexcept -> std::span<const HIRBlock> {
    return view().blocks();
}

auto SemanticProgram::block_controls() const noexcept -> std::span<const HIRBlockControl> {
    return view().block_controls();
}

auto SemanticProgram::scopes() const noexcept -> std::span<const SemanticScope> {
    return view().scopes();
}

auto SemanticProgram::bindings() const noexcept
    -> std::span<const std::optional<SemanticBindingFacts>> {
    return view().bindings();
}

auto SemanticProgram::symbols() const noexcept -> std::span<const HIRSymbol> {
    return view().symbols();
}

auto SemanticProgram::failure_sets() const noexcept -> std::span<const HIRFailureSet> {
    return view().failure_sets();
}

auto SemanticProgram::callable_signatures() const noexcept
    -> std::span<const HIRCallableSignature> {
    return view().callable_signatures();
}

auto SemanticProgram::callables() const noexcept -> std::span<const HIRCallable> {
    return view().callables();
}

auto SemanticProgram::callable_flows() const noexcept -> std::span<const HIRCallableFlow> {
    return view().callable_flows();
}
