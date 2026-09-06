module carven:semantic.analysis.body.call.impl;

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

auto BodyElaborator::callable_contract(BuiltExpression& callee, Span span) noexcept
    -> AnalysisResult<ConstructionCallableContract> {
    const auto& built = (callee);
    if (const auto* direct = std::get_if<DirectCallable>(&built.storage)) {
        return draft().construction_callable_contract_copy(direct->callable);
    }
    if (const auto* concrete = std::get_if<TypeID>(&built.type)) {
        const auto canonical = draft().type_copy(*concrete);
        auto callable = std::optional<CallableID>();
        std::visit(
            Overloaded {
                [&](const FunctionTypeValue& value) noexcept { callable = value.callable; },
                [&](const ClosureTypeValue& value) noexcept { callable = value.callable; },
                []<typename Value>(const Value&) static noexcept {
                    static_assert(
                        std::same_as<Value, BuiltinTypeValue>
                            || std::same_as<Value, StructTypeValue>
                            || std::same_as<Value, EnumTypeValue>
                            || std::same_as<Value, ArrayTypeValue>
                            || std::same_as<Value, CallableViewTypeValue>
                            || std::same_as<Value, CppTypeValue>,
                        "unhandled non-owning callable type"
                    );
                },
            },
            canonical.value
        );
        if (callable.has_value()) {
            return draft().construction_callable_contract_copy(*callable);
        }
    } else {
        const auto construction = draft().construction_type_copy(std::get<TypeTermID>(built.type));
        if (const auto* view =
                std::get_if<ConstructionCallableViewTypeValue>(&construction.value)) {
            return ConstructionCallableContract {
                .parameters = view->parameters,
                .result = view->result,
                .failures = view->failures,
                .policy = FailureContractPolicy::Declared,
            };
        }
    }
    return std::unexpected(
        fail(span, DiagnosticCode::TypeNotCallable, "expression is not callable")
    );
}

auto BodyElaborator::build_call_argument(
    ASTExprID source_id,
    const ConstructionCallableParameter& parameter
) noexcept -> AnalysisResult<BuiltCallArgument> {
    const auto& source = ast.expression(source_id);
    auto operand_id = source_id;
    auto explicit_access = std::optional<ASTAccessMode>();
    if (const auto* access = std::get_if<ASTAccessExpr>(&source.value)) {
        explicit_access = access->mode;
        operand_id = access->operand_id;
    }
    const auto required_ast = [&]() noexcept {
        switch (parameter.access) {
            case AccessMode::Read:  return ASTAccessMode::Read;
            case AccessMode::Write: return ASTAccessMode::Write;
            case AccessMode::Take:  return ASTAccessMode::Take;
        }
        std::unreachable();
    }();
    if (parameter.access != AccessMode::Read
        && (!explicit_access.has_value() || *explicit_access != required_ast)) {
        return std::unexpected(fail(
            source.span,
            DiagnosticCode::AccessCallMismatch,
            parameter.access == AccessMode::Write
                ? "Write parameter requires an explicit Write argument"
                : "Take parameter requires an explicit Take argument"
        ));
    }
    if (parameter.access == AccessMode::Read
        && explicit_access.has_value()
        && *explicit_access != ASTAccessMode::Read) {
        return std::unexpected(fail(
            source.span,
            DiagnosticCode::AccessCallMismatch,
            "argument access marker differs from the parameter"
        ));
    }
    auto built = expression(operand_id, parameter.type);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    auto pending_failures = take_pending(*built);
    if (parameter.access == AccessMode::Write) {
        auto compatible_storage =
            require_writable_storage_type(built->type, parameter.type, source.span);
        if (!compatible_storage.has_value()) {
            return std::unexpected(compatible_storage.error());
        }
        auto place = as_place(*built, source.span);
        if (!place.has_value()) {
            return std::unexpected(place.error());
        }
        if (built->type != parameter.type) {
            *place = active_builder().append_cpp_place(
                *place,
                parameter.type,
                CppConvertOperation {.explicit_cast = false},
                {},
                origin(source.span)
            );
        }
        return BuiltCallArgument {
            .argument = expression_construction::Argument {expression_construction::WriteArgument {
                .place = *place
            }},
            .pending_failures = std::move(pending_failures),
            .completes = built->completes,
        };
    }
    auto coerced = coerce_to(*built, parameter.type, source.span);
    if (!coerced.has_value()) {
        return std::unexpected(coerced.error());
    }
    auto value = as_value(*built, source.span, parameter.access);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    if (parameter.access == AccessMode::Take) {
        return BuiltCallArgument {
            .argument = expression_construction::Argument {expression_construction::TakeArgument {
                .value = *value
            }},
            .pending_failures = std::move(pending_failures),
            .completes = built->completes,
        };
    }
    return BuiltCallArgument {
        .argument = expression_construction::Argument {expression_construction::ReadArgument {
            .value = *value
        }},
        .pending_failures = std::move(pending_failures),
        .completes = built->completes,
    };
}

auto BodyElaborator::expected_enum_type(
    std::optional<ConstructionTypeRef> expected,
    Span span
) noexcept -> AnalysisResult<TypeID> {
    if (!expected.has_value()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "contextual enum case requires an expected enum type"
        ));
    }
    const auto* concrete = std::get_if<TypeID>(&*expected);
    if (concrete == nullptr
        || !std::holds_alternative<EnumTypeValue>(draft().type_copy(*concrete).value)) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "contextual enum case requires an expected enum type"
        ));
    }
    return *concrete;
}

auto BodyElaborator::enum_case_expression(
    TypeID enumeration_type,
    std::string_view case_name,
    std::span<const ASTCallArgument> arguments,
    Span span,
    Span case_span
) noexcept -> AnalysisResult<BuiltExpression> {
    const auto canonical = draft().type_copy(enumeration_type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    if (nominal == nullptr) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "enum case qualifier does not name an enum type"
        ));
    }
    const auto enumeration = draft().construction_enum_declaration_copy(nominal->enumeration);
    auto selected = std::optional<ConstructionEnumCaseDeclaration>();
    auto selected_id = std::optional<EnumCaseID>();
    for (const auto case_id : enumeration.cases) {
        auto declaration = draft().construction_enum_case_declaration_copy(case_id);
        if (draft().spelling_copy(declaration.name) == case_name) {
            selected = std::move(declaration);
            selected_id = case_id;
            break;
        }
    }
    if (!selected.has_value()) {
        return std::unexpected(fail(
            case_span,
            DiagnosticCode::TypeMemberUnresolved,
            std::format("enum has no case named '{}'", case_name)
        ));
    }
    if (arguments.size() != selected->payload_types.size()) {
        return std::unexpected(
            fail(span, DiagnosticCode::TypeEnumCaseArity, "enum case payload arity does not match")
        );
    }
    auto completes = true;
    auto payload = std::vector<ExpressionHandle>();
    auto payload_constants = std::vector<ConstantID>();
    auto pending_failures = PendingFailureTerms();
    payload.reserve(arguments.size());
    payload_constants.reserve(arguments.size());
    for (auto index = 0uz; index < arguments.size(); ++index) {
        auto built = expression(arguments[index].expression, selected->payload_types[index]);
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        completes &= built->completes;
        append_pending(pending_failures, take_pending(*built));
        auto coerced = coerce_to(
            *built,
            selected->payload_types[index],
            ast.expression(arguments[index].expression).span
        );
        if (!coerced.has_value()) {
            return std::unexpected(coerced.error());
        }
        auto value =
            as_value(*built, ast.expression(arguments[index].expression).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        payload.push_back(*value);
        if (built->constant.has_value()) {
            payload_constants.push_back(*built->constant);
        }
    }
    auto constant = selected->constant;
    if (!payload.empty() && payload_constants.size() == payload.size()) {
        constant = draft().intern_constant(
            ConstantFact {
                .type = enumeration_type,
                .value = PayloadEnumConstant {
                    .enum_case = *selected_id,
                    .payload = std::move(payload_constants),
                },
            }
        );
    }
    const auto value = constant.has_value()
        ? active_builder().append_value(
              enumeration_type,
              active_builder().lifetime(),
              expression_construction::Input {SemConstant {.constant = *constant}},
              origin(span)
          )
        : active_builder().append_value(
              enumeration_type,
              active_builder().lifetime(),
              expression_construction::Input {
                  expression_construction::EnumCase {
                      .enum_case = *selected_id,
                      .payload = std::move(payload)
                  },
              },
              origin(span)
          );
    return BuiltExpression {
        .type = enumeration_type,
        .storage = value,
        .constant = constant,
        .pending_failures = std::move(pending_failures),
        .completes = completes,
    };
}

auto BodyElaborator::enum_case_reference(
    TypeID enumeration_type,
    std::string_view case_name,
    Span span,
    Span case_span
) noexcept -> AnalysisResult<BuiltExpression> {
    const auto canonical = draft().type_copy(enumeration_type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    if (nominal == nullptr) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "enum case qualifier does not name an enum type"
        ));
    }
    const auto enumeration = draft().construction_enum_declaration_copy(nominal->enumeration);
    auto selected = std::optional<ConstructionEnumCaseDeclaration>();
    auto selected_id = std::optional<EnumCaseID>();
    for (const auto case_id : enumeration.cases) {
        auto declaration = draft().construction_enum_case_declaration_copy(case_id);
        if (draft().spelling_copy(declaration.name) == case_name) {
            selected = std::move(declaration);
            selected_id = case_id;
            break;
        }
    }
    if (!selected.has_value()) {
        return std::unexpected(fail(
            case_span,
            DiagnosticCode::TypeMemberUnresolved,
            std::format("enum has no case named '{}'", case_name)
        ));
    }
    if (selected->payload_types.empty()) {
        return enum_case_expression(
            enumeration_type,
            case_name,
            std::span<const ASTCallArgument>(),
            span,
            case_span
        );
    }
    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(selected->payload_types.size());
    for (const auto type : selected->payload_types) {
        parameters.push_back(
            ConstructionCallableParameter {
                .access = AccessMode::Read,
                .type = type,
            }
        );
    }
    const auto type = draft().append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = std::move(parameters),
                .result = enumeration_type,
                .failures = draft().add_empty_failure_term(),
            },
        }
    );
    const auto value = active_builder().append_value(
        type,
        active_builder().lifetime(),
        SemEnumConstructor {.enum_case = *selected_id},
        origin(span)
    );
    return BuiltExpression {
        .type = type,
        .storage = value,
        .constant = std::nullopt,
        .pending_failures = {},
        .takeable = false,
    };
}

auto BodyElaborator::call_expression(
    const ASTCallExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    const auto& callee_source = ast.expression(source.callee);
    if (const auto* contextual = std::get_if<ASTContextualCaseExpr>(&callee_source.value)) {
        auto enumeration = expected_enum_type(expected, contextual->name_span);
        if (!enumeration.has_value()) {
            return std::unexpected(enumeration.error());
        }
        return enum_case_expression(
            *enumeration,
            spelling(contextual->name_span),
            source.arguments,
            span,
            contextual->name_span
        );
    }
    if (const auto* member = std::get_if<ASTMemberExpr>(&callee_source.value);
        member != nullptr && member->op == ASTMemberOperator::Scope) {
        auto enumeration = resolve_enum_qualifier(member->operand_id);
        if (!enumeration.has_value()) {
            return std::unexpected(enumeration.error());
        }
        if (!enumeration->has_value()) {
            return std::unexpected(fail(
                ast.expression(member->operand_id).span,
                DiagnosticCode::TypeEnumContext,
                "enum case qualifier does not name an enum type"
            ));
        }
        return enum_case_expression(
            **enumeration,
            spelling(member->name_span),
            source.arguments,
            span,
            member->name_span
        );
    }
    auto prepared_callee = std::optional<BuiltExpression>();
    if (const auto* member = std::get_if<ASTMemberExpr>(&callee_source.value);
        member != nullptr && member->op == ASTMemberOperator::Dot) {
        auto operand = expression(member->operand_id);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        auto pending_failures = take_pending(*operand);
        const auto* concrete = std::get_if<TypeID>(&operand->type);
        const auto is_text = concrete != nullptr
            && draft().type_copy(*concrete).value
                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}};
        if (is_text) {
            auto decision = decide_text_method(
                draft(),
                operand->type,
                spelling(member->name_span),
                source.arguments.size()
            );
            if (!decision.has_value()) {
                return std::unexpected(fail(
                    member->name_span,
                    decision.error().code,
                    std::string(decision.error().message)
                ));
            }
            if (!decision->has_value()) {
                invariant_violation("str method decision omitted a text intrinsic");
            }
            const auto result_type = draft().intern_builtin_type(text_intrinsic_result(**decision));
            auto folded =
                fold_text_intrinsic_constant(draft(), **decision, operand->constant, result_type);
            if (folded.has_value()) {
                auto result = publish_constant(std::move(*folded), span);
                result.pending_failures = std::move(pending_failures);
                return result;
            }
            if (const auto diagnostic = constant_evaluation_diagnostic(folded.error())) {
                return std::unexpected(
                    fail(member->name_span, diagnostic->code, std::string(diagnostic->message))
                );
            }
            auto source_value =
                as_value(*operand, ast.expression(member->operand_id).span, AccessMode::Read);
            if (!source_value.has_value()) {
                return std::unexpected(source_value.error());
            }
            const auto result = active_builder().append_value(
                result_type,
                active_builder().lifetime(),
                expression_construction::TextOperation {
                    .source = *source_value,
                    .intrinsic = **decision
                },
                origin(span)
            );
            return BuiltExpression {
                .type = result_type,
                .storage = result,
                .constant = std::nullopt,
                .pending_failures = std::move(pending_failures),
                .completes = operand->completes,
            };
        }
        operand->pending_failures = std::move(pending_failures);
        auto projected = member_projection(*member, callee_source.span, std::move(*operand));
        if (!projected.has_value()) {
            return std::unexpected(projected.error());
        }
        prepared_callee = std::move(*projected);
    }
    if (const auto* name = std::get_if<ASTNameExpr>(&callee_source.value)) {
        const auto text = spelling(name->name_span);
        if (find_local(text) == nullptr && !catalog().lookup(source_module_id, text).empty()) {
            auto selected = find_global(text, name->name_span);
            if (!selected.has_value()) {
                return std::unexpected(selected.error());
            }
            if (const auto* enum_case = std::get_if<CatalogEnumCaseForm>(&(*selected)->form)) {
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = enum_case->owner},
                    }
                );
                return enum_case_expression(type, text, source.arguments, span, name->name_span);
            }
        }
    }
    auto callee = [&]() noexcept -> AnalysisResult<BuiltExpression> {
        if (prepared_callee.has_value()) {
            return std::move(*prepared_callee);
        }
        return expression(source.callee);
    }();
    if (!callee.has_value()) {
        return std::unexpected(callee.error());
    }
    if (is_cpp_type(callee->type)) {
        return cpp_call(*callee, source, span);
    }
    auto pending_failures = take_pending(*callee);
    auto contract = callable_contract(*callee, ast.expression(source.callee).span);
    if (!contract.has_value()) {
        return std::unexpected(contract.error());
    }
    if (source.arguments.size() != contract->parameters.size()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeCallArity,
            std::format(
                "call expects {} arguments but received {}",
                contract->parameters.size(),
                source.arguments.size()
            )
        ));
    }
    auto callee_operand = std::optional<expression_construction::Callee>();
    if (const auto* direct = std::get_if<DirectCallable>(&callee->storage)) {
        callee_operand = expression_construction::Callee {direct->callable};
    } else {
        auto value = as_value(*callee, ast.expression(source.callee).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        callee_operand = expression_construction::Callee {*value};
    }
    auto completes = callee->completes;
    auto arguments = std::vector<expression_construction::Argument>();
    arguments.reserve(source.arguments.size());
    for (auto index = 0uz; index < source.arguments.size(); ++index) {
        auto argument =
            build_call_argument(source.arguments[index].expression, contract->parameters[index]);
        if (!argument.has_value()) {
            return std::unexpected(argument.error());
        }
        append_pending(pending_failures, argument->pending_failures);
        completes &= argument->completes;
        arguments.push_back(argument->argument);
    }
    const auto failures = contract->failures;
    append_pending(pending_failures, PendingFailureTerms {failures});
    auto call =
        active_builder()
            .call_expression(*callee_operand, arguments, contract->result, failures, origin(span));
    const auto result = body_builder.add_expression(std::move(call));
    return BuiltExpression {
        .type = contract->result,
        .storage = result,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = completes,
    };
}


} // namespace body_elaboration
