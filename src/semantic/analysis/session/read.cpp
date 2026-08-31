module carven:semantic.analysis.session.read.impl;

import :semantic.analysis.session.read;

SemanticDraftView::SemanticDraftView(const SemanticDraft& draft) noexcept
    : semantic_draft(std::addressof(draft)) {}

auto SemanticDraftView::provenance() const noexcept -> CompilationProvenanceView {
    return semantic_draft->provenance();
}

auto SemanticDraftView::type(HIRTypeID id) const noexcept -> const HIRType& {
    return semantic_draft->type(id);
}

auto SemanticDraftView::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return semantic_draft->expression(id);
}

auto SemanticDraftView::expression_control(HIRExprID id) const noexcept
    -> const HIRExpressionControl& {
    return semantic_draft->expression_control(id);
}

auto SemanticDraftView::evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect& {
    return semantic_draft->evaluation_effect(id);
}

auto SemanticDraftView::place_use(HIRExprID id) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    return semantic_draft->place_use(id);
}

auto SemanticDraftView::try_facts(HIRExprID id) const noexcept
    -> const std::optional<HIRTryFacts>& {
    return semantic_draft->try_facts(id);
}

auto SemanticDraftView::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return semantic_draft->constant(id);
}

auto SemanticDraftView::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return semantic_draft->statement(id);
}

auto SemanticDraftView::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return semantic_draft->pattern(id);
}

auto SemanticDraftView::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return semantic_draft->block(id);
}

auto SemanticDraftView::block_control(HIRBlockID id) const noexcept -> const HIRBlockControl& {
    return semantic_draft->block_control(id);
}

auto SemanticDraftView::scope(SemanticScopeID id) const noexcept -> const SemanticScope& {
    return semantic_draft->scope(id);
}

auto SemanticDraftView::binding(SymbolID id) const noexcept
    -> const std::optional<SemanticBindingFacts>& {
    return semantic_draft->binding(id);
}

auto SemanticDraftView::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    return semantic_draft->function(id);
}

auto SemanticDraftView::has_body(BodyID id) const noexcept -> bool {
    return semantic_draft->has_body(id);
}

auto SemanticDraftView::body(BodyID id) const noexcept -> const HIRBody& {
    return semantic_draft->body(id);
}

auto SemanticDraftView::test(TestID id) const noexcept -> const HIRTestDecl& {
    return semantic_draft->test(id);
}

auto SemanticDraftView::structure(StructID id) const noexcept -> const HIRStructDecl& {
    return semantic_draft->structure(id);
}

auto SemanticDraftView::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return semantic_draft->enumeration(id);
}

auto SemanticDraftView::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return semantic_draft->enum_case(id);
}

auto SemanticDraftView::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return semantic_draft->symbol(id);
}

auto SemanticDraftView::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return semantic_draft->hir_module(id);
}

auto SemanticDraftView::failure_set(FailureSetID id) const noexcept -> const HIRFailureSet& {
    return semantic_draft->failure_set(id);
}

auto SemanticDraftView::callable_signature(CallableSignatureID id) const noexcept
    -> const HIRCallableSignature& {
    return semantic_draft->callable_signature(id);
}

auto SemanticDraftView::callable(CallableID id) const noexcept -> const HIRCallable& {
    return semantic_draft->callable(id);
}

auto SemanticDraftView::callable_flow(CallableID id) const noexcept -> const HIRCallableFlow& {
    return semantic_draft->callable_flow(id);
}

auto SemanticDraftView::callable_failure_input(CallableID id) const noexcept
    -> const SemanticCallableFailureInput& {
    return semantic_draft->callable_failure_input(id);
}

auto SemanticDraftView::symbol_constant(SymbolID id) const noexcept
    -> std::optional<HIRConstantID> {
    return semantic_draft->symbol_constant(id);
}

auto SemanticDraftView::symbol_role(SymbolID id) const noexcept -> SemanticSymbolRole {
    return semantic_draft->symbol_role(id);
}

auto SemanticDraftView::symbol_write_eligible(SymbolID id) const noexcept -> bool {
    return semantic_draft->symbol_write_eligible(id);
}

auto SemanticDraftView::symbol_states() const noexcept -> std::span<const SemanticSymbolState> {
    return semantic_draft->symbol_states();
}

auto SemanticDraftView::place_use_candidate(HIRExprID id) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    return semantic_draft->place_use_candidate(id);
}

auto SemanticDraftView::functions() const noexcept -> std::span<const HIRFunctionDecl> {
    return semantic_draft->functions();
}

auto SemanticDraftView::bodies() const noexcept -> std::span<const std::optional<HIRBody>> {
    return semantic_draft->bodies();
}

auto SemanticDraftView::tests() const noexcept -> std::span<const HIRTestDecl> {
    return semantic_draft->tests();
}

auto SemanticDraftView::structures() const noexcept -> std::span<const HIRStructDecl> {
    return semantic_draft->structures();
}

auto SemanticDraftView::enumerations() const noexcept -> std::span<const HIREnumDecl> {
    return semantic_draft->enumerations();
}

auto SemanticDraftView::enum_cases() const noexcept -> std::span<const HIREnumCase> {
    return semantic_draft->enum_cases();
}

auto SemanticDraftView::modules() const noexcept -> std::span<const HIRModule> {
    return semantic_draft->modules();
}

auto SemanticDraftView::expressions() const noexcept -> std::span<const HIRExpr> {
    return semantic_draft->expressions();
}

auto SemanticDraftView::expression_controls() const noexcept
    -> std::span<const HIRExpressionControl> {
    return semantic_draft->expression_controls();
}

auto SemanticDraftView::evaluation_effects() const noexcept -> std::span<const EvaluationEffect> {
    return semantic_draft->evaluation_effects();
}

auto SemanticDraftView::place_uses() const noexcept
    -> std::span<const std::optional<SemanticPlaceUse>> {
    return semantic_draft->place_uses();
}

auto SemanticDraftView::try_facts() const noexcept -> std::span<const std::optional<HIRTryFacts>> {
    return semantic_draft->try_facts();
}

auto SemanticDraftView::constants() const noexcept -> std::span<const HIRConstantFact> {
    return semantic_draft->constants();
}

auto SemanticDraftView::types() const noexcept -> std::span<const HIRType> {
    return semantic_draft->types();
}

auto SemanticDraftView::statements() const noexcept -> std::span<const HIRStmt> {
    return semantic_draft->statements();
}

auto SemanticDraftView::patterns() const noexcept -> std::span<const HIRPattern> {
    return semantic_draft->patterns();
}

auto SemanticDraftView::blocks() const noexcept -> std::span<const HIRBlock> {
    return semantic_draft->blocks();
}

auto SemanticDraftView::block_controls() const noexcept -> std::span<const HIRBlockControl> {
    return semantic_draft->block_controls();
}

auto SemanticDraftView::scopes() const noexcept -> std::span<const SemanticScope> {
    return semantic_draft->scopes();
}

auto SemanticDraftView::bindings() const noexcept
    -> std::span<const std::optional<SemanticBindingFacts>> {
    return semantic_draft->bindings();
}

auto SemanticDraftView::symbol_count() const noexcept -> std::size_t {
    return semantic_draft->symbol_count();
}

auto SemanticDraftView::failure_sets() const noexcept -> std::span<const HIRFailureSet> {
    return semantic_draft->failure_sets();
}

auto SemanticDraftView::callable_signatures() const noexcept
    -> std::span<const HIRCallableSignature> {
    return semantic_draft->callable_signatures();
}

auto SemanticDraftView::callables() const noexcept -> std::span<const HIRCallable> {
    return semantic_draft->callables();
}

auto SemanticDraftView::callable_flows() const noexcept -> std::span<const HIRCallableFlow> {
    return semantic_draft->callable_flows();
}

auto SemanticDraftView::callable_failure_inputs() const noexcept
    -> std::span<const SemanticCallableFailureInput> {
    return semantic_draft->callable_failure_inputs();
}

auto SemanticDraftView::nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
    -> const HIRNominalCapabilities& {
    return semantic_draft->nominal_capabilities(declaration);
}

auto SemanticDraftView::structure_capabilities() const noexcept
    -> std::span<const HIRNominalCapabilities> {
    return semantic_draft->structure_capabilities();
}

auto SemanticDraftView::enumeration_capabilities() const noexcept
    -> std::span<const HIRNominalCapabilities> {
    return semantic_draft->enumeration_capabilities();
}

auto SemanticDraftView::nominal_dependency_sets() const noexcept
    -> std::span<const std::vector<HIRNominalDeclRef>> {
    return semantic_draft->nominal_dependency_sets();
}

auto SemanticDraftView::nominal_containment(HIRNominalDeclRef declaration) const noexcept
    -> std::span<const HIRNominalDeclRef> {
    return semantic_draft->nominal_containment(declaration);
}
