module carven:backend.target.cleanup.impl;

import :backend.target.expr;
import :backend.target.item;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.traversal;
import std;

namespace {
struct Cleanup final {
    static constexpr bool cleanup_edges = true;

    auto leave_expression(TargetExpr& expression) noexcept -> bool;
    auto leave_statement(TargetStmt& statement) noexcept -> bool;
};

auto Cleanup::leave_expression(TargetExpr& expression) noexcept -> bool {
    static_cast<TargetExprValue::Base&>(expression.value)
        .emplace<TargetLiteralExpr>(TargetLiteralExpr {.value = false});
    return true;
}

auto Cleanup::leave_statement(TargetStmt& statement) noexcept -> bool {
    static_cast<TargetStmtValue::Base&>(statement.value).emplace<TargetBreakStmt>();
    return true;
}

} // namespace

auto TargetExpressionCleanup::clear(TargetExprValue& value) noexcept -> void {
    if (std::holds_alternative<TargetLiteralExpr>(value)) {
        return;
    }
    auto root = TargetExpr {.value = std::move(value)};
    auto cleanup = Cleanup();
    static_cast<void>(traverse_target_expression(root, cleanup));
}

auto TargetStatementCleanup::clear(TargetStmtValue& value) noexcept -> void {
    if (std::holds_alternative<TargetBreakStmt>(value)) {
        return;
    }
    auto root = TargetStmt {
        .value = std::move(value),
        .attribution =
            TargetGeneratedExpansionAttribution {.reason = TargetExpansionReason::LoweringSupport},
    };
    auto cleanup = Cleanup();
    static_cast<void>(traverse_target_statement(root, cleanup));
}

auto TargetItemCleanup::clear(TargetItemValue& value) noexcept -> void {
    const auto* space = std::get_if<TargetNamespace>(&value);
    if (space == nullptr) {
        return;
    }

    struct Event final {
        TargetItem* item;
        bool leave;
    };

    auto pending = std::vector<Event>();
    const auto children = [&](TargetNamespace& parent) noexcept {
        for (auto& item : parent.items) {
            pending.push_back({std::addressof(item), false});
        }
    };
    children(std::get<TargetNamespace>(value));
    while (!pending.empty()) {
        const auto event = pending.back();
        pending.pop_back();
        if (event.leave) {
            static_cast<TargetItemValue::Base&>(event.item->value)
                .emplace<TargetRawFragment>(TargetRawFragment {.bytes = {}});
        } else if (auto* child = std::get_if<TargetNamespace>(&event.item->value)) {
            pending.push_back({event.item, true});
            children(*child);
        }
    }
    // Every nested namespace is flat now; declarations contain expression and
    // statement wrappers which handle their own owned descendants.
}
