module carven:backend.target.builder.impl;

import :backend.target.builder;
import :support.invariant;
import std;

auto TargetUnitBuilder::intern_type(TargetType type) noexcept -> TargetTypeID {
    const auto existing = std::ranges::find(storage.types.values(), type);
    if (existing != storage.types.values().end()) {
        return TargetTypeID::from_index(
            static_cast<std::uint32_t>(std::distance(storage.types.values().begin(), existing))
        );
    }
    return storage.types.add(std::move(type));
}

auto TargetUnitBuilder::append_expression(TargetExpr expression) noexcept -> TargetExprID {
    return storage.expressions.add(std::move(expression));
}

auto TargetUnitBuilder::append_statement(TargetStmt statement) noexcept -> TargetStmtID {
    if (!statement.attribution.origin.has_value() && !statement.attribution.reason.has_value()) {
        invariant_violation("target statement append requires explicit attribution");
    }
    return storage.statements.add(std::move(statement));
}

auto TargetUnitBuilder::append_lowering_statement(TargetStmtValue value) noexcept -> TargetStmtID {
    return storage.statements.add({
        .value = std::move(value),
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::LoweringSupport,
        },
    });
}

auto TargetUnitBuilder::append_item(TargetItem item) noexcept -> TargetItemID {
    if (!item.attribution.origin.has_value() && !item.attribution.reason.has_value()) {
        invariant_violation("target item append requires explicit attribution");
    }
    return storage.items.add(std::move(item));
}

auto TargetUnitBuilder::append_lowering_item(TargetItemValue value) noexcept -> TargetItemID {
    return storage.items.add({
        .value = std::move(value),
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::LoweringSupport,
        },
    });
}

auto TargetUnitBuilder::expression(TargetExprID id) const noexcept -> const TargetExpr& {
    return storage.expressions.get(id);
}

auto TargetUnitBuilder::statement(TargetStmtID id) const noexcept -> const TargetStmt& {
    return storage.statements.get(id);
}

auto TargetUnitBuilder::item(TargetItemID id) const noexcept -> const TargetItem& {
    return storage.items.get(id);
}

auto TargetUnitBuilder::type(TargetTypeID id) const noexcept -> const TargetType& {
    return storage.types.get(id);
}

auto TargetUnitBuilder::finish(TargetUnitRoot root) && noexcept -> TargetUnit {
    return TargetUnitFinalizer::finalize(std::move(storage), std::move(root));
}
