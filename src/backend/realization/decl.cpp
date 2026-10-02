module carven:backend.realization.decl.impl;

import :backend.realization.decl;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

struct DeclarationInitializers final {
    const std::flat_map<TargetLocalID, UnusedInitializer>& candidates;
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

struct UnusedDeclarations final {
    const std::flat_set<TargetLocalID>& unused;
    const std::flat_map<TargetLocalID, UnusedInitializer>& candidates;

    auto finish(std::vector<TargetStmt>& statements) const noexcept -> void {
        for (auto& statement : statements) {
            auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
            if (variable != nullptr
                && unused.contains(variable->local)
                && candidates.at(variable->local) == UnusedInitializer::Evaluate) {
                statement.value =
                    TargetDiscardStmt {.expression = std::move(variable->initializer)};
            }
        }
        std::erase_if(statements, [&](const TargetStmt& statement) noexcept {
            const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
            return variable != nullptr && unused.contains(variable->local);
        });
    }

    auto enter_expression(TargetExpr& expression, TargetExpressionRole) const noexcept -> bool {
        if (auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value)) {
            finish(lambda->body);
        }
        return true;
    }

    auto enter_statement(TargetStmt& statement) const noexcept -> bool {
        statement.value.visit([&](auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (requires { value.body; }) {
                finish(value.body);
            } else if constexpr (std::same_as<Value, TargetBlockStmt>) {
                finish(value.statements);
            } else if constexpr (std::same_as<Value, TargetIfStmt>) {
                for (auto& branch : value.branches) {
                    finish(branch.body);
                }
                if (value.else_body) {
                    finish(*value.else_body);
                }
            }
            if constexpr (std::same_as<Value, TargetForStmt>) {
                if (value.initializer) {
                    auto* variable = std::get_if<TargetVariableStmt>(&value.initializer->value);
                    if (variable != nullptr && unused.contains(variable->local)) {
                        if (candidates.at(variable->local) == UnusedInitializer::Omit) {
                            value.initializer.reset();
                        } else {
                            value.initializer->value =
                                TargetDiscardStmt {.expression = std::move(variable->initializer)};
                        }
                    }
                }
            }
        });
        return true;
    }
};

auto finish_unused_declarations(
    std::vector<TargetStmt>& statements,
    const std::flat_map<TargetLocalID, UnusedInitializer>& candidates
) noexcept -> void {
    auto declarations =
        DeclarationInitializers {.candidates = candidates, .initializers = {}, .references = {}};
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
            && candidates.at(local) == UnusedInitializer::Omit
            && !traverse_target_expression(*found->second, uses)) {
            invariant_violation("initializer reference traversal did not complete");
        }
    }
    const auto completed = UnusedDeclarations {.unused = unused, .candidates = candidates};
    completed.finish(statements);
    if (!traverse_target_statements(statements, completed)) {
        invariant_violation("unused declaration finalization did not complete");
    }
}

struct JumpReferences final {
    std::map<std::string, std::size_t> counts;
    bool has_transfer = false;

    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
        if (const auto* jump = std::get_if<TargetGotoStmt>(&statement.value)) {
            ++counts[std::string(jump->label.spelling())];
            has_transfer = true;
        } else if (std::holds_alternative<TargetLabelStmt>(statement.value)) {
            has_transfer = true;
        }
        return true;
    }
};

auto has_transfer(const TargetStmt& statement) noexcept -> bool {
    auto transfers = JumpReferences();
    if (!traverse_target_statement(statement, transfers)) {
        invariant_violation("jump traversal did not complete");
    }
    return transfers.has_transfer;
}

// Restructures the single jump to the label at `exit` when it ends an earlier
// conditional of this list: the statements it skips become the conditional's
// alternative. Only independent blocks may introduce skipped storage; a
// declaration in this list would change scope and cleanup when moved.
auto structure_exit(
    std::vector<TargetStmt>& statements,
    std::size_t exit,
    const TargetIdentifier& label
) noexcept -> bool {
    if (exit == 0uz) {
        return false;
    }
    if (auto* block = std::get_if<TargetBlockStmt>(&statements[exit - 1uz].value)) {
        if (structure_exit(block->statements, block->statements.size(), label)) {
            return true;
        }
    }
    for (auto index = exit; index-- > 0uz;) {
        auto& statement = statements[index];
        auto* conditional = std::get_if<TargetIfStmt>(&statement.value);
        if (conditional != nullptr
            && conditional->branches.size() == 1uz
            && !conditional->else_body
            && !conditional->branches.front().body.empty()) {
            auto& branch = conditional->branches.front();
            const auto* jump = std::get_if<TargetGotoStmt>(&branch.body.back().value);
            if (jump != nullptr && jump->label == label) {
                branch.body.pop_back();
                const auto first = statements.begin() + static_cast<std::ptrdiff_t>(index + 1uz);
                const auto last = statements.begin() + static_cast<std::ptrdiff_t>(exit);
                auto skipped = std::vector<TargetStmt>(
                    std::make_move_iterator(first),
                    std::make_move_iterator(last)
                );
                statements.erase(first, last);
                if (!branch.body.empty()) {
                    if (!skipped.empty()) {
                        conditional->else_body = std::move(skipped);
                    }
                } else if (skipped.empty()) {
                    statement.value = TargetDiscardStmt {.expression = std::move(branch.condition)};
                } else {
                    branch.condition = prefix_expression(
                        TargetPrefixOperator::LogicalNot,
                        std::move(branch.condition)
                    );
                    branch.body = std::move(skipped);
                }
                return true;
            }
        }
        if (std::holds_alternative<TargetVariableStmt>(statement.value)
            || has_transfer(statement)) {
            return false;
        }
    }
    return false;
}

struct ExitStructuring final {
    JumpReferences& references;

    // A jump to the label that immediately follows it transfers nowhere.
    auto remove_adjacent_jumps(std::vector<TargetStmt>& statements) const noexcept -> void {
        for (auto index = 0uz; index + 1uz < statements.size();) {
            const auto* jump = std::get_if<TargetGotoStmt>(&statements[index].value);
            const auto* label = std::get_if<TargetLabelStmt>(&statements[index + 1uz].value);
            if (jump == nullptr || label == nullptr || jump->label != label->label) {
                ++index;
                continue;
            }
            auto& count = references.counts[std::string(label->label.spelling())];
            --count;
            const auto erased = count == 0uz ? 2uz : 1uz;
            statements.erase(
                statements.begin() + static_cast<std::ptrdiff_t>(index),
                statements.begin() + static_cast<std::ptrdiff_t>(index + erased)
            );
        }
    }

    auto list(std::vector<TargetStmt>& statements) const noexcept -> void {
        remove_adjacent_jumps(statements);
        for (auto index = 0uz; index < statements.size(); ++index) {
            const auto* exit = std::get_if<TargetLabelStmt>(&statements[index].value);
            if (exit == nullptr) {
                continue;
            }
            const auto label = exit->label;
            auto& count = references.counts[std::string(label.spelling())];
            const auto size = statements.size();
            if (count != 1uz || !structure_exit(statements, index, label)) {
                continue;
            }
            count = 0uz;
            index -= size - statements.size();
            statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(index));
            --index;
        }
    }

    auto leave_expression(TargetExpr& expression) const noexcept -> bool {
        if (auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value)) {
            list(lambda->body);
        }
        return true;
    }

    auto leave_statement(TargetStmt& statement) const noexcept -> bool {
        statement.value.visit([&](auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (requires { value.body; }) {
                list(value.body);
            } else if constexpr (std::same_as<Value, TargetBlockStmt>) {
                list(value.statements);
            } else if constexpr (std::same_as<Value, TargetIfStmt>) {
                for (auto& branch : value.branches) {
                    list(branch.body);
                }
                if (value.else_body) {
                    list(*value.else_body);
                }
            }
        });
        return true;
    }
};

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
        if ((variable.binding == TargetVariableBinding::ConstValue
             || variable.binding == TargetVariableBinding::ConstSnapshot)
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
    const std::flat_map<TargetLocalID, UnusedInitializer>& unused_initializers
) noexcept -> std::vector<bool> {
    finish_unused_declarations(statements, unused_initializers);
    auto references = JumpReferences();
    if (!traverse_target_statements(statements, references)) {
        invariant_violation("jump reference traversal did not complete");
    }
    const auto structured = ExitStructuring {.references = references};
    if (!traverse_target_statements(statements, structured)) {
        invariant_violation("exit structuring traversal did not complete");
    }
    structured.list(statements);
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
