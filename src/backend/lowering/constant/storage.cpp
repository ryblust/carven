module carven:backend.lowering.constant.storage.impl;

import :backend.lowering.constant.storage;
import :backend.lowering.context;
import :backend.target.builder;
import :backend.target.decl;
import :support.invariant;
import std;

ConstantStorage::ConstantStorage(const TargetNamePlan& names, ModuleID module_id) noexcept
    : target_names(names),
      module_id(module_id) {}

auto ConstantStorage::find(ConstantID id) const noexcept -> std::optional<TargetName> {
    return materialized.contains(id)
        ? std::optional(
              target_names.module_support_name(module_id, target_names.constant_identifier(id))
          )
        : std::nullopt;
}

auto ConstantStorage::empty() const noexcept -> bool {
    return items.empty();
}

auto ConstantStorage::append(ConstantID id, TargetTypeID type, TargetExpr initializer) noexcept
    -> TargetName {
    if (!materialized.insert(id).second) {
        invariant_violation("constant storage was materialized more than once in a module");
    }
    const auto identifier = target_names.constant_identifier(id);
    auto name = target_names.module_support_name(module_id, identifier);
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
