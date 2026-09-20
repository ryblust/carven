module carven:backend.realization.decl.impl;

import :backend.realization.decl;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

struct RemovableDeclarations final {
    const std::flat_set<TargetLocalID>& candidates;
    std::map<TargetLocalID, const TargetExpr*> initializers;
    std::map<TargetLocalID, std::size_t> references;

    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
        if (candidates.contains(variable.local)) {
            initializers.emplace(variable.local, &variable.initializer);
        }
        return true;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        if (const auto* local = std::get_if<TargetLocalExpr>(&expression.value)) {
            ++references[local->local];
        }
        return true;
    }
};

struct RemovedInitializerUses final {
    std::map<TargetLocalID, std::size_t>& references;
    std::vector<TargetLocalID>& unused;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        if (const auto* local = std::get_if<TargetLocalExpr>(&expression.value)) {
            if (--references.at(local->local) == 0) {
                unused.push_back(local->local);
            }
        }
        return true;
    }
};

struct DeclarationRemoval final {
    const std::flat_set<TargetLocalID>& unused;

    auto erase(std::vector<TargetStmt>& statements) const noexcept -> void {
        std::erase_if(statements, [&](const TargetStmt& statement) noexcept {
            const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
            return variable != nullptr && unused.contains(variable->local);
        });
    }

    auto enter_expression(TargetExpr& expression, TargetExpressionRole) const noexcept -> bool {
        if (auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value)) {
            erase(lambda->body);
        }
        return true;
    }

    auto enter_statement(TargetStmt& statement) const noexcept -> bool {
        statement.value.visit([&](auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (requires { value.body; }) {
                erase(value.body);
            } else if constexpr (std::same_as<Value, TargetBlockStmt>) {
                erase(value.statements);
            } else if constexpr (std::same_as<Value, TargetIfStmt>) {
                for (auto& branch : value.branches) {
                    erase(branch.body);
                }
                if (value.else_body) {
                    erase(*value.else_body);
                }
            }
            if constexpr (std::same_as<Value, TargetForStmt>) {
                if (value.initializer) {
                    const auto* variable =
                        std::get_if<TargetVariableStmt>(&value.initializer->value);
                    if (variable != nullptr && unused.contains(variable->local)) {
                        value.initializer.reset();
                    }
                }
            }
        });
        return true;
    }
};

auto remove_unused_declarations(
    std::vector<TargetStmt>& statements,
    const std::flat_set<TargetLocalID>& candidates
) noexcept -> void {
    auto declarations =
        RemovableDeclarations {.candidates = candidates, .initializers = {}, .references = {}};
    if (!traverse_target_statements(statements, declarations)) {
        invariant_violation("local reference traversal did not complete");
    }
    auto pending = std::vector<TargetLocalID>();
    for (const auto& [local, initializer] : declarations.initializers) {
        if (!declarations.references.contains(local)) {
            pending.push_back(local);
        }
    }
    auto unused = std::flat_set<TargetLocalID>();
    auto uses = RemovedInitializerUses {.references = declarations.references, .unused = pending};
    while (!pending.empty()) {
        const auto local = pending.back();
        pending.pop_back();
        const auto found = declarations.initializers.find(local);
        if (found != declarations.initializers.end()
            && unused.insert(local).second
            && !traverse_target_expression(*found->second, uses)) {
            invariant_violation("initializer reference traversal did not complete");
        }
    }
    const auto removal = DeclarationRemoval {.unused = unused};
    removal.erase(statements);
    if (!traverse_target_statements(statements, removal)) {
        invariant_violation("local declaration removal did not complete");
    }
}

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
    std::vector<TargetStmt>& statements,
    std::span<const TargetLocalID> parameters,
    const std::flat_set<TargetLocalID>& mutable_owners,
    const std::flat_set<TargetLocalID>& removable_locals
) noexcept -> std::vector<bool> {
    remove_unused_declarations(statements, removable_locals);
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
