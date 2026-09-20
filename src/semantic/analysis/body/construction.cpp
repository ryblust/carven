module carven:semantic.analysis.body.construction.impl;

import :diagnostics.builder;
import :diagnostics.code;
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
import :semantic.analysis.constant.root;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
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

auto BodyElaborator::resolve_constant_name(std::string_view name, Span span) noexcept
    -> AnalysisTask<std::optional<ConstantID>> {
    if (const auto* local = use_local(name)) {
        const auto* constant = std::get_if<ConstantID>(&local->storage);
        co_return constant == nullptr ? std::nullopt : std::optional(*constant);
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

auto BodyElaborator::resolve_nominal_qualifier(ASTExprID expression) noexcept
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
    const auto enumeration = draft().enum_declaration_copy(nominal->enumeration);
    for (const auto case_id : enumeration.cases) {
        const auto declaration = draft().construction_enum_case_declaration_copy(case_id);
        if (draft().spelling_copy(declaration.name) != name) {
            continue;
        }
        co_return ResolvedEnumCase {
            .id = case_id,
            .owner = declaration.owner,
            .payload_types = declaration.payload_types,
            .constant = declaration.constant,
        };
    }
    co_return std::unexpected(fail(
        span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format("enum has no case named '{}'", name)
    ));
}

auto BodyElaborator::resolve_type(ASTTypeID type) noexcept -> AnalysisTask<ConstructionTypeRef> {
    auto result = (co_await resolve_source_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        type,
        [&](ASTExprID extent) noexcept { return resolve_array_extent(extent); }
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
    auto result = (co_await resolve_source_construction_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        type,
        [&](ASTExprID extent) noexcept { return resolve_array_extent(extent); }
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
    const auto* expression = std::get_if<SemanticExpression>(&storage);
    return expression != nullptr && std::holds_alternative<SemCallable>(expression->value);
}

auto BuiltExpression::expression() const noexcept -> const SemanticExpression& {
    if (const auto* value = std::get_if<SemanticExpression>(&storage)) {
        return *value;
    }
    return std::get<PlaceExpression>(storage).expression;
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
