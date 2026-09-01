module carven:semantic.analysis.elaboration.expr.access.impl;

import :frontend.ast.expr;
import :semantic.analysis.operations;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.symbol;
import :semantic.hir.type;
import std;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTIndexExpr& index,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    const auto& builder = module_analysis.builder();
    const auto& value = ast.expression(id);
    const auto operand = build_expression(module_analysis, scopes, control, index.operand_id);
    const auto index_value = build_expression(module_analysis, scopes, control, index.index);
    const auto operand_type = builder.type(expression_type(module_analysis, operand)).value;
    auto result_type = std::optional<HIRTypeID>();
    if (const auto* array = std::get_if<HIRArrayTypeValue>(&operand_type)) {
        result_type = array->element_type_id;
    } else if (std::holds_alternative<HIRForeignTypeValue>(operand_type)) {
        result_type = expression_type(module_analysis, operand);
    } else if (!std::holds_alternative<HIRErrorTypeValue>(operand_type)) {
        module_analysis.emit(
            value.span,
            "indexed expression is not an array",
            DiagnosticCode::TypeNotIndexable
        );
    }
    if (!is_integer(module_analysis, expression_type(module_analysis, index_value))
        && !is_opaque_or_error(module_analysis, expression_type(module_analysis, index_value))) {
        module_analysis.emit(
            ast.expression(index.index).span,
            "array index must have an integer type",
            DiagnosticCode::TypeIndexInteger
        );
    }
    if (const auto* array = std::get_if<HIRArrayTypeValue>(&operand_type)) {
        const auto constant = constant_integer(module_analysis, index_value);
        if (constant.has_value()
            && (constant->negative() || constant->magnitude() >= array->extent)) {
            module_analysis.emit(
                ast.expression(index.index).span,
                "constant array index is out of bounds",
                DiagnosticCode::ConstIndexBounds
            );
        }
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type =
                result_type.has_value() ? *result_type : error_type(module_analysis, value.span),
            .constant = std::nullopt,
            .value = HIRIndexExpr {.operand_id = operand, .index = index_value},
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTMemberExpr& member,
    ASTExprID id,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto operand = build_expression(module_analysis, scopes, control, member.operand_id);
    return member_expression(
        module_analysis,
        scopes,
        control,
        member,
        id,
        expression_origin,
        operand
    );
}

auto member_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack&,
    BodyControl,
    const ASTMemberExpr& member,
    ASTExprID id,
    ProgramOriginID expression_origin,
    HIRExprID operand
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto operand_type = builder.type(expression_type(module_analysis, operand)).value;
    auto result_type = std::optional<HIRTypeID>();
    auto resolved_constant = std::optional<HIRConstantID>();
    const auto name = module_analysis.spelling(member.name_span);
    const auto scope = member.op == ASTMemberOperator::Scope;
    auto resolved_target = HIRMemberTarget {HIRUnresolvedMemberTarget {
        .name = builder.intern_string(name),
        .scope = scope,
    }};
    if (!scope) {
        const auto* builtin_type = std::get_if<HIRBuiltinTypeValue>(&operand_type);
        if (builtin_type != nullptr && builtin_type->kind == HIRBuiltinType::Str) {
            if (name == "bytes" || name == "chars") {
                const auto intrinsic =
                    name == "bytes" ? HIRTextIntrinsic::Bytes : HIRTextIntrinsic::Chars;
                const auto check = check_text_intrinsic(
                    builder,
                    intrinsic,
                    expression_type(module_analysis, operand)
                );
                return append_expression(
                    module_analysis,
                    {
                        .origin = expression_origin,
                        .type = builtin(module_analysis, member.name_span, check.result),
                        .constant = std::nullopt,
                        .value = HIRTextIntrinsicExpr {
                            .operand_id = operand,
                            .intrinsic = intrinsic,
                        },
                    }
                );
            }
            module_analysis.emit(
                member.name_span,
                name == "len" || name == "is_empty" ? "str len/is_empty must be called"
                                                    : "str has no such property",
                DiagnosticCode::TypeStrProperty
            );
        }
    }
    if (!scope) {
        const auto signature_resolution = resolve_structure_contract(
            module_analysis,
            expression_type(module_analysis, operand),
            member.name_span
        );
        if (signature_resolution.has_value()) {
            const auto signature = signature_resolution.value();
            const auto field = std::ranges::find_if(
                signature.fields,
                [&](const HIRStructField& candidate) noexcept {
                    return builder.provenance().spelling(candidate.name) == name;
                }
            );
            if (field != signature.fields.end()) {
                result_type = field->type;
                const auto* owner = std::get_if<HIRStructTypeValue>(
                    &builder.type(expression_type(module_analysis, operand)).value
                );
                if (owner != nullptr) {
                    resolved_target = HIRStructFieldTarget {
                        .owner = owner->structure,
                        .index = static_cast<std::uint32_t>(field - signature.fields.begin()),
                    };
                }
            } else {
                module_analysis.emit(
                    member.name_span,
                    std::format("structure has no field named '{}'", name),
                    DiagnosticCode::TypeMemberUnresolved
                );
            }
        } else if (signature_resolution.error() == LookupError::Missing) {
            if (std::holds_alternative<HIRForeignTypeValue>(operand_type)) {
                result_type = expression_type(module_analysis, operand);
            } else if (!std::holds_alternative<HIRErrorTypeValue>(operand_type)) {
                module_analysis.emit(
                    member.name_span,
                    "member cannot be resolved for this type",
                    DiagnosticCode::TypeMemberUnresolved
                );
            }
        } else {
            return module_analysis.recover_expression(member.name_span);
        }
    } else {
        const auto member_resolution = resolve_enum_case(
            module_analysis,
            expression_type(module_analysis, operand),
            name,
            member.name_span
        );
        if (member_resolution.has_value()) {
            const auto member_signature = member_resolution.value();
            const auto member_symbol = member_signature.symbol;
            result_type = symbol_type(module_analysis, member_symbol);
            resolved_constant = member_signature.constant;
            resolved_target = HIREnumCaseTarget {
                .enum_case = member_signature.id,
            };
        } else if (member_resolution.error() == LookupError::Missing) {
            if (std::holds_alternative<HIRForeignTypeValue>(operand_type)) {
                result_type = expression_type(module_analysis, operand);
            } else if (!std::holds_alternative<HIRErrorTypeValue>(operand_type)) {
                module_analysis.emit(
                    member.name_span,
                    "member cannot be resolved for this type",
                    DiagnosticCode::TypeMemberUnresolved
                );
            }
        } else {
            return module_analysis.recover_expression(member.name_span);
        }
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result_type.has_value() ? *result_type
                                            : error_type(module_analysis, ast.expression(id).span),
            .constant = [&]() noexcept -> std::optional<HIRConstant> {
                if (!resolved_constant.has_value()) {
                    return std::nullopt;
                }
                return builder.constant(*resolved_constant).value;
            }(),
            .value = HIRMemberExpr {
                .operand_id = operand,
                .target = resolved_target,
            },
        }
    );
}
