module carven:backend.target.traversal;

import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import :support.visit;
import std;

enum class TargetTraversalScopeKind {
    Namespace,
    Callable,
    Block,
    ConditionalBranch,
    Loop,
};

struct TargetTraversalScope final {
    TargetTraversalScopeKind kind;
    bool initialization_barrier;
};

namespace target_traversal_detail {

template<typename Visitor>
auto visit_type(Visitor& visitor, TargetTypeID id) noexcept -> bool {
    if constexpr (requires { visitor.visit_type(id); }) {
        return visitor.visit_type(id);
    }
    return true;
}

template<typename Visitor>
auto enter_expression(Visitor& visitor, const TargetExpr& expression) noexcept -> bool {
    if constexpr (requires { visitor.enter_expression(expression); }) {
        return visitor.enter_expression(expression);
    }
    return true;
}

template<typename Visitor>
auto leave_expression(Visitor& visitor, const TargetExpr& expression) noexcept -> bool {
    if constexpr (requires { visitor.leave_expression(expression); }) {
        return visitor.leave_expression(expression);
    }
    return true;
}

template<typename Visitor>
auto enter_statement(Visitor& visitor, const TargetStmt& statement) noexcept -> bool {
    if constexpr (requires { visitor.enter_statement(statement); }) {
        return visitor.enter_statement(statement);
    }
    return true;
}

template<typename Visitor>
auto leave_statement(Visitor& visitor, const TargetStmt& statement) noexcept -> bool {
    if constexpr (requires { visitor.leave_statement(statement); }) {
        return visitor.leave_statement(statement);
    }
    return true;
}

template<typename Visitor>
auto enter_declaration(Visitor& visitor, const TargetDecl& declaration) noexcept -> bool {
    if constexpr (requires { visitor.enter_declaration(declaration); }) {
        return visitor.enter_declaration(declaration);
    }
    return true;
}

template<typename Visitor>
auto leave_declaration(Visitor& visitor, const TargetDecl& declaration) noexcept -> bool {
    if constexpr (requires { visitor.leave_declaration(declaration); }) {
        return visitor.leave_declaration(declaration);
    }
    return true;
}

template<typename Visitor>
auto enter_item(Visitor& visitor, const TargetItem& item) noexcept -> bool {
    if constexpr (requires { visitor.enter_item(item); }) {
        return visitor.enter_item(item);
    }
    return true;
}

template<typename Visitor>
auto leave_item(Visitor& visitor, const TargetItem& item) noexcept -> bool {
    if constexpr (requires { visitor.leave_item(item); }) {
        return visitor.leave_item(item);
    }
    return true;
}

template<typename Visitor>
auto enter_scope(Visitor& visitor, TargetTraversalScope scope) noexcept -> bool {
    if constexpr (requires { visitor.enter_scope(scope); }) {
        return visitor.enter_scope(scope);
    }
    return true;
}

template<typename Visitor>
auto leave_scope(Visitor& visitor, TargetTraversalScope scope) noexcept -> bool {
    if constexpr (requires { visitor.leave_scope(scope); }) {
        return visitor.leave_scope(scope);
    }
    return true;
}

template<typename Visitor, typename Action>
auto in_scope(Visitor& visitor, TargetTraversalScope scope, Action action) noexcept -> bool {
    if (!enter_scope(visitor, scope)) {
        return false;
    }
    if (!action()) {
        return false;
    }
    return leave_scope(visitor, scope);
}

} // namespace target_traversal_detail

template<typename Visitor>
auto visit_target_type_children(const TargetTypeValue& value, Visitor& visitor) noexcept -> bool {
    const auto visit_types = [&](std::span<const TargetTypeID> ids) noexcept {
        return std::ranges::all_of(ids, [&](TargetTypeID id) noexcept {
            return target_traversal_detail::visit_type(visitor, id);
        });
    };
    return std::visit(
        Overloaded {
            [&](const TargetDeducedType& deduced) noexcept {
                const auto visit_query = [&](this const auto& self,
                                             const TargetTypeQuery& query) noexcept -> bool {
                    if (const auto* type = std::get_if<TargetTypeID>(&query.operation);
                        type != nullptr && !target_traversal_detail::visit_type(visitor, *type)) {
                        return false;
                    }
                    return std::ranges::all_of(query.operands, self);
                };
                return visit_query(deduced.query);
            },
            [&](const TargetNamedType& named) noexcept {
                if (!visit_types(named.type_argument_ids)) {
                    return false;
                }
                return std::ranges::all_of(named.nested, [&](const auto& segment) noexcept {
                    return visit_types(segment.type_argument_ids);
                });
            },
            [&](const TargetIntrinsicType& intrinsic) noexcept {
                return visit_types(intrinsic.type_argument_ids);
            },
            [&](const TargetArrayType& array) noexcept {
                return target_traversal_detail::visit_type(visitor, array.element_type_id);
            },
            [&](const TargetFunctionType& function) noexcept {
                return visit_types(function.parameters)
                    && target_traversal_detail::visit_type(visitor, function.result);
            },
            [&](const TargetPointerType& pointer) noexcept {
                return target_traversal_detail::visit_type(visitor, pointer.pointee);
            },
            [&](const TargetReferenceType& reference) noexcept {
                return target_traversal_detail::visit_type(visitor, reference.referent);
            },
        },
        value
    );
}

template<typename Visitor>
auto traverse_target_expression(const TargetExpr& expression, Visitor& visitor) noexcept -> bool;

template<typename Visitor>
auto traverse_target_statement(const TargetStmt& statement, Visitor& visitor) noexcept -> bool;

template<typename Visitor>
auto traverse_target_statements(std::span<const TargetStmt> statements, Visitor& visitor) noexcept
    -> bool {
    return std::ranges::all_of(statements, [&](const TargetStmt& statement) noexcept {
        return traverse_target_statement(statement, visitor);
    });
}

template<typename Visitor>
auto traverse_target_expressions(std::span<const TargetExpr> expressions, Visitor& visitor) noexcept
    -> bool {
    return std::ranges::all_of(expressions, [&](const TargetExpr& expression) noexcept {
        return traverse_target_expression(expression, visitor);
    });
}

template<typename Visitor>
auto traverse_target_callable_body(
    std::span<const TargetStmt> statements,
    Visitor& visitor
) noexcept -> bool {
    return target_traversal_detail::in_scope(
        visitor,
        TargetTraversalScope {
            .kind = TargetTraversalScopeKind::Callable,
            .initialization_barrier = false,
        },
        [&]() noexcept { return traverse_target_statements(statements, visitor); }
    );
}

template<typename Visitor>
auto traverse_target_expression(const TargetExpr& expression, Visitor& visitor) noexcept -> bool {
    if (!target_traversal_detail::enter_expression(visitor, expression)) {
        return false;
    }
    const auto children = std::visit(
        Overloaded {
            [](const TargetNameExpr&) static noexcept { return true; },
            [](const TargetIntrinsicNameExpr&) static noexcept { return true; },
            [](const TargetLiteralExpr&) static noexcept { return true; },
            [&](const TargetPrefixExpr& value) noexcept {
                return traverse_target_expression(*value.operand, visitor);
            },
            [&](const TargetBinaryExpr& value) noexcept {
                return traverse_target_expression(*value.left, visitor)
                    && traverse_target_expression(*value.right, visitor);
            },
            [&](const TargetConditionalExpr& value) noexcept {
                return traverse_target_expression(*value.condition, visitor)
                    && traverse_target_expression(*value.true_value, visitor)
                    && traverse_target_expression(*value.false_value, visitor);
            },
            [&](const TargetCallExpr& value) noexcept {
                return traverse_target_expression(*value.callee, visitor)
                    && std::ranges::all_of(
                           value.template_argument_type_ids,
                           [&](TargetTypeID id) noexcept {
                               return target_traversal_detail::visit_type(visitor, id);
                           }
                    )
                    && traverse_target_expressions(value.arguments, visitor);
            },
            [&](const TargetArrayExpr& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.element_type_id)
                    && traverse_target_expression(*value.extent, visitor)
                    && traverse_target_expressions(value.elements, visitor);
            },
            [&](const TargetConstructionExpr& value) noexcept {
                if (!target_traversal_detail::visit_type(visitor, value.type)) {
                    return false;
                }
                return std::visit(
                    Overloaded {
                        [](const std::monostate&) static noexcept { return true; },
                        [&](const std::vector<TargetExpr>& values) noexcept {
                            return traverse_target_expressions(values, visitor);
                        },
                        [&](const std::vector<TargetFieldInitializer>& fields) noexcept {
                            return std::ranges::all_of(fields, [&](const auto& field) noexcept {
                                return traverse_target_expression(*field.value, visitor);
                            });
                        },
                    },
                    value.initializer
                );
            },
            [&](const TargetIndexExpr& value) noexcept {
                return traverse_target_expression(*value.operand, visitor)
                    && traverse_target_expression(*value.index, visitor);
            },
            [&](const TargetMemberExpr& value) noexcept {
                return traverse_target_expression(*value.operand, visitor);
            },
            [&](const TargetScopeMemberExpr& value) noexcept {
                return traverse_target_expression(*value.operand, visitor);
            },
            [&](const TargetStaticMemberExpr& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.owner);
            },
            [&](const TargetStaticCastExpr& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.type)
                    && traverse_target_expression(*value.operand, visitor);
            },
            [&](const TargetRegionExpr& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.result)
                    && traverse_target_callable_body(value.body, visitor);
            },
        },
        expression.value
    );
    return children && target_traversal_detail::leave_expression(visitor, expression);
}

template<typename Visitor, typename Value>
auto traverse_target_for_clause(const Value& value, Visitor& visitor) noexcept -> bool {
    using Clause = std::remove_cvref_t<Value>;
    if constexpr (std::same_as<Clause, TargetExprStmt> || std::same_as<Clause, TargetDiscardStmt>) {
        return traverse_target_expression(value.expression, visitor);
    } else if constexpr (std::same_as<Clause, TargetVariableStmt>) {
        return target_traversal_detail::visit_type(visitor, value.type)
            && traverse_target_expression(value.initializer, visitor);
    } else if constexpr (std::same_as<Clause, TargetAssignmentStmt>) {
        return traverse_target_expression(value.target, visitor)
            && traverse_target_expression(value.value, visitor);
    } else if constexpr (std::same_as<Clause, TargetUpdateStmt>) {
        return traverse_target_expression(value.target, visitor);
    } else {
        static_assert(std::same_as<Clause, void>, "unhandled target for-clause child");
    }
}

template<typename Visitor>
auto traverse_target_statement(const TargetStmt& statement, Visitor& visitor) noexcept -> bool {
    if (!target_traversal_detail::enter_statement(visitor, statement)) {
        return false;
    }
    const auto children = std::visit(
        Overloaded {
            [&](const TargetExprStmt& value) noexcept {
                return traverse_target_expression(value.expression, visitor);
            },
            [&](const TargetDiscardStmt& value) noexcept {
                return traverse_target_expression(value.expression, visitor);
            },
            [&](const TargetReturnStmt& value) noexcept {
                return !value.expression.has_value()
                    || traverse_target_expression(*value.expression, visitor);
            },
            [&](const TargetVariableStmt& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.type)
                    && traverse_target_expression(value.initializer, visitor);
            },
            [&](const TargetBlockStmt& value) noexcept {
                if (!value.scoped) {
                    return traverse_target_statements(value.statements, visitor);
                }
                return target_traversal_detail::in_scope(
                    visitor,
                    TargetTraversalScope {
                        .kind = TargetTraversalScopeKind::Block,
                        .initialization_barrier = false,
                    },
                    [&]() noexcept { return traverse_target_statements(value.statements, visitor); }
                );
            },
            [&](const TargetAssignmentStmt& value) noexcept {
                return traverse_target_expression(value.target, visitor)
                    && traverse_target_expression(value.value, visitor);
            },
            [&](const TargetUpdateStmt& value) noexcept {
                return traverse_target_expression(value.target, visitor);
            },
            [](const TargetBreakStmt&) static noexcept { return true; },
            [](const TargetContinueStmt&) static noexcept { return true; },
            [](const TargetUnreachableStmt&) static noexcept { return true; },
            [](const TargetRuntimeTrapStmt&) static noexcept { return true; },
            [](const TargetGotoStmt&) static noexcept { return true; },
            [](const TargetLabelStmt&) static noexcept { return true; },
            [&](const TargetIfStmt& value) noexcept {
                for (const auto& branch : value.branches) {
                    if (!traverse_target_expression(branch.condition, visitor)
                        || !target_traversal_detail::in_scope(
                            visitor,
                            TargetTraversalScope {
                                .kind = TargetTraversalScopeKind::ConditionalBranch,
                                .initialization_barrier = false,
                            },
                            [&]() noexcept {
                                return traverse_target_statements(branch.body, visitor);
                            }
                        )) {
                        return false;
                    }
                }
                return !value.else_body.has_value()
                    || target_traversal_detail::in_scope(
                        visitor,
                        TargetTraversalScope {
                            .kind = TargetTraversalScopeKind::ConditionalBranch,
                            .initialization_barrier = false,
                        },
                        [&]() noexcept {
                            return traverse_target_statements(*value.else_body, visitor);
                        }
                    );
            },
            [&](const TargetWhileStmt& value) noexcept {
                return traverse_target_expression(value.condition, visitor)
                    && target_traversal_detail::in_scope(
                           visitor,
                           TargetTraversalScope {
                               .kind = TargetTraversalScopeKind::Loop,
                               .initialization_barrier = false,
                           },
                           [&]() noexcept {
                               return traverse_target_statements(value.body, visitor);
                           }
                    );
            },
            [&](const TargetRangeForStmt& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.type)
                    && traverse_target_expression(value.range, visitor)
                    && target_traversal_detail::in_scope(
                           visitor,
                           TargetTraversalScope {
                               .kind = TargetTraversalScopeKind::Loop,
                               .initialization_barrier = true,
                           },
                           [&]() noexcept {
                               return traverse_target_statements(value.body, visitor);
                           }
                    );
            },
            [&](const TargetForStmt& value) noexcept {
                const auto initialized = value.initializer.has_value()
                    && std::holds_alternative<TargetVariableStmt>(value.initializer->value);
                return target_traversal_detail::in_scope(
                    visitor,
                    TargetTraversalScope {
                        .kind = TargetTraversalScopeKind::Loop,
                        .initialization_barrier = initialized,
                    },
                    [&]() noexcept {
                        if (value.initializer.has_value()
                            && !std::visit(
                                [&](const auto& clause) noexcept {
                                    return traverse_target_for_clause(clause, visitor);
                                },
                                value.initializer->value
                            )) {
                            return false;
                        }
                        if (value.condition.has_value()
                            && !traverse_target_expression(*value.condition, visitor)) {
                            return false;
                        }
                        for (const auto& step : value.steps) {
                            if (!std::visit(
                                    [&](const auto& clause) noexcept {
                                        return traverse_target_for_clause(clause, visitor);
                                    },
                                    step.value
                                )) {
                                return false;
                            }
                        }
                        return traverse_target_statements(value.body, visitor);
                    }
                );
            },
        },
        statement.value
    );
    return children && target_traversal_detail::leave_statement(visitor, statement);
}

template<typename Visitor>
auto traverse_target_parameter(const TargetParameter& parameter, Visitor& visitor) noexcept
    -> bool {
    return target_traversal_detail::visit_type(visitor, parameter.type)
        && (!parameter.default_value.has_value()
            || traverse_target_expression(*parameter.default_value, visitor));
}

template<typename Visitor>
auto traverse_target_parameters(
    std::span<const TargetParameter> parameters,
    Visitor& visitor
) noexcept -> bool {
    return std::ranges::all_of(parameters, [&](const TargetParameter& parameter) noexcept {
        return traverse_target_parameter(parameter, visitor);
    });
}

template<typename Visitor>
auto traverse_target_member_function(
    const TargetMemberFunctionDecl& function,
    Visitor& visitor
) noexcept -> bool {
    if (!traverse_target_parameters(function.parameters, visitor)
        || !target_traversal_detail::visit_type(visitor, function.result)) {
        return false;
    }
    const auto* definition = std::get_if<TargetMemberFunctionDefinition>(&function.form);
    return definition == nullptr || traverse_target_callable_body(definition->body, visitor);
}

template<typename Visitor>
auto traverse_target_record_member(const TargetRecordMember& member, Visitor& visitor) noexcept
    -> bool {
    return std::visit(
        Overloaded {
            [&](const TargetStructField& field) noexcept {
                return target_traversal_detail::visit_type(visitor, field.type);
            },
            [&](const TargetMemberFunctionDecl& function) noexcept {
                return traverse_target_member_function(function, visitor);
            },
        },
        member
    );
}

template<typename Visitor>
auto traverse_target_class_member(const TargetClassMember& member, Visitor& visitor) noexcept
    -> bool {
    return std::visit(
        Overloaded {
            [&](const TargetMemberVariable& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.type);
            },
            [&](const TargetNestedRecord& value) noexcept {
                return std::ranges::all_of(value.members, [&](const auto& nested) noexcept {
                    return traverse_target_record_member(nested, visitor);
                });
            },
            [&](const TargetTypeAlias& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.type);
            },
            [&](const TargetConstructorDecl& value) noexcept {
                return traverse_target_parameters(value.parameters, visitor)
                    && std::ranges::all_of(
                           value.initializers,
                           [&](const auto& initializer) noexcept {
                               return traverse_target_expression(initializer.value, visitor);
                           }
                    );
            },
            [&](const TargetMemberFunctionDecl& value) noexcept {
                return traverse_target_member_function(value, visitor);
            },
        },
        member
    );
}

template<typename Visitor>
auto traverse_target_declaration(const TargetDecl& declaration, Visitor& visitor) noexcept -> bool {
    if (!target_traversal_detail::enter_declaration(visitor, declaration)) {
        return false;
    }
    const auto children = std::visit(
        Overloaded {
            [&](const TargetFunctionDecl& value) noexcept {
                if (!traverse_target_parameters(value.parameters, visitor)
                    || !target_traversal_detail::visit_type(visitor, value.result)) {
                    return false;
                }
                const auto* definition = std::get_if<TargetFreeFunctionDefinition>(&value.form);
                return definition == nullptr
                    || traverse_target_callable_body(definition->body, visitor);
            },
            [&](const TargetStructDecl& value) noexcept {
                return std::ranges::all_of(value.members, [&](const auto& member) noexcept {
                    return traverse_target_record_member(member, visitor);
                });
            },
            [](const TargetStructForwardDecl&) static noexcept { return true; },
            [&](const TargetEnumDecl& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.underlying_type)
                    && std::ranges::all_of(value.cases, [&](const auto& item) noexcept {
                           return traverse_target_expression(item.value, visitor);
                       });
            },
            [&](const TargetEnumForwardDecl& value) noexcept {
                return target_traversal_detail::visit_type(visitor, value.underlying_type);
            },
            [&](const TargetClassDecl& value) noexcept {
                for (const auto& section : value.sections) {
                    for (const auto& member : section.members) {
                        if (!traverse_target_class_member(member, visitor)) {
                            return false;
                        }
                    }
                }
                return true;
            },
            [](const TargetClassForwardDecl&) static noexcept { return true; },
        },
        declaration
    );
    return children && target_traversal_detail::leave_declaration(visitor, declaration);
}

template<typename Visitor>
auto traverse_target_item(const TargetItem& item, Visitor& visitor) noexcept -> bool;

template<typename Visitor>
auto traverse_target_items(std::span<const TargetItem> items, Visitor& visitor) noexcept -> bool {
    return std::ranges::all_of(items, [&](const TargetItem& item) noexcept {
        return traverse_target_item(item, visitor);
    });
}

template<typename Visitor>
auto traverse_target_item(const TargetItem& item, Visitor& visitor) noexcept -> bool {
    if (!target_traversal_detail::enter_item(visitor, item)) {
        return false;
    }
    const auto children = std::visit(
        Overloaded {
            [&](const TargetDecl& declaration) noexcept {
                return traverse_target_declaration(declaration, visitor);
            },
            [&](const TargetNamespace& target_namespace) noexcept {
                return target_traversal_detail::in_scope(
                    visitor,
                    TargetTraversalScope {
                        .kind = TargetTraversalScopeKind::Namespace,
                        .initialization_barrier = false,
                    },
                    [&]() noexcept {
                        return traverse_target_items(target_namespace.items, visitor);
                    }
                );
            },
            [](const TargetUsing&) static noexcept { return true; },
            [](const TargetRawFragment&) static noexcept { return true; },
        },
        item.value
    );
    return children && target_traversal_detail::leave_item(visitor, item);
}

template<typename Visitor>
auto traverse_target_unit(const TargetUnitSections& sections, Visitor& visitor) noexcept -> bool {
    return traverse_target_items(sections.preamble, visitor)
        && traverse_target_items(sections.body, visitor)
        && traverse_target_items(sections.epilogue, visitor);
}
