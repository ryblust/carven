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
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.constant.proof;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

auto BodyElaborator::conditional_expression(
    const ASTIfForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    if (!source.else_branch.has_value()) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeIfMissingElse, "value-form if requires an else branch")
        );
    }
    return build_if(source, span, expected, true);
}

auto BodyElaborator::lambda_expression(
    const ASTLambdaExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    auto expected_view = std::optional<ConstructionCallableViewTypeValue>();
    if (expected.has_value()) {
        if (const auto* term = std::get_if<TypeTermID>(&*expected)) {
            const auto construction = draft().construction_type_copy(*term);
            if (const auto* view =
                    std::get_if<ConstructionCallableViewTypeValue>(&construction.value)) {
                expected_view = *view;
            }
        }
    }
    if (expected_view.has_value() && expected_view->parameters.size() != source.parameters.size()) {
        return std::unexpected(fail(
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
            auto resolved = resolve_type(*parameter.type);
            if (!resolved.has_value()) {
                return std::unexpected(resolved.error());
            }
            type = *resolved;
        } else if (expected_view.has_value()) {
            type = expected_view->parameters[index].type;
        } else {
            return std::unexpected(fail(
                parameter.span,
                DiagnosticCode::LambdaSignatureInference,
                "lambda parameter type requires an annotation or expected callable view"
            ));
        }
        if (expected_view.has_value()
            && (access != expected_view->parameters[index].access
                || !compatible(*type, expected_view->parameters[index].type))) {
            return std::unexpected(fail(
                parameter.span,
                DiagnosticCode::LambdaSignatureInference,
                "lambda parameter differs from its expected callable view"
            ));
        }
        parameters.push_back(
            ConstructionCallableParameter {
                .access = access,
                .type = *type,
            }
        );
    }

    auto lambda_result = std::optional<ConstructionTypeRef>();
    if (source.result_type.has_value()) {
        auto resolved = resolve_type(*source.result_type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        lambda_result = *resolved;
    } else if (expected_view.has_value()) {
        lambda_result = expected_view->result;
    }
    if (expected_view.has_value()
        && lambda_result.has_value()
        && !compatible(*lambda_result, expected_view->result)) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::LambdaSignatureInference,
            "lambda result differs from its expected callable view"
        ));
    }

    const auto lambda_origin = origin(span);
    const auto actual_failures = draft().add_empty_failure_term();
    auto signature_failures = actual_failures;
    auto failure_policy = FailureContractPolicy::Inferred;
    if (source.throw_clause.has_value()) {
        auto members = resolve_failure_types(
            draft(),
            catalog(),
            import_usage(),
            source_module_id,
            ast,
            *source.throw_clause,
            [&](ASTExprID extent) noexcept { return resolve_array_extent(extent); }
        );
        if (!members.has_value()) {
            return std::unexpected(members.error());
        }
        const auto allowed = draft().add_concrete_failure_term(std::move(*members));
        draft().require_failure_subset(
            actual_failures,
            allowed,
            lambda_origin,
            FailureSubsetRequirementKind::DeclaredCallable
        );
        signature_failures = allowed;
        failure_policy = FailureContractPolicy::Declared;
    }

    struct CaptureSource final {
        ASTLambdaCapture syntax;
        ConstructionTypeRef type;
        CaptureMode mode;
        expression_construction::Capture operand;
    };
    auto captures = std::vector<CaptureSource>();
    auto capture_names = std::flat_set<std::string, std::less<>>();
    captures.reserve(source.captures.size());
    for (const auto& capture : source.captures) {
        const auto name = spelling(capture.name_span);
        if (!capture_names.insert(name).second) {
            return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::LambdaCaptureDuplicate,
                std::format("lambda captures '{}' more than once", name)
            ));
        }
        const auto* local = use_local(name);
        if (local == nullptr || std::holds_alternative<ConstantID>(local->storage)) {
            return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::LambdaCaptureInvalid,
                std::format("'{}' is not a runtime local that can be captured", name)
            ));
        }
        if (type_contains_callable_view(draft(), local->type)) {
            return std::unexpected(fail(
                capture.name_span,
                DiagnosticCode::TypeCallableViewEscape,
                "callable views cannot be captured by a lambda"
            ));
        }
        const auto storage = std::visit(
            Overloaded {
                [](const BoundStorage& value) noexcept -> ExpressionStorage {
                    return value.root_place;
                },
                [](ExpressionHandle value) noexcept -> ExpressionStorage { return value; },
                [](PlaceHandle value) noexcept -> ExpressionStorage { return value; },
                [](ConstantID) noexcept -> ExpressionStorage {
                    invariant_violation("compile-time constant reached runtime capture");
                },
            },
            local->storage
        );
        auto built = BuiltExpression {BuiltExpression {
            .type = local->type,
            .storage = storage,
            .constant = std::nullopt,
            .pending_failures = {},
            .takeable = false,
        }};
        const auto mode =
            capture.write_marker.has_value() ? CaptureMode::Write : CaptureMode::Value;
        auto operand = std::optional<expression_construction::Capture>();
        if (mode == CaptureMode::Write) {
            auto place = as_place(built, capture.span);
            if (!place.has_value()) {
                return std::unexpected(place.error());
            }
            operand = expression_construction::WriteCapture {.place = *place};
        } else {
            auto value = as_value(built, capture.span, AccessMode::Read);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            operand = expression_construction::ValueCapture {.value = *value};
        }
        captures.push_back(
            CaptureSource {
                .syntax = capture,
                .type = local->type,
                .mode = mode,
                .operand = *operand,
            }
        );
    }

    auto reservation = draft().reserve_body(BodyKind::Closure);
    const auto body_id = reservation.id();
    auto child = BodyElaborator(
        *batch,
        source_module_id,
        semantic_module_id,
        ast,
        std::move(reservation),
        lambda_result,
        actual_failures,
        failure_policy != FailureContractPolicy::UndeclaredPublished,
        false
    );
    auto inherited_constants = std::vector<std::string>();
    auto inherited_names = std::flat_set<std::string, std::less<>>();
    for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
        for (const auto& [name, local] : frame->names) {
            if (!inherited_names.insert(name).second) {
                continue;
            }
            const auto* constant = std::get_if<ConstantID>(&local.storage);
            if (constant == nullptr) {
                continue;
            }
            child.frames.front().names.emplace(
                name,
                LocalStorage {
                    .storage = *constant,
                    .type = local.type,
                    .writable_owner = false,
                    .used = false,
                    .takeable = false,
                    .role = LocalRole::Local,
                    .unused_candidate = std::nullopt,
                }
            );
            inherited_constants.push_back(name);
        }
    }
    for (const auto& capture : captures) {
        auto added = child.add_capture(capture.syntax.name_span, capture.type, capture.mode);
        if (!added.has_value()) {
            return std::unexpected(added.error());
        }
    }
    for (auto index = 0uz; index < source.parameters.size(); ++index) {
        auto added = child.add_parameter(source.parameters[index], parameters[index]);
        if (!added.has_value()) {
            return std::unexpected(added.error());
        }
    }
    auto child_body = child.run(source.body);
    if (!child_body.has_value()) {
        return std::unexpected(child_body.error());
    }
    for (const auto& name : inherited_constants) {
        if (!child.local_was_used(name)) {
            continue;
        }
        const auto* local = use_local(name);
        if (local == nullptr || !std::holds_alternative<ConstantID>(local->storage)) {
            invariant_violation("inherited lambda constant no longer names its source");
        }
    }
    const auto resolved_result = child.inferred_result_type();
    if (expected_view.has_value() && !compatible(resolved_result, expected_view->result)) {
        return std::unexpected(fail(
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
    const auto callable = draft().append_body_callable(
        ConstructionCallableContract {
            .parameters = std::move(parameters),
            .result = resolved_result,
            .failures = signature_failures,
            .policy = failure_policy,
        }
    );
    draft().complete_callable(callable, ClosureBodyImplementation {.body = body_id});
    draft().add_body_draft(std::move(*child_body));

    auto operands = std::vector<expression_construction::Capture>();
    operands.reserve(captures.size());
    for (const auto& capture : captures) {
        operands.push_back(capture.operand);
    }
    const auto closure_type = draft().intern_type(
        CanonicalType {
            .value = ClosureTypeValue {.callable = callable},
        }
    );
    const auto value = active_builder().append_value(
        closure_type,
        active_builder().lifetime(),
        expression_construction::Closure {.callable = callable, .captures = std::move(operands)},
        lambda_origin
    );
    return BuiltExpression {
        .type = closure_type,
        .storage = value,
        .constant = std::nullopt,
        .pending_failures = {},
    };
}

auto BodyElaborator::expression(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
    -> AnalysisResult<BuiltExpression> {
    const auto was_reachable = reachable;
    [[maybe_unused]] const auto path = ReferencePathGuard(reference_path_reachable, was_reachable);
    const auto& source = ast.expression(id);
    auto result = std::visit(
        Overloaded {
            [&](const ASTLiteral& value) noexcept {
                return literal_expression(value, source.span, expected);
            },
            [&](const ASTCppNameExpr& value) noexcept {
                return global_cpp_expression(value, source.span);
            },
            [&](const ASTNameExpr& value) noexcept { return name_expression(value, source.span); },
            [&](const ASTGroupExpr& value) noexcept {
                return expression(value.expression, expected);
            },
            [&](const ASTArrayExpr& value) noexcept {
                return array_expression(value, source.span, expected);
            },
            [&](const ASTConstructionExpr& value) noexcept {
                return construction_expression(value, source.span);
            },
            [&](const ASTPrefixExpr& value) noexcept {
                return prefix_expression(value, source.span, expected);
            },
            [&](const ASTAccessExpr& value) noexcept {
                return access_expression(value, source.span);
            },
            [&](const ASTBinaryExpr& value) noexcept {
                return binary_expression(value, source.span, expected);
            },
            [&](const ASTCastExpr& value) noexcept { return cast_expression(value, source.span); },
            [&](const ASTCallExpr& value) noexcept {
                return call_expression(value, source.span, expected);
            },
            [&](const ASTIndexExpr& value) noexcept {
                return index_expression(value, source.span);
            },
            [&](const ASTMemberExpr& value) noexcept {
                return member_expression(value, source.span);
            },
            [&](const ASTPropagationExpr& value) noexcept {
                return propagation_expression(value, source.span);
            },
            [&](const ASTIfForm& value) noexcept {
                return conditional_expression(value, source.span, expected);
            },
            [&](const ASTContextualCaseExpr& value) noexcept -> AnalysisResult<BuiltExpression> {
                auto enumeration = expected_enum_type(expected, value.name_span);
                if (!enumeration.has_value()) {
                    return std::unexpected(enumeration.error());
                }
                return enum_case_expression(
                    *enumeration,
                    spelling(value.name_span),
                    std::span<const ASTCallArgument>(),
                    source.span,
                    value.name_span
                );
            },
            [&](const ASTLambdaExpr& value) noexcept -> AnalysisResult<BuiltExpression> {
                return lambda_expression(value, source.span, expected);
            },
            [&](const ASTMatchForm& value) noexcept -> AnalysisResult<BuiltExpression> {
                return match_expression(value, source.span, expected);
            },
            [&](const ASTTryForm& value) noexcept -> AnalysisResult<BuiltExpression> {
                return try_expression(value, source.span, expected);
            },
        },
        source.value
    );
    if (result.has_value()
        && does_not_complete(*result)
        && expected.has_value()
        && is_void_type(draft(), result->type)) {
        auto node = take_built(*result, source.span);
        node.type = *expected;
        result->storage = body_builder.add_expression(std::move(node));
        result->type = *expected;
    }
    if (result.has_value()) {
        reachable = was_reachable && result->completes;
    }
    return result;
}


} // namespace body_elaboration
