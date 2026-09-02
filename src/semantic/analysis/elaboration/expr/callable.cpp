module carven:semantic.analysis.elaboration.expr.callable.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.operations;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTCallExpr& call,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& value = ast.expression(id);
    const auto* member = std::get_if<ASTMemberExpr>(&ast.expression(call.callee).value);
    const auto callee_proof = builder.begin_expression_proof();
    auto callee = std::optional<HIRExprID>();
    if (member != nullptr && member->op == ASTMemberOperator::Dot) {
        const auto operand = build_expression(module_analysis, scopes, control, member->operand_id);
        const auto* builtin_type = std::get_if<HIRBuiltinTypeValue>(
            &builder.type(expression_type(module_analysis, operand)).value
        );
        if (builtin_type != nullptr && builtin_type->kind == HIRBuiltinType::Str) {
            const auto name = module_analysis.spelling(member->name_span);
            const auto recognized = name == "len" || name == "is_empty";
            if (!recognized) {
                module_analysis.emit(
                    member->name_span,
                    name == "bytes" || name == "chars"
                        ? "str bytes/chars are properties, not functions"
                        : "str has no such method",
                    DiagnosticCode::TypeStrMethod
                );
            }
            if (!call.arguments.empty()) {
                module_analysis.emit(
                    value.span,
                    "str text methods take no arguments",
                    DiagnosticCode::TypeStrMethodArity
                );
            }
            const auto intrinsic =
                name == "is_empty" ? HIRTextIntrinsic::IsEmpty : HIRTextIntrinsic::Len;
            const auto check =
                check_text_intrinsic(builder, intrinsic, expression_type(module_analysis, operand));
            const auto result_type = builtin(module_analysis, member->name_span, check.result);
            auto constant = std::optional<HIRConstant>();
            if (recognized) {
                auto evaluation =
                    evaluate_text_intrinsic_constant(builder, intrinsic, operand, result_type);
                if (evaluation.has_value()) {
                    constant = std::move(evaluation->value);
                }
            }
            return append_expression(
                module_analysis,
                {
                    .origin = expression_origin,
                    .type =
                        recognized ? result_type : error_type(module_analysis, member->name_span),
                    .constant = std::move(constant),
                    .value = HIRTextIntrinsicExpr {
                        .operand_id = operand,
                        .intrinsic = intrinsic,
                    },
                }
            );
        }
        callee = member_expression(
            module_analysis,
            scopes,
            control,
            *member,
            call.callee,
            module_analysis.origin(ast.expression(call.callee).span),
            operand
        );
    } else {
        callee = build_expression(module_analysis, scopes, control, call.callee);
    }
    const auto callee_id = *callee;
    const auto callee_result_type = expression_type(module_analysis, callee_id);
    const auto callee_type = builder.type(callee_result_type).value;
    auto enum_case = std::optional<EnumCaseID>();
    if (const auto* member = std::get_if<HIRMemberExpr>(&builder.expression(callee_id).value)) {
        if (const auto* target = std::get_if<HIREnumCaseTarget>(&member->target)) {
            enum_case = target->enum_case;
            builder.finish_expression_proof(callee_proof);
        }
    }
    const auto* function = std::get_if<HIRFunctionTypeValue>(&callee_type);
    const auto* function_ref = std::get_if<HIRFunctionRefTypeValue>(&callee_type);
    const auto* closure = std::get_if<HIRClosureTypeValue>(&callee_type);
    auto parameter_types = std::vector<HIRFunctionParameterType>();
    auto callable_result = std::optional<HIRTypeID>();
    if (function != nullptr || closure != nullptr) {
        const auto callable = function != nullptr ? function->callable : closure->callable;
        const auto& contract = builder.callable(callable);
        parameter_types = contract.parameters;
        callable_result = contract.result;
    } else if (function_ref != nullptr) {
        const auto& contract = builder.callable_signature(function_ref->signature);
        parameter_types = contract.parameters;
        callable_result = contract.result;
    }
    auto arguments = std::vector<HIRCallArgument>();
    for (auto index = 0uz; index < call.arguments.size(); ++index) {
        const auto& argument = call.arguments[index];
        auto access_expression_id = argument.expression;
        while (const auto* group =
                   std::get_if<ASTGroupExpr>(&ast.expression(access_expression_id).value)) {
            access_expression_id = group->expression;
        }
        const auto* access_expression =
            std::get_if<ASTAccessExpr>(&ast.expression(access_expression_id).value);
        const auto access = access_expression == nullptr
            ? HIRAccessMode::Read
            : access_mode(
                  ASTAccessSyntax {
                      .mode = access_expression->mode,
                      .marker = access_expression->marker_span,
                  }
              );
        const auto analyzed_expression =
            access == HIRAccessMode::Write ? access_expression->operand_id : argument.expression;
        arguments.push_back({
            .access = access,
            .expression = index < parameter_types.size()
                ? expression_expected_diagnosing(
                      module_analysis,
                      scopes,
                      control,
                      analyzed_expression,
                      parameter_types[index].type,
                      DiagnosticCode::TypeCallArgument,
                      "call argument has an incompatible type",
                      ExpectedExpressionUsage::CallArgument
                  )
                : build_expression(module_analysis, scopes, control, analyzed_expression),
        });
    }
    auto result_type = std::optional<HIRTypeID>();
    if (callable_result.has_value()) {
        result_type = *callable_result;
        if (call.arguments.size() != arguments.size()) {
            invariant_violation("source and elaborated call arguments do not align");
        }
        if (arguments.size() != parameter_types.size()) {
            module_analysis.emit(
                value.span,
                "call argument count does not match the function parameters",
                DiagnosticCode::TypeCallArity
            );
        }
        for (const auto& [source_argument, argument, parameter] :
             std::views::zip(call.arguments, arguments, parameter_types)) {
            if (!compatible(
                    module_analysis,
                    parameter.type,
                    expression_type(module_analysis, argument.expression)
                )) {
                module_analysis.emit(
                    ast.expression(source_argument.expression).span,
                    "call argument has an incompatible type",
                    DiagnosticCode::TypeCallArgument
                );
            }
            if (parameter.access != argument.access) {
                module_analysis.emit(
                    ast.expression(source_argument.expression).span,
                    "call argument access does not match the parameter access",
                    DiagnosticCode::AccessCallMismatch
                );
            }
            if (argument.access == HIRAccessMode::Write) {
                diagnose_mutation_target(
                    module_analysis,
                    argument.expression,
                    ast.expression(source_argument.expression).span,
                    "Write access requires a write-eligible argument",
                    DiagnosticCode::AccessWriteArgument
                );
            }
        }
    } else if (!std::holds_alternative<HIRErrorTypeValue>(callee_type)) {
        module_analysis.emit(
            value.span,
            "called expression is not a function",
            DiagnosticCode::TypeNotCallable
        );
    }
    if (enum_case.has_value()) {
        auto payload = std::vector<HIRExprID>();
        payload.reserve(arguments.size());
        auto payload_constants = std::vector<HIRConstantID>();
        auto all_constant = true;
        for (const auto& argument : arguments) {
            payload.push_back(argument.expression);
            if (builder.expression(argument.expression).constant.has_value()) {
                payload_constants.push_back(*builder.expression(argument.expression).constant);
            } else {
                all_constant = false;
            }
        }
        return append_expression(
            module_analysis,
            {
                .origin = expression_origin,
                .type = result_type.has_value() ? *result_type
                                                : error_type(module_analysis, value.span),
                .constant = all_constant ? std::optional<HIRConstant> {HIRPayloadEnumConstant {
                                               .enum_case = *enum_case,
                                               .payload = std::move(payload_constants),
                                           }}
                                         : std::nullopt,
                .value = HIRCaseConstructionExpr {
                    .enum_case = *enum_case,
                    .payload = std::move(payload),
                },
            }
        );
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type =
                result_type.has_value() ? *result_type : error_type(module_analysis, value.span),
            .constant = std::nullopt,
            .value = HIRCallExpr {
                .callee = callee_id,
                .arguments = std::move(arguments),
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTLambdaExpr& lambda,
    ASTExprID id,
    ProgramOriginID expression_origin,
    std::optional<HIRTypeID> contextual_type
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& source = ast.expression(id);
    auto expected = std::optional<HIRCallableSignature>();
    if (contextual_type.has_value()) {
        const auto contextual_value = builder.type(*contextual_type).value;
        if (const auto* function = std::get_if<HIRFunctionRefTypeValue>(&contextual_value)) {
            expected = builder.callable_signature(function->signature);
        }
    }
    auto captures = std::vector<HIRLambdaCapture>();
    auto capture_sources = std::flat_set<SymbolID>();
    // Captured locals belong to the closure scope and must remain visible while
    // its body is analyzed. The scope also keeps lookup limited to enclosing
    // bindings when capture clauses are resolved below.
    const auto closure_scope = scopes.enter_scope();
    const auto closure_scope_id = scopes.current_scope();
    for (const auto& capture : lambda.captures) {
        const auto name = module_analysis.spelling(capture.name_span);
        if (name == "_") {
            module_analysis.emit(
                capture.name_span,
                "'_' cannot be captured",
                DiagnosticCode::LambdaCaptureInvalid
            );
            continue;
        }
        const auto source_symbol = scopes.find_enclosing(name);
        if (!source_symbol.has_value()) {
            module_analysis.emit(
                capture.name_span,
                "capture does not name an enclosing runtime binding",
                DiagnosticCode::LambdaCaptureInvalid
            );
            continue;
        }
        if (builder.symbol_constant(*source_symbol).has_value()) {
            module_analysis.emit(
                capture.name_span,
                "compile-time constants do not form runtime captures",
                DiagnosticCode::LambdaCaptureInvalid
            );
            continue;
        }
        const auto source_role = builder.symbol_role(*source_symbol);
        if (source_role != SemanticSymbolRole::Local
            && source_role != SemanticSymbolRole::Parameter
            && source_role != SemanticSymbolRole::LoopBinding) {
            module_analysis.emit(
                capture.name_span,
                "capture is not a runtime local binding",
                DiagnosticCode::LambdaCaptureInvalid
            );
            continue;
        }
        if (!capture_sources.insert(*source_symbol).second) {
            module_analysis.emit(
                capture.name_span,
                "lambda capture is listed more than once",
                DiagnosticCode::LambdaCaptureDuplicate
            );
            continue;
        }
        const auto write = capture.write_marker.has_value();
        if (write && !builder.symbol_write_eligible(*source_symbol)) {
            module_analysis.emit(
                capture.name_span,
                "Write capture requires a write-eligible binding",
                DiagnosticCode::LambdaCaptureInvalid
            );
        }
        const auto local = module_analysis.append_symbol({
            .name = builder.intern_string(name),
            .module_id = std::nullopt,
            .role = SemanticSymbolRole::Local,
            .parent = std::nullopt,
        });
        set_symbol_type(module_analysis, local, symbol_type(module_analysis, *source_symbol));
        builder.define_symbol_binding(
            local,
            write ? SemanticBindingRole::WriteAlias : SemanticBindingRole::ClosureState,
            write
        );
        scopes.bind(name, local);
        builder.mark_explicit_capture(local);
        builder.record_symbol_lint_candidate(local, module_analysis.origin(capture.name_span));
        captures.push_back({
            .mode = write ? HIRCaptureMode::Write : HIRCaptureMode::Value,
            .source = *source_symbol,
            .local = local,
            .type = symbol_type(module_analysis, *source_symbol),
            .origin = module_analysis.origin(capture.span),
        });
    }
    auto lambda_context = scopes.enter_lambda_boundary();
    auto parameters = std::vector<HIRParameter>();
    auto parameter_types = std::vector<HIRFunctionParameterType>();
    for (auto index = 0uz; index < lambda.parameters.size(); ++index) {
        const auto& parameter = lambda.parameters[index];
        auto parameter_type = parameter.type.has_value()
            ? build_type(module_analysis, scopes, control, *parameter.type)
            : (expected.has_value() && index < expected->parameters.size()
                   ? expected->parameters[index].type
                   : error_type(module_analysis, parameter.span));
        if (!parameter.type.has_value()
            && (!expected.has_value() || index >= expected->parameters.size())) {
            module_analysis.emit(
                parameter.span,
                "lambda parameter type cannot be inferred",
                DiagnosticCode::LambdaSignatureInference
            );
        }
        parameter_type = require_value_type(
            module_analysis,
            parameter_type,
            parameter.span,
            ValueTypeRole::FunctionParameter
        );
        parameter_types.push_back({
            .access = access_mode(parameter.access),
            .type = parameter_type,
        });
        auto target = HIRBindingTarget {HIRDiscardBindingTarget {}};
        if (const auto* named = std::get_if<ASTNamedBindingTarget>(&parameter.target)) {
            const auto name = module_analysis.spelling(named->name_span);
            const auto symbol = module_analysis.append_symbol({
                .name = builder.intern_string(name),
                .module_id = std::nullopt,
                .role = SemanticSymbolRole::Parameter,
                .parent = std::nullopt,
            });
            if (!scopes.bind(name, symbol)) {
                module_analysis.emit(
                    named->name_span,
                    "duplicate lambda parameter name",
                    DiagnosticCode::NameDuplicateParameter
                );
            }
            set_symbol_type(module_analysis, symbol, parameter_type);
            const auto access = access_mode(parameter.access);
            builder.define_symbol_binding(
                symbol,
                access == HIRAccessMode::Read
                    ? SemanticBindingRole::ReadAlias
                    : (access == HIRAccessMode::Write ? SemanticBindingRole::WriteAlias
                                                      : SemanticBindingRole::Owner),
                access == HIRAccessMode::Write
            );
            builder.record_symbol_lint_candidate(symbol, module_analysis.origin(named->name_span));
            target = HIRNamedBindingTarget {
                .symbol = symbol,
            };
        }
        parameters.push_back({
            .access = access_mode(parameter.access),
            .target = std::move(target),
            .type = parameter_type,
            .origin = module_analysis.origin(parameter.span),
        });
    }
    auto result = lambda.result_type.has_value()
        ? build_type(module_analysis, scopes, control, *lambda.result_type)
        : (expected.has_value() ? expected->result
                                : builtin(module_analysis, source.span, HIRBuiltinType::Void));
    const auto infer_result = !lambda.result_type.has_value() && !expected.has_value();
    auto return_inference = ReturnTypeInference();
    const auto body = build_block(
        module_analysis,
        scopes,
        lambda.body,
        infer_result ? inferred_body_control(return_inference) : root_body_control(result)
    );
    lambda_context.close();
    if (infer_result) {
        result = infer_return_type(module_analysis, return_inference, source.span);
    }
    const auto contract_failures = lambda.throw_clause.has_value()
        ? normalized_failures(module_analysis, scopes, control, *lambda.throw_clause)
        : std::vector<HIRTypeID>();
    const auto callable = builder.append_body_callable(
        parameter_types,
        result,
        contract_failures,
        lambda.throw_clause.has_value() ? SemanticFailureContractKind::Declared
                                        : SemanticFailureContractKind::Inferred
    );
    builder.define_callable_body(callable, std::move(parameters), closure_scope_id, body);
    const auto closure_type = builder.intern_closure_type(callable, !captures.empty());
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = closure_type,
            .constant = std::nullopt,
            .value = HIRClosureExpr {
                .captures = std::move(captures),
                .result = result,
                .callable = callable,
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTPropagationExpr& propagation,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto operand = build_expression(module_analysis, scopes, control, propagation.operand_id);
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = expression_type(module_analysis, operand),
            .constant = std::nullopt,
            .value = HIRPropagationExpr {
                .operand_id = operand,
            },
        }
    );
}
