module carven:backend.realization.decl.impl;

import :backend.realization.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

class DeclarationUses final {
public:
    DeclarationUses(
        std::span<const TargetIdentifier> parameters,
        std::span<const TargetIdentifier> captures
    ) noexcept
        : parameter_references(parameters.size(), false) {
        for (const auto& capture : captures) {
            declare(capture, {.maybe_unused = nullptr, .parameter_index = std::nullopt});
        }
        for (auto index = 0uz; index < parameters.size(); ++index) {
            declare(parameters[index], {.maybe_unused = nullptr, .parameter_index = index});
        }
    }

    auto enter_scope(TargetTraversalScope) noexcept -> bool {
        scopes.emplace_back();
        return true;
    }

    auto leave_scope(TargetTraversalScope) noexcept -> bool {
        scopes.pop_back();
        return true;
    }

    auto visit_local_parameter(const TargetIdentifier& name) noexcept -> bool {
        declare(name, {.maybe_unused = nullptr, .parameter_index = std::nullopt});
        return true;
    }

    template<typename Variable>
    auto visit_variable(Variable& variable) noexcept -> bool {
        declare(
            variable.name,
            {.maybe_unused = &variable.maybe_unused, .parameter_index = std::nullopt}
        );
        return true;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole role) noexcept
        -> bool {
        const auto* reference = std::get_if<TargetNameExpr>(&expression.value);
        if (reference == nullptr
            || reference->name.is_globally_qualified()
            || reference->name.components().size() != 1uz) {
            return true;
        }
        const auto name = std::string(reference->name.components().front().spelling());
        for (const auto& scope : scopes | std::views::reverse) {
            const auto found = scope.find(name);
            if (found == scope.end()) {
                continue;
            }
            const auto& declaration = found->second;
            if (declaration.parameter_index.has_value()) {
                parameter_references[*declaration.parameter_index] = true;
            }
            if (role == TargetExpressionRole::Operand && declaration.maybe_unused != nullptr) {
                *declaration.maybe_unused = false;
            }
            break;
        }
        return true;
    }

    auto finish() && noexcept -> std::vector<bool> { return std::move(parameter_references); }

private:
    struct Declaration final {
        bool* maybe_unused;
        std::optional<std::size_t> parameter_index;
    };

    auto declare(const TargetIdentifier& name, Declaration declaration) noexcept -> void {
        scopes.back().insert_or_assign(std::string(name.spelling()), declaration);
    }

    std::vector<bool> parameter_references;
    std::vector<std::flat_map<std::string, Declaration>> scopes {1uz};
};

} // namespace

auto finish_body_declarations(
    std::span<TargetStmt> statements,
    std::span<const TargetIdentifier> parameters,
    std::span<const TargetIdentifier> captures
) noexcept -> std::vector<bool> {
    auto uses = DeclarationUses(parameters, captures);
    if (!traverse_target_statements(statements, uses)) {
        invariant_violation("body declaration traversal did not complete");
    }
    return std::move(uses).finish();
}
