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
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.constant.root;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.analysis.types.display;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.evaluation.output;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
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
    bool test_body
) noexcept
    : batch(std::addressof(owner)),
      source_module_id(source_module_id),
      semantic_module_id(semantic_module_id),
      ast(source),
      body_builder(std::move(reservation), *owner.draft),
      result_type(result),
      infer_result(!result.has_value()),
      outward_failure_term_id(outward_failure_term_id),
      is_test(test_body),
      dead_failure_context {owner.draft->add_empty_failure_term(), true},
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
}

auto BodyElaborator::observe_source(
    Span location,
    std::optional<SourceSpan> definition,
    std::optional<ConstructionTypeRef> type
) noexcept -> void {
    if (!observe_sources || location.empty()) {
        return;
    }
    auto builtin = std::optional<BuiltinType>();
    if (type) {
        if (const auto* concrete = std::get_if<TypeID>(&*type)) {
            const auto canonical = draft().type_copy(*concrete);
            if (const auto* value = std::get_if<BuiltinTypeValue>(&canonical.value)) {
                builtin = value->kind;
            }
        }
    }
    source_occurrences.push_back(
        {.location = locate(ast.source_id(), location),
         .definition = definition,
         .type = type,
         .builtin_type = builtin}
    );
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
    co_return (co_await evaluate_array_extent(draft(), source_module_id, ast, scope, id));
}

auto BodyElaborator::static_expression(
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<BuiltExpression> {
    if (static_stage()) {
        co_return co_await this->expression(expression, expected);
    }
    auto scope = BodyExprSite(*this);
    auto result = co_await build_static_expression(
        draft(),
        source_module_id,
        ast,
        scope,
        expression,
        expected
    );
    if (!result) {
        if (const auto* diagnostic = std::get_if<AnalysisFailure>(&result.error())) {
            co_return std::unexpected(*diagnostic);
        }
        co_return std::unexpected(fail(
            ast.expression(expression).span,
            DiagnosticCode::ConstAdmission,
            "static input requires admitted static operands"
        ));
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
        co_return active_builder().make_expression(
            local.type,
            active_builder().lifetime(),
            origin(span),
            SemConstant {.constant = **constant}
        );
    }
    auto value = active_builder().binding_expression(binding).expression;
    value.category = SemanticValueCategory::Value;
    value.lifetime = active_builder().lifetime();
    value.origin = origin(span);
    co_return value;
}

auto BodyElaborator::resolve_static_references(SemanticExpression& source) noexcept
    -> AnalysisTask<bool> {
    auto bindings = std::vector<SemanticExpression*>();
    visit_semantic_nodes(source, [&](SemanticExpression& expression) noexcept {
        if (std::holds_alternative<SemBinding>(expression.value)) {
            bindings.push_back(&expression);
        }
    });
    auto complete = true;
    for (auto* expression : bindings) {
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
        expression->value = SemConstant {.constant = **value};
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
    auto complete = co_await resolve_static_references(initializer);
    if (!complete) {
        co_return std::unexpected(complete.error());
    }
    if (!*complete) {
        co_return std::nullopt;
    }
    const auto published_type = constant_initializer_type(draft(), initializer.type.construction());
    auto result = co_await construction_requests().stage().evaluate(
        initializer,
        ExecutionOutputMode::Discard
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

auto BodyElaborator::resolve_constant_name(std::string_view name, Span span) noexcept
    -> AnalysisTask<std::optional<ConstantID>> {
    if (const auto* local = use_local(name)) {
        observe_binding(span, local->storage.binding, local->type);
        co_return co_await compute_static_binding(local->storage.binding);
    }
    auto selected = (co_await find_global(name, span));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    co_return (co_await (*selected)->form.visit(
        Overloaded {
            [&](const CatalogConstantForm& form) noexcept
                -> AnalysisTask<std::optional<ConstantID>> {
                const auto declaration = draft().module_constant_declaration_copy(form.constant);
                co_return declaration.value;
            },
            [&](const CatalogEnumCaseForm& form) noexcept
                -> AnalysisTask<std::optional<ConstantID>> {
                const auto declaration =
                    draft().construction_enum_case_declaration_copy(form.enum_case);
                co_return declaration.constant;
            },
            [&](const CatalogFunctionForm& form) noexcept
                -> AnalysisTask<std::optional<ConstantID>> {
                auto completed =
                    (co_await batch
                         ->ensure_function_signature(form.function, source_module_id, span));
                if (!completed.has_value()) {
                    co_return std::unexpected(completed.error());
                }
                co_return std::nullopt;
            },
            [&]<typename Form>(const Form&) noexcept -> AnalysisTask<std::optional<ConstantID>> {
                static_assert(
                    std::same_as<Form, CatalogStructForm> || std::same_as<Form, CatalogEnumForm>,
                    "unhandled non-constant catalog symbol"
                );
                co_return std::unexpected(fail(
                    span,
                    DiagnosticCode::TypeValueRequired,
                    std::format("'{}' does not name a constant value", name)
                ));
            },
        }
    ));
}

auto BodyElaborator::construction_requests() noexcept -> ConstructionRequests& {
    return batch->requests;
}

auto BodyElaborator::resolve_function(std::string_view name, Span span) noexcept
    -> AnalysisTask<std::optional<FunctionID>> {
    if (use_local(name) != nullptr || catalog().lookup(source_module_id, name).empty()) {
        co_return std::optional<FunctionID>();
    }
    auto selected = (co_await find_global(name, span));
    if (!selected) {
        co_return std::unexpected(selected.error());
    }
    const auto* function = std::get_if<CatalogFunctionForm>(&(*selected)->form);
    if (function == nullptr) {
        co_return std::optional<FunctionID>();
    }

    auto completed =
        (co_await batch->ensure_function_signature(function->function, source_module_id, span));
    if (!completed) {
        co_return std::unexpected(completed.error());
    }
    co_return std::optional(function->function);
}

auto BodyElaborator::resolve_type_qualifier(ASTExprID expression) noexcept
    -> AnalysisTask<std::optional<TypeID>> {
    auto current_id = expression;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(current_id).value)) {
        current_id = group->expression;
    }
    const auto* name = std::get_if<ASTNameExpr>(&ast.expression(current_id).value);
    if (name == nullptr) {
        co_return std::optional<TypeID>();
    }
    const auto text = spelling(name->name_span);
    observe_source(name->name_span, std::nullopt, std::nullopt);
    if (const auto builtin = source_builtin_type(text)) {
        co_return std::optional(draft().builtin_type(*builtin));
    }
    if (find_local(text) != nullptr) {
        co_return std::optional<TypeID>();
    }
    auto selected = (co_await find_global(text, name->name_span));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    if (const auto* record = std::get_if<CatalogStructForm>(&(*selected)->form)) {
        co_return std::optional(
            draft().intern_type({.value = StructTypeValue {.structure = record->structure}})
        );
    }
    const auto* enumeration = std::get_if<CatalogEnumForm>(&(*selected)->form);
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
            type_display_name(draft(), type),
            name,
            spelling_suggestion(name, catalog().enum_case_names(nominal->enumeration))
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
        resolve_extent
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
        resolve_extent
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
        }
    }
}
