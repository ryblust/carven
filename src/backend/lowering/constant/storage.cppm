module carven:backend.lowering.constant.storage;

import :backend.generation.plan;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.name;
import :backend.target.type;
import :semantic.semir.ids;
import std;

enum class ConstantStorageRole { Elements, Object };

// Content-named backing storage belongs to one target scope within an artifact.
class ConstantStorage final {
public:
    ConstantStorage(const TargetNamePlan& names, TargetName scope) noexcept;
    ConstantStorage(const ConstantStorage&) = delete;
    ConstantStorage(ConstantStorage&&) = default;
    auto operator=(const ConstantStorage&) -> ConstantStorage& = delete;

    auto find(
        ConstantID id,
        ConstantStorageRole role = ConstantStorageRole::Elements
    ) const noexcept -> std::optional<TargetName>;

    auto append(
        ConstantID id,
        TargetTypeID type,
        TargetExpr initializer,
        ConstantStorageRole role = ConstantStorageRole::Elements
    ) noexcept -> TargetName;

    auto take() noexcept -> std::vector<TargetItem>;
    auto empty() const noexcept -> bool;

private:
    const TargetNamePlan& target_names;
    TargetName scope;
    std::set<std::pair<ConstantID, ConstantStorageRole>> materialized;
    std::vector<TargetItem> items;
};
