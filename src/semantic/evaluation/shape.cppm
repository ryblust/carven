module carven:semantic.evaluation.shape;

import :semantic.semir.constant_access;
import std;

struct ConstantTypeShape final {
    bool supported;
    std::size_t depth;
    // Counts retained slots, saturated at the element limit plus one.
    std::size_t elements;
};

// Scoped to one construction/execution boundary. Only complete type shapes are
// cached. Incomplete types and shapes beyond the depth limit produce no entry.
class ConstantTypeShapes final {
public:
    explicit ConstantTypeShapes(const ConstantValueAccess& values) noexcept;
    auto get(TypeID type) const noexcept -> std::optional<ConstantTypeShape>;

private:
    auto compute(TypeID type, std::size_t depth) const noexcept -> std::optional<ConstantTypeShape>;

    const ConstantValueAccess& values;
    mutable std::map<TypeID, ConstantTypeShape> completed;
};
