module carven:semantic.analysis.body.cpp.impl;

import :diagnostics.code;
import :frontend.ast.expr;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.cpp.identifier;
import std;

namespace body_elaboration {

auto BodyElaborator::is_cpp_type(ConstructionTypeRef type) const noexcept -> bool {
    const auto* concrete = std::get_if<TypeID>(&type);
    return concrete != nullptr
        && std::holds_alternative<CppTypeValue>(draft().type_copy(*concrete).value);
}

auto BodyElaborator::cpp_expression(
    CppOperation operation,
    std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>> operands,
    Span span,
    std::optional<ConstructionTypeRef> type
) noexcept -> AnalysisResult<BuiltExpression> {
    if (const auto* name = std::get_if<CppNameOperation>(&operation);
        name != nullptr && !is_supported_cpp_identifier(name->name)) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::CppIdentifier,
            "external name cannot be represented as a C++ identifier"
        ));
    }
    if (type.has_value() && std::holds_alternative<CppConvertOperation>(operation)) {
        const auto* concrete = std::get_if<TypeID>(&*type);
        const auto borrowed = concrete != nullptr
            ? std::holds_alternative<CallableViewTypeValue>(draft().type_copy(*concrete).value)
            : std::holds_alternative<ConstructionCallableViewTypeValue>(
                  draft().construction_type_copy(std::get<TypeTermID>(*type)).value
              );
        if (borrowed) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeCallableViewEscape,
                "an undeclared C++ contract cannot establish a Carven callable borrow"
            ));
        }
    }
    const auto location = origin(span);
    auto result_type = type;
    if (!result_type.has_value()) {
        auto inputs = std::vector<CppTypeOperand>();
        for (const auto& operand : operands) {
            const auto* concrete = std::get_if<TypeID>(&operand.expression.type);
            if (concrete == nullptr) {
                return std::unexpected(fail(
                    span,
                    DiagnosticCode::TypeMismatch,
                    "C++ type queries require concrete argument types"
                ));
            }
            inputs.push_back({.type = *concrete, .access = operand.access});
        }
        result_type = draft().intern_type(
            {.value = CppTypeValue {
                 .form = CppDeducedType {
                     .origin = location,
                     .operation = operation,
                     .operands = std::move(inputs)
                 }
             }}
        );
    }
    auto value = active_builder().make_expression(
        *result_type,
        active_builder().lifetime(),
        location,
        SemCpp<ConstructionTypeRef, FailureTermID> {
            .operation = std::move(operation),
            .operands = std::move(operands)
        }
    );
    return BuiltExpression {
        .type = *result_type,
        .storage = active_builder().add_expression(std::move(value)),
        .constant = std::nullopt,
        .pending_failures = {}
    };
}

auto BodyElaborator::cpp_call(
    BuiltExpression& callee,
    const ASTCallExpr& source,
    Span span
) noexcept -> AnalysisResult<BuiltExpression> {
    auto completes = callee.completes;
    auto callee_value = as_value(callee, ast.expression(source.callee).span, AccessMode::Read);
    if (!callee_value.has_value()) {
        return std::unexpected(callee_value.error());
    }
    auto operands = std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>>();
    operands.push_back(
        {.access = AccessMode::Read, .expression = active_builder().take_value(*callee_value)}
    );
    for (const auto& argument : source.arguments) {
        auto operand_id = argument.expression;
        auto access = AccessMode::Read;
        if (const auto* marker = std::get_if<ASTAccessExpr>(&ast.expression(operand_id).value)) {
            access = marker->mode == ASTAccessMode::Write ? AccessMode::Write : AccessMode::Take;
            operand_id = marker->operand_id;
        }
        auto built = expression(operand_id);
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        completes = completes && built->completes;
        if (access == AccessMode::Write) {
            auto place = as_place(*built, ast.expression(operand_id).span);
            if (!place.has_value()) {
                return std::unexpected(place.error());
            }
            operands.push_back(
                {.access = access, .expression = active_builder().take_place(*place)}
            );
        } else {
            auto value = as_value(*built, ast.expression(operand_id).span, access);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            operands.push_back(
                {.access = access, .expression = active_builder().take_value(*value)}
            );
        }
    }
    auto result = cpp_expression(CppCallOperation {}, std::move(operands), span);
    if (result.has_value()) {
        result->completes = completes;
    }
    return result;
}
}
