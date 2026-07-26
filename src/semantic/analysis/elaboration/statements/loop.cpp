module carven:semantic.analysis.elaboration.statements.loop.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.stmt;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.statements;
import :semantic.analysis.elaboration.types;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto while_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTWhileStmt& loop,
    Span statement_span,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto condition = build_expression(module_analysis, scopes, control, loop.condition);
    if (!is_bool(module_analysis, expression_type(module_analysis, condition))
        && !is_opaque_or_error(module_analysis, expression_type(module_analysis, condition))) {
        module_analysis.emit(
            ast.expression(loop.condition).span,
            "while condition must have type bool",
            DiagnosticCode::TypeConditionBool
        );
    }
    return builder.append_statement({
        .origin = module_analysis.origin(statement_span),
        .value = HIRWhileStmt {
            .condition = condition,
            .body = build_block(module_analysis, scopes, loop.body, inside_loop(control)),
        },
    });
}

auto for_statement(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTForStmt& loop,
    Span statement_span,
    BodyControl control
) noexcept -> HIRStmtID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto loop_scope = scopes.enter_scope();
    const auto loop_scope_id = scopes.current_scope();
    const auto result = std::visit(
        Overloaded {
            [&](const ASTRangeForHeader& range) noexcept -> HIRStmtID {
                auto target = HIRBindingTarget {HIRDiscardBindingTarget {}};
                auto symbol = std::optional<SymbolID>();
                auto binding_name = std::optional<std::string_view>();
                auto binding_name_span = std::optional<Span>();
                if (const auto* named = std::get_if<ASTNamedBindingTarget>(&range.target)) {
                    const auto name = module_analysis.spelling(named->name_span);
                    binding_name = name;
                    binding_name_span = named->name_span;
                    symbol = module_analysis.append_symbol({
                        .name = builder.intern_string(name),
                        .module_id = std::nullopt,
                        .role = SemanticSymbolRole::LoopBinding,
                        .parent = std::nullopt,
                    });
                    builder.define_symbol_binding(
                        *symbol,
                        range.write_marker.has_value() ? SemanticBindingRole::WriteAlias
                                                       : SemanticBindingRole::ReadAlias,
                        range.write_marker.has_value()
                    );
                    target = HIRNamedBindingTarget {
                        .symbol = *symbol,
                    };
                }
                const auto declared_binding_type = range.type.has_value()
                                                   ? std::optional<HIRTypeID> {
                                                         build_type(module_analysis, scopes, control, *range.type),
                                                     }
                                                   : std::nullopt;
                auto inferred_binding_type = std::optional<HIRTypeID>();
                auto iterable = std::optional<std::variant<HIRExprID, HIRHalfOpenRange>>();
                if (const auto* bounds = std::get_if<ASTHalfOpenRange>(&range.iterable)) {
                    const auto begin =
                        build_expression(module_analysis, scopes, control, bounds->begin);
                    const auto end =
                        build_expression(module_analysis, scopes, control, bounds->end);
                    iterable = HIRHalfOpenRange {.begin = begin, .end = end};
                    inferred_binding_type = expression_type(module_analysis, begin);
                    if (range.write_marker.has_value()) {
                        module_analysis.emit(
                            *range.write_marker,
                            "integer range bindings cannot use mutating access",
                            DiagnosticCode::AccessRangeBinding
                        );
                    }
                    if (!is_integer(module_analysis, expression_type(module_analysis, begin))
                        && !is_opaque_or_error(
                            module_analysis,
                            expression_type(module_analysis, begin)
                        )) {
                        module_analysis.emit(
                            ast.expression(bounds->begin).span,
                            "range bound must have an integer type",
                            DiagnosticCode::TypeRangeInteger
                        );
                    }
                    if (!is_integer(module_analysis, expression_type(module_analysis, end))
                        && !is_opaque_or_error(
                            module_analysis,
                            expression_type(module_analysis, end)
                        )) {
                        module_analysis.emit(
                            ast.expression(bounds->end).span,
                            "range bound must have an integer type",
                            DiagnosticCode::TypeRangeInteger
                        );
                    }
                    if (!compatible(
                            module_analysis,
                            expression_type(module_analysis, begin),
                            expression_type(module_analysis, end)
                        )
                        && !is_opaque_or_error(
                            module_analysis,
                            expression_type(module_analysis, begin)
                        )
                        && !is_opaque_or_error(
                            module_analysis,
                            expression_type(module_analysis, end)
                        )) {
                        module_analysis.emit(
                            bounds->operator_span,
                            "range bounds have incompatible types",
                            DiagnosticCode::TypeRangeBounds
                        );
                    }
                } else {
                    const auto value = build_expression(
                        module_analysis,
                        scopes,
                        control,
                        std::get<ASTExprID>(range.iterable)
                    );
                    iterable = value;
                    const auto& iterable_type =
                        builder.type(expression_type(module_analysis, value)).value;
                    const auto* builtin_type = std::get_if<HIRBuiltinTypeValue>(&iterable_type);
                    if (const auto* array = std::get_if<HIRArrayTypeValue>(&iterable_type)) {
                        inferred_binding_type = array->element_type_id;
                        if (range.write_marker.has_value()) {
                            diagnose_mutation_target(
                                module_analysis,
                                value,
                                ast.expression(std::get<ASTExprID>(range.iterable)).span,
                                "mutating range access requires a mutable iterable",
                                DiagnosticCode::AccessRangeIterable
                            );
                        }
                    } else if (builtin_type != nullptr
                               && (builtin_type->kind == HIRBuiltinType::StrBytesView
                                   || builtin_type->kind == HIRBuiltinType::StrCharsView)) {
                        inferred_binding_type = builtin(
                            module_analysis,
                            binding_target_span(range.target),
                            builtin_type->kind == HIRBuiltinType::StrBytesView
                                ? HIRBuiltinType::U8
                                : HIRBuiltinType::Char
                        );
                        if (range.write_marker.has_value()) {
                            module_analysis.emit(
                                *range.write_marker,
                                "str bytes/chars range bindings only support read access",
                                DiagnosticCode::AccessTextRangeBinding
                            );
                        }
                    } else if (std::holds_alternative<HIRForeignTypeValue>(iterable_type)
                               || std::holds_alternative<HIRErrorTypeValue>(iterable_type)) {
                        inferred_binding_type = expression_type(module_analysis, value);
                    } else {
                        module_analysis.emit(
                            ast.expression(std::get<ASTExprID>(range.iterable)).span,
                            "range source must be an array or an explicit C++ boundary",
                            DiagnosticCode::TypeRangeIterable
                        );
                        inferred_binding_type = error_type(module_analysis, statement_span);
                    }
                }
                const auto binding_type = declared_binding_type.has_value()
                    ? *declared_binding_type
                    : (inferred_binding_type.has_value()
                           ? *inferred_binding_type
                           : error_type(module_analysis, statement_span));
                if (declared_binding_type.has_value()
                    && inferred_binding_type.has_value()
                    && !compatible(module_analysis, *declared_binding_type, *inferred_binding_type)
                    && !is_opaque_or_error(module_analysis, *inferred_binding_type)) {
                    module_analysis.emit(
                        range.type.has_value() ? ast.type(*range.type).span : statement_span,
                        "range binding type is incompatible with the element type",
                        DiagnosticCode::TypeRangeBinding
                    );
                }
                if (symbol.has_value()) {
                    set_symbol_type(module_analysis, *symbol, binding_type);
                    if (!scopes.bind(*binding_name, *symbol)) {
                        invariant_violation("range binding name was already present in its scope");
                    }
                    builder.record_symbol_lint_candidate(
                        *symbol,
                        module_analysis.origin(*binding_name_span)
                    );
                }
                return builder.append_statement({
                    .origin = module_analysis.origin(statement_span),
                    .value = HIRRangeForStmt {
                        .scope = loop_scope_id,
                        .access = range.write_marker.has_value() ? HIRAccessMode::Write
                                                                 : HIRAccessMode::Read,
                        .target = std::move(target),
                        .type = binding_type,
                        .iterable = std::move(*iterable),
                        .body =
                            build_block(module_analysis, scopes, loop.body, inside_loop(control)),
                    },
                });
            },
            [&](const ASTCStyleForHeader& c_style) noexcept -> HIRStmtID {
                const auto initializer = std::visit(
                    Overloaded {
                        [](const std::monostate&) static noexcept -> std::optional<HIRStmtID> {
                            return std::nullopt;
                        },
                        [&](const ASTVariableDecl& value) noexcept -> std::optional<HIRStmtID> {
                            return elaborate_binding(
                                module_analysis,
                                scopes,
                                value,
                                c_style.initializer.span,
                                control
                            );
                        },
                        [&](const ASTAssignment& value) noexcept -> std::optional<HIRStmtID> {
                            return elaborate_assignment(module_analysis, scopes, value, control);
                        },
                        [&](ASTExprID value) noexcept -> std::optional<HIRStmtID> {
                            return elaborate_expression_statement(
                                module_analysis,
                                scopes,
                                value,
                                ast.expression(value).span,
                                control
                            );
                        },
                    },
                    c_style.initializer.value
                );
                auto condition = std::optional<HIRExprID>();
                if (c_style.condition.has_value()) {
                    condition =
                        build_expression(module_analysis, scopes, control, *c_style.condition);
                    if (!is_bool(module_analysis, expression_type(module_analysis, *condition))
                        && !is_opaque_or_error(
                            module_analysis,
                            expression_type(module_analysis, *condition)
                        )) {
                        module_analysis.emit(
                            ast.expression(*c_style.condition).span,
                            "for condition must have type bool",
                            DiagnosticCode::TypeConditionBool
                        );
                    }
                }
                auto steps = std::vector<HIRStmtID>();
                for (const auto& step : c_style.steps) {
                    steps.push_back(
                        std::visit(
                            Overloaded {
                                [&](const ASTAssignment& value) noexcept -> HIRStmtID {
                                    return elaborate_assignment(
                                        module_analysis,
                                        scopes,
                                        value,
                                        control
                                    );
                                },
                                [&](const ASTUpdate& value) noexcept -> HIRStmtID {
                                    return elaborate_update(
                                        module_analysis,
                                        scopes,
                                        value,
                                        control
                                    );
                                },
                                [&](ASTExprID value) noexcept -> HIRStmtID {
                                    return elaborate_expression_statement(
                                        module_analysis,
                                        scopes,
                                        value,
                                        ast.expression(value).span,
                                        control
                                    );
                                },
                            },
                            step.value
                        )
                    );
                }
                return builder.append_statement({
                    .origin = module_analysis.origin(statement_span),
                    .value = HIRCStyleForStmt {
                        .scope = loop_scope_id,
                        .initializer = initializer,
                        .condition = condition,
                        .steps = std::move(steps),
                        .body =
                            build_block(module_analysis, scopes, loop.body, inside_loop(control)),
                    },
                });
            },
        },
        loop.header.value
    );
    return result;
}
