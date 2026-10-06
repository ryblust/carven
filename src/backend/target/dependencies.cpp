module carven:backend.target.dependencies.impl;

import :backend.target.dependencies;
import :backend.target.header;
import :backend.target.symbol;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

class TargetDependencyCollector final {
public:
    TargetDependencyCollector(
        TargetUnitIdentity unit_identity,
        std::span<const TargetType> source_types,
        const TargetUnitSections& source_sections
    ) noexcept;
    auto collect() noexcept -> std::vector<TargetHeaderRequirement>;
    auto visit_type(TargetTypeID id) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;

private:
    auto visit_symbol(TargetSymbol symbol) noexcept -> void;

    TargetUnitIdentity identity;
    std::span<const TargetType> types;
    const TargetUnitSections& sections;
    std::vector<bool> visited_types;
    std::vector<TargetTypeID> pending_types;
    std::flat_set<TargetSymbol> symbols;
};

TargetDependencyCollector::TargetDependencyCollector(
    TargetUnitIdentity unit_identity,
    std::span<const TargetType> source_types,
    const TargetUnitSections& source_sections
) noexcept
    : identity(unit_identity),
      types(source_types),
      sections(source_sections),
      visited_types(source_types.size(), false) {}

auto TargetDependencyCollector::collect() noexcept -> std::vector<TargetHeaderRequirement> {
    if (!traverse_target_unit(sections, *this)) {
        invariant_violation("target dependency traversal failed");
    }
    while (!pending_types.empty()) {
        const auto id = pending_types.back();
        pending_types.pop_back();
        const auto& value = types[id.index()].value;
        value.visit([&](const auto& node) noexcept {
            if (const auto symbol = target_node_symbol(node)) {
                visit_symbol(*symbol);
            }
        });
        if (!visit_target_type_children(value, *this)) {
            invariant_violation("target type dependency traversal failed");
        }
    }
    auto requirements = std::vector<TargetHeaderRequirement>();
    for (const auto symbol : symbols) {
        if (const auto provider = target_symbol_info(symbol).header) {
            requirements.push_back({
                .header =
                    {.delimiter = TargetHeaderDelimiter::AngleBrackets,
                     .path = std::string(provider->path)},
                .group = provider->group,
            });
        }
    }
    return requirements;
}

auto TargetDependencyCollector::visit_type(TargetTypeID id) noexcept -> bool {
    if (id.owner() != identity || id.index() >= types.size()) {
        invariant_violation("target dependency traversal reached an invalid type ID");
    }
    if (visited_types[id.index()]) {
        return true;
    }
    visited_types[id.index()] = true;
    pending_types.push_back(id);
    return true;
}

auto TargetDependencyCollector::enter_expression(
    const TargetExpr& expression,
    TargetExpressionRole
) noexcept -> bool {
    expression.value.visit([&](const auto& node) noexcept {
        if (const auto symbol = target_node_symbol(node)) {
            visit_symbol(*symbol);
        }
    });
    return true;
}

auto TargetDependencyCollector::enter_statement(const TargetStmt& statement) noexcept -> bool {
    statement.value.visit([&](const auto& node) noexcept {
        if (const auto symbol = target_node_symbol(node)) {
            visit_symbol(*symbol);
        }
    });
    return true;
}

auto TargetDependencyCollector::visit_symbol(TargetSymbol symbol) noexcept -> void {
    symbols.insert(symbol);
}

} // namespace

auto collect_target_dependencies(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept -> std::vector<TargetHeaderRequirement> {
    return TargetDependencyCollector(identity, types, sections).collect();
}
