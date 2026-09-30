module carven:semantic.analysis.stage.iteration.impl;

import :semantic.analysis.stage.iteration;
import :semantic.semir.body;
import :semantic.semir.table;
import :semantic.semir.traversal;
import std;

namespace {

class IterationBindings final {
public:
    explicit IterationBindings(const StructuredBodyDraft& body) noexcept;
    auto expand(SemanticRegion& region) noexcept -> void;
    auto finish(SemanticRegion region) && noexcept -> StructuredRegionDraft;

private:
    auto local(LifetimeRegionID lifetime) const noexcept -> bool;
    auto lifetime(LifetimeRegionID id) noexcept -> LifetimeRegionID;
    auto binding(LocalBindingID id) noexcept -> LocalBindingID;
    auto pattern(PatternID id) noexcept -> PatternID;
    auto iteration(SemanticRegion& region) noexcept -> void;
    auto expression(SemanticExpression& value) noexcept -> void;
    auto statement(SemanticStatement& value) noexcept -> void;

    MutableBodyTable<LifetimeRegion, LifetimeRegionID> lifetimes;
    MutableBodyTable<ElaboratedLocalBinding, LocalBindingID> bindings;
    MutableBodyTable<ElaboratedPattern, PatternID> patterns;
    LifetimeRegionID root;
    std::map<LifetimeRegionID, LifetimeRegionID> lifetime_ids;
    std::map<LocalBindingID, LocalBindingID> binding_ids;
    std::map<PatternID, PatternID> pattern_ids;
};

IterationBindings::IterationBindings(const StructuredBodyDraft& body) noexcept
    : lifetimes(body.lifetime_regions.owner()),
      bindings(body.bindings.owner()),
      patterns(body.patterns.owner()),
      root(body.region.lifetime) {
    for (const auto entry : body.lifetime_regions.entries()) {
        lifetimes.add(entry.value);
    }
    for (const auto entry : body.bindings.entries()) {
        bindings.add(entry.value);
    }
    for (const auto entry : body.patterns.entries()) {
        patterns.add(entry.value);
    }
}

auto IterationBindings::local(LifetimeRegionID id) const noexcept -> bool {
    auto current = std::optional(id);
    while (current) {
        if (*current == root) {
            return true;
        }
        current = lifetimes.copy(*current).parent;
    }
    return false;
}

auto IterationBindings::lifetime(LifetimeRegionID id) noexcept -> LifetimeRegionID {
    if (const auto found = lifetime_ids.find(id); found != lifetime_ids.end()) {
        return found->second;
    }
    if (!local(id)) {
        return id;
    }
    auto row = lifetimes.copy(id);
    if (row.parent) {
        row.parent = lifetime(*row.parent);
    }
    const auto result = lifetimes.add(std::move(row));
    lifetime_ids.emplace(id, result);
    return result;
}

auto IterationBindings::binding(LocalBindingID id) noexcept -> LocalBindingID {
    if (const auto found = binding_ids.find(id); found != binding_ids.end()) {
        return found->second;
    }
    auto row = bindings.copy(id);
    if (!local(row.lifetime)) {
        return id;
    }
    row.lifetime = lifetime(row.lifetime);
    const auto result = bindings.add(std::move(row));
    binding_ids.emplace(id, result);
    return result;
}

auto IterationBindings::pattern(PatternID id) noexcept -> PatternID {
    if (const auto found = pattern_ids.find(id); found != pattern_ids.end()) {
        return found->second;
    }
    auto row = patterns.copy(id);
    row.value.visit([&](auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, BindingPattern>) {
            value.binding = binding(value.binding);
        } else if constexpr (std::same_as<Value, OrPattern>) {
            for (auto& child : value.alternatives) {
                child = pattern(child);
            }
        } else if constexpr (std::same_as<Value, EnumCasePattern>) {
            for (auto& child : value.payload) {
                child = pattern(child);
            }
        }
    });
    const auto result = patterns.add(std::move(row));
    pattern_ids.emplace(id, result);
    return result;
}

auto IterationBindings::expression(SemanticExpression& value) noexcept -> void {
    value.lifetime = lifetime(value.lifetime);
    value.value.visit([&](auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemBinding>) {
            operation.binding = binding(operation.binding);
        } else if constexpr (std::same_as<Operation, SemMatch> || std::same_as<Operation, SemTry>) {
            for (auto& arm : operation.arms) {
                for (auto& id : arm.bindings) {
                    id = binding(id);
                }
                for (auto& bound : arm.pattern_bounds) {
                    bound.pattern = pattern(bound.pattern);
                }
                if constexpr (std::same_as<Operation, SemMatch>) {
                    arm.pattern = pattern(arm.pattern);
                } else {
                    for (auto& alternative : arm.alternatives) {
                        if (auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                            typed->inner = pattern(typed->inner);
                        }
                    }
                }
            }
        }
    });
}

auto IterationBindings::statement(SemanticStatement& value) noexcept -> void {
    value.lifetime = lifetime(value.lifetime);
    value.value.visit([&](auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemInitialize>
                      || std::same_as<Operation, SemStaticBinding>) {
            operation.binding = binding(operation.binding);
        } else if constexpr (std::same_as<Operation, SemRangeLoop>) {
            operation.lifetime = lifetime(operation.lifetime);
            if (operation.binding) {
                operation.binding = binding(*operation.binding);
            }
        }
    });
}

auto IterationBindings::iteration(SemanticRegion& region) noexcept -> void {
    root = region.lifetime;
    lifetime_ids.clear();
    binding_ids.clear();
    pattern_ids.clear();
    visit_semantic_nodes(region, [&](auto& node) noexcept {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticRegion>) {
            node.lifetime = lifetime(node.lifetime);
        } else if constexpr (std::same_as<Node, SemanticExpression>) {
            expression(node);
        } else {
            statement(node);
        }
    });
}

auto IterationBindings::expand(SemanticRegion& region) noexcept -> void {
    visit_semantic_nodes(region, [&](SemanticStatement& statement) noexcept {
        if (auto* loop = std::get_if<SemExpandedLoop>(&statement.value)) {
            for (auto& region : loop->iterations) {
                iteration(region);
            }
        }
    });
}

auto IterationBindings::finish(SemanticRegion region) && noexcept -> StructuredRegionDraft {
    return {
        .lifetime_regions = LifetimeRegionTree(std::move(lifetimes).seal()),
        .bindings = std::move(bindings).seal(),
        .patterns = std::move(patterns).seal(),
        .region = std::move(region),
    };
}

} // namespace

auto bind_expanded_iterations(const StructuredBodyDraft& source, SemanticRegion region) noexcept
    -> StructuredRegionDraft {
    auto bindings = IterationBindings(source);
    bindings.expand(region);
    return std::move(bindings).finish(std::move(region));
}
