module carven:backend.realization.decl.impl;

import :backend.realization.decl;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

struct LocalUses final {
    std::flat_set<TargetLocalID> referenced;
    std::flat_set<TargetLocalID> read;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole role) noexcept
        -> bool {
        if (const auto* local = std::get_if<TargetLocalExpr>(&expression.value)) {
            referenced.insert(local->local);
            if (role == TargetExpressionRole::Operand) {
                read.insert(local->local);
            }
        }
        return true;
    }
};

struct DeclarationUses final {
    const LocalUses& uses;
    const std::flat_set<TargetLocalID>& mutable_owners;

    template<typename Variable>
    auto visit_variable(Variable& variable) const noexcept -> bool {
        if (variable.binding == TargetVariableBinding::ConstValue
            && mutable_owners.contains(variable.local)) {
            variable.binding = TargetVariableBinding::MutableValue;
        }
        if (uses.read.contains(variable.local)) {
            variable.maybe_unused = false;
        }
        return true;
    }
};

} // namespace

auto finish_body_declarations(
    std::span<TargetStmt> statements,
    std::span<const TargetLocalID> parameters,
    const std::flat_set<TargetLocalID>& mutable_owners
) noexcept -> std::vector<bool> {
    auto uses = LocalUses();
    auto declarations = DeclarationUses {.uses = uses, .mutable_owners = mutable_owners};
    if (!traverse_target_statements(statements, uses)
        || !traverse_target_statements(statements, declarations)) {
        invariant_violation("body declaration traversal did not complete");
    }
    auto referenced = std::vector<bool>();
    for (const auto parameter : parameters) {
        referenced.push_back(uses.referenced.contains(parameter));
    }
    return referenced;
}
