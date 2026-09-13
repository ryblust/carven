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
import :semantic.analysis.body.expression_site;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.constant;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.analysis.validation;
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

auto BodyElaborator::resolve_array_extent(ASTExprID id) noexcept -> AnalysisResult<std::uint64_t> {
    auto scope = BodyExpressionSite(*this);
    return evaluate_array_extent(draft(), source_module_id, ast, scope, id);
}

auto BodyElaborator::resolve_constant_name(std::string_view name, Span span) noexcept
    -> AnalysisResult<ResolvedConstantName> {
    if (const auto* local = use_local(name)) {
        const auto* constant = std::get_if<ConstantID>(&local->storage);
        return ResolvedConstantName {
            .type = local->type,
            .constant = constant == nullptr ? std::nullopt : std::optional(*constant),
        };
    }
    auto selected = find_global(name, span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    return std::visit(
        Overloaded {
            [&](const CatalogConstantForm& form) noexcept -> AnalysisResult<ResolvedConstantName> {
                const auto declaration = draft().module_constant_declaration_copy(form.constant);
                return ResolvedConstantName {
                    .type = draft().constant_copy(declaration.value).type,
                    .constant = declaration.value,
                };
            },
            [&](const CatalogEnumCaseForm& form) noexcept -> AnalysisResult<ResolvedConstantName> {
                const auto declaration =
                    draft().construction_enum_case_declaration_copy(form.enum_case);
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = declaration.owner},
                    }
                );
                return ResolvedConstantName {
                    .type = type,
                    .constant = declaration.constant,
                };
            },
            [&](const CatalogFunctionForm& form) noexcept -> AnalysisResult<ResolvedConstantName> {
                auto completed =
                    batch->ensure_function_signature(form.function, source_module_id, span);
                if (!completed.has_value()) {
                    return std::unexpected(completed.error());
                }
                return ResolvedConstantName {
                    .type = draft().intern_type(
                        CanonicalType {
                            .value = FunctionTypeValue {.callable = form.callable},
                        }
                    ),
                    .constant = std::nullopt,
                };
            },
            [&]<typename Form>(const Form&) noexcept -> AnalysisResult<ResolvedConstantName> {
                static_assert(
                    std::same_as<Form, CatalogStructForm> || std::same_as<Form, CatalogEnumForm>,
                    "unhandled non-constant catalog symbol"
                );
                return std::unexpected(fail(
                    span,
                    DiagnosticCode::TypeValueRequired,
                    std::format("'{}' does not name a constant value", name)
                ));
            },
        },
        (*selected)->form
    );
}

auto BodyElaborator::resolve_enum_qualifier(ASTExprID expression) noexcept
    -> AnalysisResult<std::optional<TypeID>> {
    auto current_id = expression;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(current_id).value)) {
        current_id = group->expression;
    }
    const auto* name = std::get_if<ASTNameExpr>(&ast.expression(current_id).value);
    if (name == nullptr) {
        return std::optional<TypeID>();
    }
    const auto text = spelling(name->name_span);
    if (find_local(text) != nullptr) {
        return std::optional<TypeID>();
    }
    auto selected = find_global(text, name->name_span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    const auto* enumeration = std::get_if<CatalogEnumForm>(&(*selected)->form);
    if (enumeration == nullptr) {
        return std::optional<TypeID>();
    }
    return std::optional(
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
) noexcept -> AnalysisResult<ResolvedEnumCase> {
    const auto canonical = draft().type_copy(type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    if (nominal == nullptr) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "enum case qualifier does not name an enum type"
        ));
    }
    const auto enumeration = draft().enum_declaration_copy(nominal->enumeration);
    for (const auto case_id : enumeration.cases) {
        const auto declaration = draft().construction_enum_case_declaration_copy(case_id);
        if (draft().spelling_copy(declaration.name) != name) {
            continue;
        }
        return ResolvedEnumCase {
            .id = case_id,
            .owner = declaration.owner,
            .payload_types = declaration.payload_types,
            .constant = declaration.constant,
        };
    }
    return std::unexpected(fail(
        span,
        DiagnosticCode::TypeMemberUnresolved,
        std::format("enum has no case named '{}'", name)
    ));
}

auto BodyElaborator::resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return resolve_source_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        type,
        [&](ASTExprID extent) noexcept { return resolve_array_extent(extent); }
    );
}

auto BodyElaborator::resolve_construction_type(const ASTConstructionType& type) noexcept
    -> AnalysisResult<ConstructionTypeRef> {
    return resolve_source_construction_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        type,
        [&](ASTExprID extent) noexcept { return resolve_array_extent(extent); }
    );
}

auto BodyElaborator::compatible(ConstructionTypeRef left, ConstructionTypeRef right) const noexcept
    -> bool {
    return type_shapes_compatible(draft(), left, right);
}

auto BodyElaborator::require_invariant_storage_type(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    if (is_cpp_type(source) || is_cpp_type(target)) {
        return {};
    }

    struct ArrayShape final {
        ConstructionTypeRef element;
        std::uint64_t extent;
    };

    struct CallableViewShape final {
        std::vector<ConstructionCallableParameter> parameters;
        ConstructionTypeRef result;
        FailureTermID failures;
    };

    const auto array_shape = [&](ConstructionTypeRef type) noexcept -> std::optional<ArrayShape> {
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            const auto canonical = draft().type_copy(*concrete);
            const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
            return array == nullptr ? std::nullopt
                                    : std::optional(
                                          ArrayShape {
                                              .element = array->element,
                                              .extent = array->extent,
                                          }
                                      );
        }
        const auto construction = draft().construction_type_copy(std::get<TypeTermID>(type));
        const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value);
        return array == nullptr ? std::nullopt
                                : std::optional(
                                      ArrayShape {
                                          .element = array->element,
                                          .extent = array->extent,
                                      }
                                  );
    };
    const auto callable_view_shape =
        [&](ConstructionTypeRef type) noexcept -> std::optional<CallableViewShape> {
        if (std::holds_alternative<TypeID>(type)) {
            return std::nullopt;
        }
        const auto construction = draft().construction_type_copy(std::get<TypeTermID>(type));
        const auto* view = std::get_if<ConstructionCallableViewTypeValue>(&construction.value);
        return view == nullptr ? std::nullopt
                               : std::optional(
                                     CallableViewShape {
                                         .parameters = view->parameters,
                                         .result = view->result,
                                         .failures = view->failures,
                                     }
                                 );
    };
    const auto invariant = [&](this auto&& self,
                               ConstructionTypeRef left,
                               ConstructionTypeRef right) noexcept -> bool {
        if (left == right) {
            return true;
        }
        const auto left_slice = slice_element(draft(), left);
        const auto right_slice = slice_element(draft(), right);
        if (left_slice || right_slice) {
            return left_slice && right_slice && self(*left_slice, *right_slice);
        }
        const auto left_array = array_shape(left);
        const auto right_array = array_shape(right);
        if (left_array.has_value() || right_array.has_value()) {
            return left_array.has_value()
                && right_array.has_value()
                && left_array->extent == right_array->extent
                && self(left_array->element, right_array->element);
        }
        const auto left_view = callable_view_shape(left);
        const auto right_view = callable_view_shape(right);
        if (left_view.has_value() || right_view.has_value()) {
            if (!left_view.has_value()
                || !right_view.has_value()
                || left_view->parameters.size() != right_view->parameters.size()
                || !self(left_view->result, right_view->result)) {
                return false;
            }
            for (auto index = 0uz; index < left_view->parameters.size(); ++index) {
                if (left_view->parameters[index].access != right_view->parameters[index].access
                    || !self(
                        left_view->parameters[index].type,
                        right_view->parameters[index].type
                    )) {
                    return false;
                }
            }
            draft().require_equal_failures(left_view->failures, right_view->failures, origin(span));
            return true;
        }
        const auto* left_type = std::get_if<TypeID>(&left);
        const auto* right_type = std::get_if<TypeID>(&right);
        return left_type != nullptr
            && right_type != nullptr
            && draft().type_copy(*left_type) == draft().type_copy(*right_type);
    };
    if (!invariant(source, target)) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeMismatch, "storage has an incompatible type")
        );
    }
    return {};
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
    ImportUsage& usage
) noexcept
    : draft(std::addressof(builder)),
      catalog_data(catalog_view),
      imports(std::addressof(usage)),
      functions(catalog_view.function_count()),
      states(catalog_view.function_count(), Unvisited {}) {
    for (const auto& symbol : catalog_view.symbols()) {
        if (const auto* function = std::get_if<CatalogFunctionForm>(&symbol.form)) {
            functions[function->function.index()] = std::addressof(symbol);
        }
    }
}
