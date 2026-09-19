module carven:backend.emission.construction.impl;

import :backend.emission.render;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

struct TypeEvent final {
    TargetTypeID type;
    bool finish;
};

struct TypeDependencies final {
    std::vector<TypeEvent>& pending;
    auto visit_type(TargetTypeID child) noexcept -> bool;
};

auto TypeDependencies::visit_type(TargetTypeID child) noexcept -> bool {
    pending.push_back({.type = child, .finish = false});
    return true;
}

} // namespace

auto TargetRenderer::LayoutConstruction::visit_type(TargetTypeID type) noexcept -> bool {
    renderer.build_type_layouts(type);
    return true;
}

auto TargetRenderer::LayoutConstruction::leave_expression(const TargetExpr& expression) noexcept
    -> bool {
    renderer.expression_layouts.emplace(
        std::addressof(expression),
        renderer.render_expression_node(expression)
    );
    return true;
}

auto TargetRenderer::LayoutConstruction::leave_statement(const TargetStmt& statement) noexcept
    -> bool {
    renderer.statement_layouts.emplace(
        std::addressof(statement),
        renderer.render_statement_node(statement)
    );
    return true;
}

auto TargetRenderer::LayoutConstruction::leave_item(const TargetItem& item) noexcept -> bool {
    renderer.item_layouts.emplace(std::addressof(item), renderer.render_item_node(item));
    return true;
}

auto TargetRenderer::build_layouts() noexcept -> void {
    type_layouts.resize(unit.type_count());
    auto construction = LayoutConstruction {*this};
    if (!traverse_target_unit(unit.sections(), construction)) {
        invariant_violation("target layout construction did not complete");
    }
}

auto TargetRenderer::build_type_layouts(TargetTypeID type) noexcept -> void {
    if (type_layouts.at(type.index())) {
        return;
    }


    auto pending = std::vector<TypeEvent> {{type, false}};
    // Type construction has already prohibited cycles; no target node is
    // replaced here, so dependency pointers remain valid throughout rendering.
    while (!pending.empty()) {
        const auto event = pending.back();
        pending.pop_back();
        if (type_layouts.at(event.type.index())) {
            continue;
        }
        const auto& value = unit.type(event.type);
        if (!event.finish) {
            pending.push_back({event.type, true});

            auto dependencies = TypeDependencies {.pending = pending};

            if (!visit_target_type_children(value.value, dependencies)) {
                invariant_violation("target type layout dependency traversal did not complete");
            }
            continue;
        }
        if (const auto* query = std::get_if<TargetDecltypeType>(&value.value)) {
            auto construction = LayoutConstruction {*this};
            if (!traverse_target_expression(query->expression(), construction)) {
                invariant_violation("target type query layout traversal did not complete");
            }
        }
        type_layouts[event.type.index()] =
            std::array {render_type_node(event.type, false), render_type_node(event.type, true)};
    }
}
