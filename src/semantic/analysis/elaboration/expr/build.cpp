module carven:semantic.analysis.elaboration.expr.build.impl;

import :frontend.ast.expr;
import :frontend.ast.literal;
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

auto constant_initializer_requires_body_scope(const ASTView& ast, ASTExprID id) noexcept -> bool {
    return std::visit(
        [&](const auto& value) noexcept -> bool {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, ASTLambdaExpr>
                          || std::same_as<Value, ASTIfForm>
                          || std::same_as<Value, ASTMatchForm>
                          || std::same_as<Value, ASTTryForm>) {
                return true;
            } else if constexpr (std::same_as<Value, ASTGroupExpr>) {
                return constant_initializer_requires_body_scope(ast, value.expression);
            } else if constexpr (std::same_as<Value, ASTArrayExpr>) {
                return std::ranges::any_of(value.element_ids, [&](ASTExprID element) noexcept {
                    return constant_initializer_requires_body_scope(ast, element);
                });
            } else if constexpr (std::same_as<Value, ASTConstructionExpr>) {
                return std::visit(
                    [&](const auto& initializer) noexcept -> bool {
                        using Initializer = std::remove_cvref_t<decltype(initializer)>;
                        if constexpr (std::same_as<Initializer, std::monostate>) {
                            return false;
                        } else if constexpr (std::same_as<
                                                 Initializer,
                                                 ASTPositionalInitializerList>) {
                            return std::ranges::any_of(
                                initializer.values,
                                [&](ASTExprID element) noexcept {
                                    return constant_initializer_requires_body_scope(ast, element);
                                }
                            );
                        } else {
                            return std::ranges::any_of(
                                initializer.fields,
                                [&](const ASTFieldInitializer& field) noexcept {
                                    return constant_initializer_requires_body_scope(
                                        ast,
                                        field.value
                                    );
                                }
                            );
                        }
                    },
                    value.initializer.value
                );
            } else if constexpr (std::same_as<Value, ASTPrefixExpr>
                                 || std::same_as<Value, ASTAccessExpr>
                                 || std::same_as<Value, ASTCastExpr>
                                 || std::same_as<Value, ASTPropagationExpr>) {
                return constant_initializer_requires_body_scope(ast, value.operand_id);
            } else if constexpr (std::same_as<Value, ASTBinaryExpr>) {
                return constant_initializer_requires_body_scope(ast, value.left)
                    || constant_initializer_requires_body_scope(ast, value.right);
            } else if constexpr (std::same_as<Value, ASTCallExpr>) {
                return constant_initializer_requires_body_scope(ast, value.callee)
                    || std::ranges::any_of(value.arguments, [&](const auto& argument) noexcept {
                           return constant_initializer_requires_body_scope(
                               ast,
                               argument.expression
                           );
                       });
            } else if constexpr (std::same_as<Value, ASTIndexExpr>) {
                return constant_initializer_requires_body_scope(ast, value.operand_id)
                    || constant_initializer_requires_body_scope(ast, value.index);
            } else if constexpr (std::same_as<Value, ASTMemberExpr>) {
                return constant_initializer_requires_body_scope(ast, value.operand_id);
            } else {
                return false;
            }
        },
        ast.expression(id).value
    );
}

auto append_expression(ModuleAnalysis& module_analysis, BuiltExpression expression) noexcept
    -> HIRExprID {
    auto& builder = module_analysis.builder();
    auto constant_id = std::optional<HIRConstantID>();
    if (expression.constant.has_value()) {
        const auto* name = std::get_if<HIRNameExpr>(&expression.value);
        const auto* member = std::get_if<HIRMemberExpr>(&expression.value);
        if (name != nullptr && builder.symbol_constant(name->symbol).has_value()) {
            constant_id = builder.symbol_constant(name->symbol);
        } else if (member != nullptr
                   && std::holds_alternative<HIREnumCaseTarget>(member->target)
                   && builder
                          .symbol_constant(module_analysis.catalog().enum_case_symbol(
                              std::get<HIREnumCaseTarget>(member->target).enum_case
                          ))
                          .has_value()) {
            constant_id = builder.symbol_constant(module_analysis.catalog().enum_case_symbol(
                std::get<HIREnumCaseTarget>(member->target).enum_case
            ));
        } else {
            constant_id = builder.append_constant({
                .type = expression.type,
                .value = *expression.constant,
            });
        }
    }
    const auto id = builder.append_expression({
        .origin = expression.origin,
        .type = expression.type,
        .constant = constant_id,
        .value = std::move(expression.value),
    });
    return id;
}

auto expression_constant(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept
    -> std::optional<HIRConstant> {
    const auto builder = module_analysis.builder();
    const auto constant = builder.expression(id).constant;
    if (!constant.has_value()) {
        return std::nullopt;
    }
    return builder.constant(*constant).value;
}

auto constant_integer(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept
    -> std::optional<HIRIntegerConstant> {
    const auto& constant = expression_constant(module_analysis, id);
    if (constant.has_value()) {
        if (const auto* integer = std::get_if<HIRIntegerConstant>(&*constant)) {
            return *integer;
        }
    }
    return std::nullopt;
}

auto expression_type(const ModuleAnalysis& module_analysis, HIRExprID id) noexcept -> HIRTypeID {
    const auto builder = module_analysis.builder();
    return builder.expression(id).type;
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID id,
    std::optional<HIRTypeID> expected
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    const auto& value = ast.expression(id);
    const auto expression_origin = module_analysis.origin(value.span);
    return std::visit(
        [&](const auto& node) noexcept -> HIRExprID {
            using Node = std::remove_cvref_t<decltype(node)>;
            if constexpr (std::same_as<Node, ASTLambdaExpr>) {
                return build_expression(
                    module_analysis,
                    scopes,
                    control,
                    node,
                    id,
                    expression_origin,
                    expected
                );
            } else if constexpr (std::same_as<Node, ASTPrefixExpr>) {
                return build_expression(
                    module_analysis,
                    scopes,
                    control,
                    node,
                    id,
                    expression_origin,
                    expected
                );
            } else if constexpr (std::same_as<Node, ASTGroupExpr>) {
                return build_expression(
                    module_analysis,
                    scopes,
                    control,
                    node.expression,
                    expected
                );
            } else {
                return build_expression(
                    module_analysis,
                    scopes,
                    control,
                    node,
                    id,
                    expression_origin
                );
            }
        },
        value.value
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack&,
    BodyControl,
    const ASTLiteral& literal,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto result_type = literal_type(module_analysis, literal);
    const auto fact = literal_fact(module_analysis, literal, result_type);
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result_type,
            .constant = fact.constant,
            .value = HIRLiteralExpr {
                .value = fact.value,
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    BodyControl,
    const ASTNameExpr& name,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    auto& builder = module_analysis.builder();
    const auto text = module_analysis.spelling(name.name_span);
    const auto symbol_resolution = symbol_for(module_analysis, scopes, text, name.name_span);
    if (symbol_resolution.has_value()) {
        const auto symbol = symbol_resolution.value();
        if (!module_analysis.resolve_declaration(symbol, name.name_span)) {
            return module_analysis.recover_expression(name.name_span);
        }
        builder.mark_symbol_referenced(symbol);
        const auto symbol_constant = builder.symbol_constant(symbol);
        const auto constant = symbol_constant.has_value()
                                  ? std::optional<HIRConstant> {
                                        builder.constant(*symbol_constant).value,
                                    }
                                  : std::nullopt;
        return append_expression(
            module_analysis,
            {
                .origin = expression_origin,
                .type = symbol_type(module_analysis, symbol),
                .constant = constant,
                .value = HIRNameExpr {
                    .symbol = symbol,
                },
            }
        );
    }
    if (symbol_resolution.error() != LookupError::Missing) {
        return module_analysis.recover_expression(name.name_span);
    }
    return module_analysis.diagnose_and_recover_expression(
        name.name_span,
        std::format("unresolved name '{}'", text),
        DiagnosticCode::NameUnresolved
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack&,
    BodyControl,
    const ASTContextualCaseExpr& contextual,
    ASTExprID,
    ProgramOriginID
) noexcept -> HIRExprID {
    return module_analysis.diagnose_and_recover_expression(
        contextual.name_span,
        "contextual enum case requires an expected enum type",
        DiagnosticCode::TypeEnumContext
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTGroupExpr& group,
    ASTExprID,
    ProgramOriginID
) noexcept -> HIRExprID {
    return build_expression(module_analysis, scopes, control, group.expression);
}
