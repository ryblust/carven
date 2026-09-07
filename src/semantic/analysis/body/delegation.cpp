module carven:semantic.analysis.body.delegation.impl;

import :diagnostics.code;
import :frontend.ast.expr;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.names;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::is_cpp_type(ConstructionTypeRef type) const noexcept -> bool {
    const auto* concrete = std::get_if<TypeID>(&type);
    return concrete != nullptr
        && std::holds_alternative<CppTypeValue>(draft().type_copy(*concrete).value);
}

auto BodyElaborator::select_cpp_name(const ASTCppNameExpr& name, Span span) noexcept
    -> AnalysisResult<SelectedExpression> {
    auto reference = lookup_cpp_name(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        CppNameLookup::Global,
        name.components
    );
    if (!reference.has_value()) {
        return std::unexpected(reference.error());
    }
    return CppSelection {.target = std::move(**reference), .span = span};
}

auto BodyElaborator::cpp_result_type(
    const CppOperation& operation,
    std::span<const CppTypeOperand> operands
) noexcept -> TypeID {
    return draft().intern_type(
        {.value = CppTypeValue {.form = cpp_query_type(operation, operands)}}
    );
}

auto BodyElaborator::materialize_selection(SelectedExpression selected) noexcept
    -> AnalysisResult<BuiltExpression> {
    if (auto* built = std::get_if<BuiltExpression>(&selected)) {
        return std::move(*built);
    }
    auto& selection = std::get<CppSelection>(selected);
    if (auto* name = std::get_if<CppNameReference>(&selection.target)) {
        return cpp_expression(CppNameOperation {.name = std::move(*name)}, {}, selection.span);
    }
    auto& member = std::get<CppMemberSelection>(selection.target);
    return cpp_projection(
        std::move(member.receiver),
        CppMemberOperation {.name = std::move(member.member)},
        {},
        selection.span
    );
}

auto BodyElaborator::cpp_projection(
    BuiltExpression receiver,
    CppOperation operation,
    std::vector<SemCallArgument> operands,
    Span span
) noexcept -> AnalysisResult<BuiltExpression> {
    auto inputs = std::vector<CppTypeOperand>();
    const auto* concrete = std::get_if<TypeID>(&receiver.type());
    if (concrete == nullptr) {
        invariant_violation("external projection requires a concrete receiver type");
    }
    auto* place = std::get_if<PlaceExpression>(&receiver.storage);
    inputs.push_back(
        {.type = *concrete,
         .access = place != nullptr ? active_builder().place_access(*place) : AccessMode::Read}
    );
    for (const auto& operand : operands) {
        const auto* type = std::get_if<TypeID>(&operand.expression.type.construction());
        if (type == nullptr) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::TypeMismatch,
                "C++ type queries require concrete argument types"
            ));
        }
        inputs.push_back({.type = *type, .access = operand.access});
    }
    const auto type = cpp_result_type(operation, inputs);
    if (place != nullptr) {
        return BuiltExpression {
            .storage = active_builder().cpp_place(
                std::move(*place),
                type,
                std::move(operation),
                std::move(operands),
                origin(span)
            ),

            .pending_failures = std::move(receiver.pending_failures),
            .takeable = false,
            .completes = receiver.completes
        };
    }
    auto pending = take_pending_failures(receiver);
    auto value = consume_value(receiver, span, AccessMode::Read);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    operands.insert(
        operands.begin(),
        {.access = AccessMode::Read, .expression = std::move(*value)}
    );
    auto result = cpp_expression(std::move(operation), std::move(operands), span, type);
    if (result.has_value()) {
        result->pending_failures = std::move(pending);
        result->completes = receiver.completes;
    }
    return result;
}

auto BodyElaborator::cpp_expression(
    CppOperation operation,
    std::vector<SemCallArgument> operands,
    Span span,
    std::optional<ConstructionTypeRef> type
) noexcept -> AnalysisResult<BuiltExpression> {
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
            const auto* concrete = std::get_if<TypeID>(&operand.expression.type.construction());
            if (concrete == nullptr) {
                return std::unexpected(fail(
                    span,
                    DiagnosticCode::TypeMismatch,
                    "C++ type queries require concrete argument types"
                ));
            }
            inputs.push_back({.type = *concrete, .access = operand.access});
        }
        result_type = cpp_result_type(operation, inputs);
    }
    auto value = active_builder().make_expression(
        *result_type,
        active_builder().lifetime(),
        location,
        SemCpp {.operation = std::move(operation), .operands = std::move(operands)}
    );
    return BuiltExpression {
        .storage = std::move(value),

        .pending_failures = {}
    };
}

auto BodyElaborator::cpp_call(
    SelectedExpression selected,
    const ASTCallExpr& source,
    Span span
) noexcept -> AnalysisResult<BuiltExpression> {
    using Operand = SemCppOperand;
    auto completes = true;
    const auto receiver = [&](BuiltExpression& built,
                              Span receiver_span) noexcept -> AnalysisResult<Operand> {
        completes &= built.completes;
        auto consumed = consume_pending(built, receiver_span);
        if (!consumed.has_value()) {
            return std::unexpected(consumed.error());
        }
        if (auto* place = std::get_if<PlaceExpression>(&built.storage)) {
            const auto access = active_builder().place_access(*place);
            return Operand {
                .access = access,
                .expression = UniqueIndirect(std::move(place->expression))
            };
        }
        auto value = consume_value(built, receiver_span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        return Operand {
            .access = AccessMode::Read,
            .expression = UniqueIndirect(std::move(*value))
        };
    };
    auto callee = [&]() noexcept -> AnalysisResult<CppCallee<Operand>> {
        if (auto* selection = std::get_if<CppSelection>(&selected)) {
            if (auto* name = std::get_if<CppNameReference>(&selection->target)) {
                return std::move(*name);
            }
            auto& member = std::get<CppMemberSelection>(selection->target);
            auto value = receiver(member.receiver, selection->span);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            return CppMemberCallee<Operand> {
                .receiver = std::move(*value),
                .member = std::move(member.member)
            };
        }
        auto& built = std::get<BuiltExpression>(selected);
        completes &= built.completes;
        auto value = consume_value(built, ast.expression(source.callee).span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        return Operand {
            .access = AccessMode::Read,
            .expression = UniqueIndirect(std::move(*value))
        };
    }();
    if (!callee.has_value()) {
        return std::unexpected(callee.error());
    }
    auto arguments = std::vector<SemCallArgument>();
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
            auto place = consume_place(*built, ast.expression(operand_id).span);
            if (!place.has_value()) {
                return std::unexpected(place.error());
            }
            arguments.push_back({.access = access, .expression = std::move(place->expression)});
        } else {
            auto value = consume_value(*built, ast.expression(operand_id).span, access);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            arguments.push_back({.access = access, .expression = std::move(*value)});
        }
    }

    auto call = SemCppCall {.callee = std::move(*callee), .arguments = std::move(arguments)};
    auto concrete = true;
    visit_cpp_operands(call, [&](AccessMode, const SemanticExpression& operand) noexcept {
        concrete &= std::holds_alternative<TypeID>(operand.type.construction());
    });
    if (!concrete) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeMismatch,
            "C++ type queries require concrete argument types"
        ));
    }
    const auto query = cpp_call_query(call, [](ConstructionTypeRef type) static noexcept {
        return std::get<TypeID>(type);
    });
    const auto type = draft().intern_type({.value = CppTypeValue {.form = query}});
    auto value =
        active_builder()
            .make_expression(type, active_builder().lifetime(), origin(span), std::move(call));
    return BuiltExpression {
        .storage = std::move(value),

        .pending_failures = {},
        .completes = completes
    };
}
