module carven:semantic.analysis.body.construction.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.suggestion;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.async;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.extent;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.source.builder;
import :semantic.analysis.stage.session;
import :semantic.analysis.types.display;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.standard;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

BodyElaborator::BodyElaborator(
    BodyBatchElaborator& owner,
    ProgramModuleID source_module_id,
    ModuleID semantic_module_id,
    ASTView source,
    BodyReservation&& reservation,
    std::optional<ConstructionTypeRef> result,
    FailureTermID outward_failure_term_id,
    bool accepts_catch_residual,
    bool test_body,
    std::optional<GenericTypeContext> generic_context,
    bool observe_source
) noexcept
    : batch(std::addressof(owner)),
      source_module_id(source_module_id),
      semantic_module_id(semantic_module_id),
      ast(source),
      generic_context(generic_context),
      body_builder(std::move(reservation), *owner.draft),
      result_type(result),
      infer_result(!result.has_value()),
      outward_failure_term_id(outward_failure_term_id),
      is_test(test_body),
      dead_failure_context {owner.draft->add_empty_failure_term(), true},
      observe_sources(owner.draft->source_analysis() != nullptr && observe_source),
      reachable(true),
      reference_path_reachable(true),
      reported_unreachable(false) {
    const auto root_origin = origin(ast.ast_module().span);
    const auto root_lifetime =
        body_builder.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, root_origin);
    frames.push_back(
        BodyLocalFrame {
            .lifetime = root_lifetime,
            .names = {},
        }
    );
    body_builder.set_lifetime(root_lifetime);
    regions.push_back(empty_region(ast.ast_module().span));
    failure_contexts.push_back({outward_failure_term_id, accepts_catch_residual});
    batch->active_builders.emplace(body_builder.identity(), std::addressof(body_builder));
}

BodyElaborator::~BodyElaborator() noexcept {
    batch->active_builders.erase(body_builder.identity());
}

auto BodyElaborator::finish_body(SemanticRegion region) noexcept -> StructuredBodyDraft {
    batch->active_builders.erase(body_builder.identity());
    return std::move(body_builder).finish(std::move(region));
}

auto BodyElaborator::observe_source(
    Span location,
    std::optional<SourceSpan> definition,
    std::optional<ConstructionTypeRef> type
) noexcept -> void {
    if (!observe_sources || location.empty()) {
        return;
    }
    source_occurrences.record(draft(), locate(ast.source_id(), location), definition, type);
}

auto BodyElaborator::draft() const noexcept -> ProgramDraft& {
    return *batch->draft;
}

auto BodyElaborator::catalog() const noexcept -> AnalysisCatalogView {
    return batch->catalog_data;
}

auto BodyElaborator::import_usage() const noexcept -> ImportUsage& {
    return *batch->imports;
}

auto BodyElaborator::origin(Span span) noexcept -> ProgramOriginID {
    return draft().append_source_origin(draft().module_source(source_module_id), span);
}

auto BodyElaborator::expansion(Span span, ProgramExpansionReason reason) noexcept
    -> ProgramOriginID {
    return draft().append_expansion_origin(origin(span), reason);
}

auto BodyElaborator::spelling(Span span) const noexcept -> std::string {
    return draft().source_slice_copy(source_module_id, span);
}

auto BodyElaborator::fail(Span span, DiagnosticCode code, std::string message) noexcept
    -> AnalysisFailure {
    return draft().diagnostics().error(
        DiagnosticBuilder(code, std::move(message)).primary(locate(ast.source_id(), span)).build()
    );
}

auto BodyElaborator::warn(Span span, DiagnosticCode code, std::string message) noexcept -> void {
    draft().diagnostics().warning(
        DiagnosticBuilder(code, std::move(message)).primary(locate(ast.source_id(), span)).build()
    );
}

auto BodyElaborator::resolve_array_extent(ASTExprID id) noexcept -> AnalysisTask<std::uint64_t> {
    auto scope = BodyExprSite(*this);
    co_return (co_await evaluate_array_extent(draft(), ast, scope, id));
}

auto BodyElaborator::build_static_expression(
    ASTExprID source,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<SemanticExpression> {
    const auto span = ast.expression(source).span;
    const auto outer_reachable = reachable;
    auto enclosing_catches = std::exchange(catches, {});
    static_scope_frames.push_back(frames.size());
    failure_contexts.push_back({draft().add_empty_failure_term(), false});
    reachable = true;
    auto built = co_await expression(source, expected);
    failure_contexts.pop_back();
    static_scope_frames.pop_back();
    catches = std::move(enclosing_catches);
    reachable = outer_reachable;
    if (!built) {
        co_return std::unexpected(built.error());
    }
    if (expected) {
        if (auto checked = coerce_to(*built, *expected, span); !checked) {
            co_return std::unexpected(checked.error());
        }
    } else {
        if (auto inferred = infer_value_type(*built, span); !inferred) {
            co_return std::unexpected(inferred.error());
        }
    }
    co_return consume_value(*built, span, AccessMode::Read);
}

auto BodyElaborator::static_expression(
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<BuiltExpression> {
    if (static_stage()) {
        co_return co_await this->expression(expression, expected);
    }
    auto result = co_await build_static_expression(expression, expected);
    if (!result) {
        co_return std::unexpected(result.error());
    }
    co_return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {std::move(*result)}},
        .pending_failures = {},
        .takeable = false,
        .completes = true,
    };
}

auto BodyElaborator::read_local(const BodyLocalStorage& local, Span span) noexcept
    -> AnalysisTask<std::optional<SemanticExpression>> {
    const auto binding = local.storage.binding;
    if (binding.owner() != active_builder().identity()) {
        auto constant = co_await compute_static_binding(binding);
        if (!constant) {
            co_return std::unexpected(constant.error());
        }
        if (!*constant) {
            co_return std::nullopt;
        }
        auto value = active_builder().make_expression(
            draft().constant(**constant).type,
            active_builder().lifetime(),
            origin(span),
            constant_operation(draft(), **constant)
        );
        for (const auto index : local.projection) {
            const auto elements = tuple_elements(draft(), value.type.construction());
            value = active_builder().make_expression(
                (*elements)[index],
                active_builder().lifetime(),
                origin(span),
                SemTupleElement {UniqueIndirect(std::move(value)), index}
            );
        }
        co_return value;
    }
    auto value = local_place(local, span).expression;
    value.category = SemanticValueCategory::Value;
    value.lifetime = active_builder().lifetime();
    value.origin = origin(span);
    co_return value;
}

auto BodyElaborator::resolve_static_references(
    SemanticExpression& source,
    const StructuredBodyDraft* scope
) noexcept -> AnalysisTask<bool> {
    auto local_lifetimes = std::set<LifetimeRegionID>();
    visit_semantic_nodes(source, [&](const SemanticRegion& region) noexcept {
        local_lifetimes.insert(region.lifetime);
    });
    auto bindings = std::vector<SemanticExpression*>();
    visit_semantic_nodes(source, [&](SemanticExpression& expression) noexcept {
        if (std::holds_alternative<SemBinding>(expression.value)) {
            bindings.push_back(&expression);
        }
    });
    auto complete = true;
    for (auto* expression : bindings) {
        const auto binding = std::get<SemBinding>(expression->value).binding;
        const auto lifetime = scope ? scope->bindings.get(binding).lifetime
                                    : body_builder.binding_copy(binding).lifetime;
        if (local_lifetimes.contains(lifetime)) {
            continue;
        }
        auto value =
            co_await compute_static_binding(std::get<SemBinding>(expression->value).binding);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        if (!*value) {
            complete = false;
            continue;
        }
        expression->constant = **value;
        expression->value = constant_operation(draft(), **value);
    }
    co_return complete;
}

auto BodyElaborator::compute_static_binding(LocalBindingID binding) noexcept
    -> AnalysisTask<std::optional<ConstantID>> {
    const auto found = batch->static_roots.find(binding);
    if (found == batch->static_roots.end()) {
        co_return std::nullopt;
    }
    if (found->second.value) {
        co_return found->second.value;
    }
    auto initializer = found->second.initializer;
    auto region = SemanticRegion {
        .lifetime = initializer.lifetime,
        .origin = initializer.origin,
        .statements = {},
        .result = std::nullopt,
        .result_reachable = false,
        .failures = initializer.failures,
        .exits_test = initializer.exits_test,
    };
    const auto active = batch->active_builders.find(binding.owner());
    if (active == batch->active_builders.end()) {
        invariant_violation("local constant requires its active construction owner");
    }
    const auto scope = active->second->snapshot(region);
    auto complete = co_await resolve_static_references(initializer, &scope);
    if (!complete) {
        co_return std::unexpected(complete.error());
    }
    if (!*complete) {
        co_return std::nullopt;
    }
    region.result = initializer;
    region.result_reachable = true;
    auto ready = co_await construction_requests().close_generic_calls(region);
    if (!ready) {
        co_return std::unexpected(ready.error());
    }
    initializer = std::move(*region.result);
    const auto published_type = initializer.type.construction();
    auto result = co_await construction_requests().stage().evaluate(
        initializer,
        StaticEvaluationPurpose::ResolveType,
        &scope
    );
    if (!result) {
        co_return std::unexpected(result.error());
    }
    const auto frozen = freeze_constant_value(draft(), std::move(*result));
    if (!frozen || !compatible(draft().constant(*frozen).type, published_type)) {
        co_return std::unexpected(fail(
            draft().source_origin(initializer.origin).span,
            DiagnosticCode::ConstInitializer,
            "constant initializer has no frozen representation of its published type"
        ));
    }
    batch->static_roots.at(binding).value = *frozen;
    co_return *frozen;
}

auto BodyElaborator::construction_requests() noexcept -> ConstructionRequests& {
    return batch->requests;
}

auto BodyElaborator::resolve_number_parser(BuiltinType number, Span span) noexcept
    -> AnalysisTask<CallableID> {
    co_return co_await construction_requests().number_parser(number, source_module_id, span);
}

auto BodyElaborator::resolve_duration_type(Span span) noexcept -> AnalysisTask<TypeID> {
    co_return co_await construction_requests()
        .standard_type(StandardDeclaration::Duration, source_module_id, span);
}

auto BodyElaborator::resolve_type_qualifier(ASTExprID expression) noexcept
    -> AnalysisTask<std::optional<ConstructionTypeRef>> {
    auto current_id = expression;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(current_id).value)) {
        current_id = group->expression;
    }
    if (const auto* application =
            std::get_if<ASTTypeApplicationExpr>(&ast.expression(current_id).value)) {
        if (const auto* name =
                std::get_if<ASTNameExpr>(&ast.expression(application->operand_id).value);
            name && find_local(spelling(name->name_span)) != nullptr) {
            co_return std::optional<TypeID>();
        }
        const auto extent = [&](ASTExprID expression) noexcept {
            return resolve_array_extent(expression);
        };
        auto type = (co_await resolve_source_type_application(
            draft(),
            catalog(),
            import_usage(),
            source_module_id,
            ast,
            *application,
            extent,
            &batch->requests,
            generic_context ? std::addressof(*generic_context) : nullptr,
            observe_sources ? &source_occurrences : nullptr
        ));
        if (!type) {
            co_return std::unexpected(type.error());
        }
        co_return std::optional(*type);
    }
    const auto* name = std::get_if<ASTNameExpr>(&ast.expression(current_id).value);
    if (name == nullptr) {
        co_return std::optional<TypeID>();
    }
    const auto text = spelling(name->name_span);
    observe_source(name->name_span, std::nullopt, std::nullopt);
    if (find_local(text) != nullptr) {
        co_return std::optional<TypeID>();
    }
    if (generic_parameter_index(draft(), generic_context ? &*generic_context : nullptr, text)) {
        co_return std::unexpected(fail(
            name->name_span,
            DiagnosticCode::TypeGenericDefinition,
            "static type qualifier requires a concrete type"
        ));
    }
    auto selected = (co_await find_global(text, name->name_span));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    const auto* selected_symbol = catalog().symbol(*selected);
    if (const auto* provided = std::get_if<ProvidedExport>(&*selected)) {
        if (const auto builtin = provided_builtin_type(*provided)) {
            co_return std::optional(draft().builtin_type(*builtin));
        }
        co_return std::optional<TypeID>();
    }
    if (const auto* record =
            (selected_symbol ? std::get_if<CatalogStructForm>(&selected_symbol->form) : nullptr)) {
        co_return std::optional(
            draft().intern_type({.value = StructTypeValue {.structure = record->structure}})
        );
    }
    const auto* enumeration =
        (selected_symbol ? std::get_if<CatalogEnumForm>(&selected_symbol->form) : nullptr);
    if (enumeration == nullptr) {
        co_return std::optional<TypeID>();
    }
    co_return std::optional(
        draft().intern_type(
            CanonicalType {
                .value = EnumTypeValue {.enumeration = enumeration->enumeration},
            }
        )
    );
}

auto BodyElaborator::resolve_constant_enum_case(
    TypeID type,
    std::string_view name,
    Span span
) noexcept -> AnalysisTask<ResolvedEnumCase> {
    const auto canonical = draft().type_copy(type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    if (nominal == nullptr) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "scope qualifier does not name an enum or class type"
        ));
    }
    auto prepared = co_await construction_requests().ensure_type(type, source_module_id, span);
    if (!prepared) {
        co_return std::unexpected(prepared.error());
    }
    if (const auto case_id = find_enum_case(nominal->enumeration, name, span)) {
        const auto declaration = draft().construction_enum_case_declaration_copy(*case_id);
        const auto reference_type =
            enum_case_reference_type(draft(), type, declaration.payload_types);
        observe_source(span, std::nullopt, reference_type);
        co_return ResolvedEnumCase {
            .id = *case_id,
            .owner = declaration.owner,
            .reference_type = reference_type,
            .payload_types = declaration.payload_types,
            .constant = declaration.constant,
        };
    }
    co_return std::unexpected(fail(
        span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format(
            "enum '{}' has no case named '{}'{}",
            type_display_name(draft(), type, catalog()),
            name,
            spelling_suggestion(name, draft().enum_case_names(nominal->enumeration))
        )
    ));
}

auto BodyElaborator::resolve_type(ASTTypeID type) noexcept -> AnalysisTask<ConstructionTypeRef> {
    const auto resolve_extent = [&](ASTExprID extent) noexcept {
        return resolve_array_extent(extent);
    };
    auto result = (co_await resolve_source_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        type,
        resolve_extent,
        &batch->requests,
        generic_context ? std::addressof(*generic_context) : nullptr,
        observe_sources ? &source_occurrences : nullptr
    ));
    if (result) {
        auto prepared =
            (co_await batch->requests.ensure_type(*result, source_module_id, ast.type(type).span));
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
    }
    co_return result;
}

auto BodyElaborator::resolve_construction_type(const ASTConstructionType& type) noexcept
    -> AnalysisTask<ConstructionTypeRef> {
    const auto resolve_extent = [&](ASTExprID extent) noexcept {
        return resolve_array_extent(extent);
    };
    auto result = (co_await resolve_source_construction_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        type,
        resolve_extent,
        &batch->requests,
        generic_context ? std::addressof(*generic_context) : nullptr,
        observe_sources ? &source_occurrences : nullptr
    ));
    if (result) {
        auto prepared =
            (co_await batch->requests.ensure_type(*result, source_module_id, type.span));
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
    }
    co_return result;
}

auto BodyElaborator::compatible(ConstructionTypeRef left, ConstructionTypeRef right) const noexcept
    -> bool {
    return type_shapes_compatible(draft(), left, right);
}

auto BuiltExpression::is_function_reference() const noexcept -> bool {
    const auto* expression = std::get_if<SemanticExpression>(&*storage);
    return expression != nullptr && std::holds_alternative<SemCallable>(expression->value);
}

auto BuiltExpression::expression() const noexcept -> const SemanticExpression& {
    if (const auto* value = std::get_if<SemanticExpression>(&*storage)) {
        return *value;
    }
    return std::get<PlaceExpression>(*storage).expression;
}

auto BuiltExpression::type() const noexcept -> const ConstructionTypeRef& {
    return expression().type.construction();
}

auto BuiltExpression::constant() const noexcept -> std::optional<ConstantID> {
    return expression().constant;
}

BodyFullExpressionSuspension::BodyFullExpressionSuspension(
    std::optional<LifetimeRegionID>& active
) noexcept
    : slot(std::addressof(active)),
      saved(std::exchange(active, std::nullopt)) {}

BodyFullExpressionSuspension::~BodyFullExpressionSuspension() noexcept {
    *slot = saved;
}

BodyReferencePathGuard::BodyReferencePathGuard(bool& active, bool path_is_reachable) noexcept
    : slot(std::addressof(active)),
      saved(std::exchange(active, active && path_is_reachable)) {}

BodyReferencePathGuard::~BodyReferencePathGuard() noexcept {
    *slot = saved;
}

BodyBatchElaborator::BodyBatchElaborator(
    ProgramDraft& builder,
    AnalysisCatalogView catalog_view,
    ImportUsage& usage,
    ConstructionRequests& requests
) noexcept
    : draft(std::addressof(builder)),
      catalog_data(catalog_view),
      imports(std::addressof(usage)),
      requests(requests),
      functions(catalog_view.function_count()),
      states(catalog_view.function_count(), Unvisited {}),
      body_ids(catalog_view.function_count()) {
    for (const auto& symbol : catalog_view.symbols()) {
        if (const auto* function = std::get_if<CatalogFunctionForm>(&symbol.form)) {
            functions[function->function.index()] = std::addressof(symbol);
            if (draft->imported(symbol.module_id)) {
                states[function->function.index()] = Complete {};
                body_ids[function->function.index()] = draft->body_for_callable(function->callable);
            }
        }
    }
}

auto BodyExprSite::evaluate_constant_expression(ASTExprID expression) noexcept
    -> AnalysisTask<std::optional<ConstantID>> {
    return body.batch->evaluate_constant_expression(
        module_id(),
        expression,
        std::nullopt,
        body.generic_context ? &*body.generic_context : nullptr,
        &body
    );
}

auto BodyBatchElaborator::evaluate_constant_expression(
    ProgramModuleID module_id,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected,
    const GenericTypeContext* generic_context,
    BodyElaborator* enclosing
) noexcept -> AnalysisTask<std::optional<ConstantID>> {
    const auto* module = catalog_data.find_module(module_id);
    auto elaborator = BodyElaborator(
        *this,
        module_id,
        module->declaration,
        draft->syntax_tree(module_id).view(),
        draft->reserve_body(BodyKind::ConstBlock),
        expected,
        draft->add_empty_failure_term(),
        false,
        false,
        generic_context ? std::optional(*generic_context) : std::nullopt
    );
    elaborator.static_body = true;
    if (enclosing) {
        elaborator.inherited_locals = enclosing->visible_locals();
        elaborator.lexical_class = enclosing->lexical_class;
    }
    const auto span = elaborator.ast.expression(expression).span;
    elaborator.begin_full_expression(span);
    auto built = co_await elaborator.build_static_expression(expression, expected);
    elaborator.end_full_expression();
    if (!built) {
        co_return std::unexpected(built.error());
    }
    auto& region = elaborator.regions.front();
    elaborator.append_statement(
        SemExpressionStatement {std::move(*built)},
        elaborator.origin(span)
    );
    auto ready = co_await requests.close_generic_calls(region);
    if (!ready) {
        co_return std::unexpected(ready.error());
    }
    if (elaborator.observe_sources) {
        draft->source_analysis()->add_body(std::move(elaborator.source_occurrences));
    }
    auto definition = elaborator.finish_body(std::move(region));
    const auto id = definition.id;
    static_expression_bodies.insert(id);
    draft->add_body_draft(std::move(definition));
    const auto& root = draft->body_draft(id);
    auto result = co_await requests.stage().evaluate(
        std::get<SemExpressionStatement>(root.region.statements.back().value).expression,
        StaticEvaluationPurpose::Execute,
        &root
    );
    if (!result) {
        co_return std::unexpected(result.error());
    }
    co_return freeze_constant_value(*draft, std::move(*result));
}
