module carven:semantic.analysis.ownership.recursion.impl;

import :semantic.analysis.ownership.context;
import :semantic.semir.callable;
import :semantic.semir.program;
import :support.graph;
import std;

OwnershipRecursionBuilder::OwnershipRecursionBuilder(const SemIRProgram& program) noexcept
    : program(program) {
    for (const auto [id, body] : program.bodies().entries()) {
        ordinals.emplace(id, static_cast<std::uint32_t>(ordinals.size()));
    }
    adjacency.resize(ordinals.size() + 1uz);
}

auto OwnershipRecursionBuilder::body_ordinal(CallableID callable) const noexcept
    -> std::optional<std::uint32_t> {
    const auto body = program.declarations().body_for_callable(callable);
    if (!body.has_value()) {
        return std::nullopt;
    }
    return ordinals.at(*body);
}

auto OwnershipRecursionBuilder::begin_body(BodyID body) noexcept -> void {
    caller = ordinals.at(body);
    direct_callees = std::flat_set<const SemanticExpression*>();
}

auto OwnershipRecursionBuilder::observe(const SemanticExpression& expression) noexcept -> void {
    const auto dynamic_targets = static_cast<std::uint32_t>(ordinals.size());
    // A call without a concrete target may invoke any body that escapes as a
    // callable value. Direct callee operands are not escaping values.
    // One shared node preserves reachability without expanding callers × targets.
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
}

auto OwnershipRecursionBuilder::finish() && noexcept -> std::flat_map<BodyID, std::uint32_t> {
    direct_callees = std::flat_set<const SemanticExpression*>();
    const auto components = strongly_connected_components(std::move(adjacency)).component_of;
    auto result = std::flat_map<BodyID, std::uint32_t>();
    for (const auto [id, ordinal] : ordinals) {
        result.emplace(id, components[ordinal]);
    }
    return result;
}
