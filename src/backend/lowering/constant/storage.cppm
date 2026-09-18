module carven:backend.lowering.constant.storage;

import :backend.generation.plan;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.name;
import :backend.target.type;
import :semantic.semir.ids;
import std;

// One artifact owns backing identity and pending declarations. take() places
// those declarations after complete private types and before their consumers.
class ConstantStorage final {
public:
    ConstantStorage(const PlannedCompilation& compilation, TargetArtifactID artifact) noexcept;
    ConstantStorage(const ConstantStorage&) = delete;
    ConstantStorage(ConstantStorage&&) = default;
    auto operator=(const ConstantStorage&) -> ConstantStorage& = delete;
    auto find(ConstantID id) const noexcept -> std::optional<TargetName>;
    auto append(ConstantID id, TargetTypeID type, TargetExpr initializer) noexcept -> TargetName;
    auto take() noexcept -> std::vector<TargetItem>;
    auto empty() const noexcept -> bool;

private:
    const PlannedCompilation& compilation;
    TargetArtifactID artifact_id;
    std::optional<TargetIdentifier> storage_namespace;
    std::map<ConstantID, TargetName> names;
    std::vector<TargetItem> items;
};
