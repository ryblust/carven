module carven:semantic.analysis.body.expr.impl;

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
import :semantic.analysis.body.resolve;
import :semantic.analysis.body.site;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.expr.member;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.source.builder;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.generic;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

auto BodyElaborator::observe_expression(ASTExprID expression, ConstructionTypeRef type) noexcept
    -> void {
    if (observe_sources) {
        source_occurrences.expression(draft(), ast, expression, type);
    }
}

auto BodyElaborator::conditional_expression(
    const ASTIfForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    bool allow_pointer_narrowing
) noexcept -> AnalysisTask<BuiltExpression> {
    if (!source.else_branch.has_value()) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::TypeIfMissingElse, "value-form if requires an else branch")
        );
    }
    co_return (co_await build_if(source, span, expected, true, allow_pointer_narrowing));
}

auto BodyElaborator::lambda_expression(
    const ASTLambdaExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<BuiltExpression> {
    auto expected_view = expected ? draft().callable_shape(*expected) : std::nullopt;
    if (expected_view && expected_view->owning_type) {
        expected_view.reset();
    }
    if (expected_view.has_value() && expected_view->parameters.size() != source.parameters.size()) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeCallArity,
            "lambda parameter count differs from its expected callable view"
        ));
    }

    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(source.parameters.size());
    for (auto index = 0uz; index < source.parameters.size(); ++index) {
        const auto& parameter = source.parameters[index];
        const auto access = semantic_access_mode(parameter.access);
        auto type = std::optional<ConstructionTypeRef>();
        if (parameter.type.has_value()) {
            auto resolved = (co_await resolve_type(*parameter.type));
            if (!resolved.has_value()) {
                co_return std::unexpected(resolved.error());
            }
            type = *resolved;
        } else if (expected_view.has_value()) {
            type = expected_view->parameters[index].type;
        } else {
            co_return std::unexpected(fail(
                parameter.span,
                DiagnosticCode::LambdaSignatureInference,
                "lambda parameter type requires an annotation or expected callable view"
            ));
        }
        if (expected_view.has_value()
            && (access != expected_view->parameters[index].access
                || !compatible(*type, expected_view->parameters[index].type))) {
            co_return std::unexpected(fail(
                parameter.span,
                DiagnosticCode::LambdaSignatureInference,
                "lambda parameter differs from its expected callable view"
            ));
        }
        parameters.push_back(
            ConstructionCallableParameter {
                .stage = ParameterStage::Runtime,
                .access = access,
                .type = *type,
            }
        );
    }

    auto lambda_result = std::optional<ConstructionTypeRef>();
    if (source.result_type.has_value()) {
        auto resolved = (co_await resolve_type(*source.result_type));
        if (!resolved.has_value()) {
            co_return std::unexpected(resolved.error());
        }
        lambda_result = *resolved;
    } else if (expected_view.has_value()) {
        lambda_result = expected_view->result;
    }
    if (expected_view.has_value()
        && lambda_result.has_value()
        && !compatible(*lambda_result, expected_view->result)) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::LambdaSignatureInference,
            "lambda result differs from its expected callable view"
        ));
    }

    const auto lambda_origin = origin(span);

    struct CaptureSource final {
        ASTLambdaCapture syntax;
        ConstructionTypeRef type;
        CaptureMode mode;
        SemCapture operand;
        std::optional<SourceSpan> definition;
    };

    auto captures = std::vector<CaptureSource>();
    auto capture_names = std::flat_set<std::string, std::less<>>();
    captures.reserve(source.captures.size());
    for (const auto& capture : source.captures) {
        const auto name = spelling(capture.name_span);
        if (!capture_names.insert(name).second) {
            co_return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::LambdaCaptureDuplicate,
                std::format("lambda captures '{}' more than once", name)
            ));
        }
        const auto* local = use_local(name);
        if (local != nullptr && runtime_local_outside_static_scope(name)) {
            co_return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::ConstAdmission,
                std::format("static expression cannot use the runtime value '{}'", name)
            ));
        }
        if (local == nullptr || batch->static_roots.contains(local->storage.binding)) {
            co_return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::LambdaCaptureInvalid,
                std::format("'{}' is not a runtime local that can be captured", name)
            ));
        }
        if (local->storage.binding.owner() != active_builder().identity()) {
            co_return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::ConstAdmission,
                "const block cannot capture a value from an enclosing execution frame"
            ));
        }
        if (type_contains_callable_view(draft(), local->type)) {
            co_return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::TypeCallableViewEscape,
                "callable views cannot be captured by a lambda"
            ));
        }
        auto built = BuiltExpression {
            .storage = UniqueIndirect {BodyExpressionStorage {local_place(*local, capture.span)}},
            .pending_failures = {},
            .takeable = false,
            .completes = true,
        };
        observe_binding(capture.name_span, *local);
        const auto mode =
            capture.write_marker.has_value() ? CaptureMode::Write : CaptureMode::Value;
        auto operand = std::optional<SemCapture>();
        if (mode == CaptureMode::Write) {
            if (std::holds_alternative<AccessBindingStorage>(
                    active_builder().binding_copy(local->storage.binding).storage
                )) {
                co_return std::unexpected(fail(
                    capture.name_span,
                    DiagnosticCode::AccessCaptureConflict,
                    "payload aliases cannot be captured with Write access"
                ));
            }
            auto place = consume_place(built, capture.span);
            if (!place.has_value()) {
                co_return std::unexpected(place.error());
            }
            operand =
                SemCapture {.mode = CaptureMode::Write, .expression = std::move(place->expression)};
        } else {
            auto value = consume_value(built, capture.span, AccessMode::Read);
            if (!value.has_value()) {
                co_return std::unexpected(value.error());
            }
            operand = SemCapture {.mode = CaptureMode::Value, .expression = std::move(*value)};
        }
        captures.push_back(
            CaptureSource {
                .syntax = capture,
                .type = local->type,
                .mode = mode,
                .operand = std::move(*operand),
                .definition = local->definition,
            }
        );
    }

    const auto finish = [&](ConstructionCallableRef callable) noexcept -> BuiltExpression {
        if (observe_sources && std::holds_alternative<CallableID>(callable)) {
            draft().source_analysis()->closure(
                std::get<CallableID>(callable),
                locate(ast.source_id(), Span::from_bounds(span.start(), span.start() + 1))
            );
        }
        auto operands = std::vector<SemCapture>();
        operands.reserve(captures.size());
        for (auto& capture : captures) {
            operands.push_back(std::move(capture.operand));
        }
        const auto closure_type =
            callable.visit([&](const auto identity) noexcept -> ConstructionTypeRef {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(identity)>, CallableID>) {
                    return draft().intern_type({.value = ClosureTypeValue {.callable = identity}});
                } else {
                    return identity;
                }
            });
        if (observe_sources) {
            source_occurrences.record(
                draft(),
                locate(ast.source_id(), Span::from_bounds(span.start(), span.start() + 1)),
                std::nullopt,
                closure_type,
                std::nullopt,
                std::holds_alternative<CallableID>(callable)
                    ? std::optional(std::get<CallableID>(callable))
                    : std::nullopt
            );
        }
        auto value = active_builder().make_expression(
            closure_type,
            active_builder().lifetime(),
            lambda_origin,
            SemClosure {.callable = callable, .captures = std::move(operands)}
        );
        return BuiltExpression {
            .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},

            .pending_failures = {},
            .takeable = true,
            .completes = true,
        };
    };
    if (const auto cached = lambda_callables.find(span); cached != lambda_callables.end()) {
        const auto contract = cached->second.visit([&](const auto identity) noexcept {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(identity)>, CallableID>) {
                return draft().construction_callable_contract_copy(identity);
            } else {
                return *draft().callable_contract(identity);
            }
        });
        if (expected_view && !compatible(contract.result, expected_view->result)) {
            co_return std::unexpected(fail(
                span,
                DiagnosticCode::LambdaSignatureInference,
                "inferred lambda result differs from its expected callable view"
            ));
        }
        co_return finish(cached->second);
    }

    const auto actual_failures = draft().add_empty_failure_term();
    auto signature_failures = ConstructionFailureRef(actual_failures);
    auto failure_policy = FailureContractPolicy::Inferred;
    if (source.throw_clause.has_value()) {
        const auto resolve_extent = [&](ASTExprID extent) noexcept {
            return resolve_array_extent(extent);
        };
        auto members = (co_await resolve_failure_types(
            draft(),
            catalog(),
            import_usage(),
            source_module_id,
            ast,
            *source.throw_clause,
            resolve_extent,
            nullptr,
            generic_context ? &*generic_context : nullptr,
            observe_sources ? &source_occurrences : nullptr
        ));
        if (!members.has_value()) {
            co_return std::unexpected(members.error());
        }
        const auto allowed = *members;
        draft().require_failure_subset(
            actual_failures,
            allowed,
            lambda_origin,
            FailureSubsetRequirementKind::DeclaredCallable
        );
        signature_failures = allowed;
        failure_policy = FailureContractPolicy::Declared;
    }

    auto definition = std::optional<GenericDeclarationID>();
    if (generic_context) {
        definition = draft().append_generic_closure(
            generic_context->definition,
            lambda_origin,
            ConstructionCallableContract {
                .execution = CallableExecutionKind::Synchronous,
                .parameters = parameters,
                .result = lambda_result.value_or(draft().builtin_type(BuiltinType::Void)),
                .failures = signature_failures,
                .policy = failure_policy,
            }
        );
    }

    // An explicit/contextual contract is independent of body completion, just
    // as for a named function. Queries retain it without inventing a body or
    // inferring a result from damaged source.
    const auto* observation = draft().source_analysis();
    const auto retain_contract =
        observation && observation->is_source_query() && !static_stage() && lambda_result;
    auto callable = std::optional<CallableID>();
    if (retain_contract && !definition) {
        callable = draft().append_body_callable({
            .execution = CallableExecutionKind::Synchronous,
            .parameters = parameters,
            .result = *lambda_result,
            .failures = signature_failures,
            .policy = failure_policy,
        });
    }

    auto reservation = definition ? draft().reserve_generic_body(*definition)
                                  : draft().reserve_body(BodyKind::Closure);
    const auto body_id = reservation.id();
    auto child = BodyElaborator(
        *batch,
        source_module_id,
        semantic_module_id,
        ast,
        std::move(reservation),
        lambda_result,
        actual_failures,
        true,
        false,
        generic_context,
        observe_sources
    );
    child.lexical_class = lexical_class;
    for (const auto& [name, local] : visible_locals()) {
        if (batch->static_roots.contains(local.storage.binding)) {
            child.inherited_locals.emplace(name, local);
        }
    }
    for (const auto& capture : captures) {
        auto added = child.add_capture(
            capture.syntax.name_span,
            capture.type,
            capture.mode,
            capture.definition
        );
        if (!added.has_value()) {
            co_return std::unexpected(added.error());
        }
    }
    for (auto index = 0uz; index < source.parameters.size(); ++index) {
        auto added = child.add_parameter(source.parameters[index], parameters[index]);
        if (!added.has_value()) {
            co_return std::unexpected(added.error());
        }
    }
    auto child_body = (co_await child.run(source.body));
    if (!child_body.has_value()) {
        if (retain_contract) {
            if (!nested_body_failure) {
                nested_body_failure = child_body.error();
            }
            if (definition) {
                const auto closure = draft().intern_generic_type(
                    GenericClosureType {
                        .definition = *definition,
                        .arguments = draft().generic_parameter_environment(*definition),
                    }
                );
                lambda_callables.emplace(span, closure);
                co_return finish(closure);
            }
            lambda_callables.emplace(span, *callable);
            co_return finish(*callable);
        }
        co_return std::unexpected(child_body.error());
    }
    const auto resolved_result = child.inferred_result_type();
    if (expected_view.has_value() && !compatible(resolved_result, expected_view->result)) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::LambdaSignatureInference,
            "inferred lambda result differs from its expected callable view"
        ));
    }
    for (const auto& capture : captures) {
        const auto name = spelling(capture.syntax.name_span);
        if (!child.local_was_used(name)) {
            warn(
                capture.syntax.name_span,
                DiagnosticCode::LambdaCaptureUnused,
                std::format("lambda capture '{}' is unused", name)
            );
        }
    }
    source_occurrences.merge(std::move(child.source_occurrences));
    if (definition) {
        draft().complete_generic_callable_result(*definition, resolved_result);
        draft().set_generic_callable_body(*definition, std::move(*child_body));
        const auto closure = draft().intern_generic_type(
            GenericClosureType {
                .definition = *definition,
                .arguments = draft().generic_parameter_environment(*definition),
            }
        );
        lambda_callables.emplace(span, closure);
        co_return finish(closure);
    }
    if (!callable) {
        callable = draft().append_body_callable(
            ConstructionCallableContract {
                .execution = CallableExecutionKind::Synchronous,
                .parameters = std::move(parameters),
                .result = resolved_result,
                .failures = signature_failures,
                .policy = failure_policy,
            }
        );
    }
    draft().complete_callable(*callable, ClosureBodyImplementation {.body = body_id});
    draft().add_body_draft(std::move(*child_body));

    lambda_callables.emplace(span, *callable);
    co_return finish(*callable);
}

auto BodyExprSite::interpret_enum_case(
    ConstructionTypeRef type,
    std::string_view name,
    Span name_span,
    std::span<const ASTExprID> arguments,
    Span span,
    bool called
) noexcept -> AnalysisTask<Value> {
    return ::interpret_enum_case(*this, type, name, name_span, arguments, span, called);
}

auto BodyElaborator::select_expression(
    ASTExprID id,
    bool allow_pointer_narrowing,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<SelectedExpression> {
    const auto was_reachable = reachable;
    [[maybe_unused]] const auto path =
        BodyReferencePathGuard(reference_path_reachable, was_reachable);
    auto site = BodyExprSite(*this, allow_pointer_narrowing);
    co_return (co_await interpret_expression(site, id, expected));
}

auto BodyElaborator::expression(
    ASTExprID id,
    bool allow_pointer_narrowing,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<BuiltExpression> {
    const auto was_reachable = reachable;
    const auto entry_reachable = reachable && reference_path_reachable;
    const auto& source = ast.expression(id);
    auto selected = (co_await select_expression(id, allow_pointer_narrowing, expected));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    auto result = materialize_selection(std::move(*selected), expected);
    if (result.has_value()
        && does_not_complete(*result)
        && expected.has_value()
        && is_void_type(draft(), result->type())) {
        auto node = take_built(*result, source.span);
        node.type = BodyType(*expected);
        *result->storage = std::move(node);
    }
    if (result.has_value()) {
        auto& node = std::visit(
            Overloaded {
                [](SemanticExpression& value) static noexcept -> SemanticExpression& {
                    return value;
                },
                [](PlaceExpression& value) static noexcept -> SemanticExpression& {
                    return value.expression;
                },
            },
            *result->storage
        );
        const auto dispatches_before_children = std::holds_alternative<SemIf>(node.value)
            || std::holds_alternative<SemMatch>(node.value)
            || std::holds_alternative<SemTry>(node.value)
            || std::holds_alternative<SemShortCircuit>(node.value);
        node.operation_reachable = std::holds_alternative<SemReport>(node.value)
            ? entry_reachable && node.operation_reachable
            : dispatches_before_children ? entry_reachable
                                         : reachable && reference_path_reachable;
        reachable = was_reachable && result->completes;
        observe_expression(id, result->type());
    }
    co_return result;
}
