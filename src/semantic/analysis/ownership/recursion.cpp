module carven:semantic.analysis.ownership.recursion.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.callable;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.graph;
import std;

auto ownership_recursion_components(const SemIRProgram& program) noexcept
    -> std::flat_map<BodyID, std::uint32_t> {
    auto ordinals = std::flat_map<BodyID, std::uint32_t>();
    for (const auto [id, body] : program.bodies().entries()) {
        ordinals.emplace(id, static_cast<std::uint32_t>(ordinals.size()));
    }
    const auto body_ordinal = [&](CallableID callable) noexcept -> std::optional<std::uint32_t> {
        const auto body = program.declarations().body_for_callable(callable);
        if (!body.has_value()) {
            return std::nullopt;
        }
        return ordinals.at(*body);
    };
    const auto dynamic_targets = static_cast<std::uint32_t>(ordinals.size());
    auto adjacency = std::vector<std::vector<std::uint32_t>>(ordinals.size() + 1uz);
    // A call without a concrete target may invoke any body that escapes as a
    // callable value. Direct callee operands are not escaping values.
    // One shared node preserves reachability without expanding callers × targets.
    for (const auto [id, body] : program.bodies().entries()) {
        const auto caller = ordinals.at(id);
        auto direct_callees = std::flat_set<const SemanticExpression*>();
        visit_semantic_nodes(body.region(), [&](const SemanticExpression& expression) noexcept {
            if (const auto* call = std::get_if<SemCall>(&expression.value)) {
                auto target = call->target;
                if (!target.has_value()) {
                    target = callable_identity(program, call->callee->type.resolved());
                }
                if (!target.has_value()) {
                    adjacency[caller].push_back(dynamic_targets);
                } else if (const auto callee = body_ordinal(*target)) {
                    adjacency[caller].push_back(*callee);
                }
                direct_callees.insert(std::addressof(*call->callee));
            } else if (const auto* callable = std::get_if<SemCallable>(&expression.value)) {
                if (!direct_callees.contains(std::addressof(expression))) {
                    if (const auto target = body_ordinal(callable->callable)) {
                        adjacency[dynamic_targets].push_back(*target);
                    }
                }
            } else if (const auto* closure = std::get_if<SemClosure>(&expression.value)) {
                if (!direct_callees.contains(std::addressof(expression))) {
                    if (const auto target = body_ordinal(closure->callable)) {
                        adjacency[dynamic_targets].push_back(*target);
                    }
                }
            }
        });
    }
    const auto components = strongly_connected_components(std::move(adjacency)).component_of;
    auto result = std::flat_map<BodyID, std::uint32_t>();
    for (const auto [id, ordinal] : ordinals) {
        result.emplace(id, components[ordinal]);
    }
    return result;
}
