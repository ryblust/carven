module carven:backend.lowering.constant.storage.impl;

import :backend.lowering.constant.storage;
import :backend.lowering.context;
import :backend.target.builder;
import :backend.target.decl;
import :support.invariant;
import std;

ConstantStorage::ConstantStorage(const TargetNamePlan& names, TargetName scope) noexcept
    : target_names(names),
      scope(
          TargetName::globally_qualified(
              std::vector<TargetIdentifier>(scope.components().begin(), scope.components().end())
          )
      ) {}

auto ConstantStorage::find(ConstantID id) const noexcept -> std::optional<TargetName> {
    if (!materialized.contains(id)) {
        return std::nullopt;
    }
    auto name = scope;
    name.append(target_names.constant_identifier(id));
    return name;
}

auto ConstantStorage::empty() const noexcept -> bool {
    return items.empty();
}

auto ConstantStorage::append(ConstantID id, TargetTypeID type, TargetExpr initializer) noexcept
    -> TargetName {
    if (!materialized.insert(id).second) {
        invariant_violation("constant storage was materialized more than once in a target scope");
    }
    const auto identifier = target_names.constant_identifier(id);
    auto name = scope;
    name.append(identifier);
    items.push_back(target_lowering_item(
        TargetDecl {TargetVariableDecl {
            .name = identifier,
            .type = type,
            .initializer = std::move(initializer),
            .inline_specifier = true,
            .constexpr_specifier = true,
        }}
    ));
    return name;
}

auto ConstantStorage::take() noexcept -> std::vector<TargetItem> {
    return std::exchange(items, {});
}
