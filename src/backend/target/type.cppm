module carven:backend.target.type;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import std;

struct TargetNamedType final {
    TargetName name;
    std::vector<TargetTypeID> type_argument_ids;
    struct Segment final {
        TargetIdentifier name;
        std::vector<TargetTypeID> type_argument_ids;
        auto operator==(const Segment&) const noexcept -> bool = default;
    };
    std::vector<Segment> nested;
    auto operator==(const TargetNamedType&) const noexcept -> bool = default;
};

struct TargetIntrinsicType final {
    TargetSymbol symbol;
    std::vector<TargetTypeID> type_argument_ids;
    auto operator==(const TargetIntrinsicType&) const noexcept -> bool = default;
};

struct TargetArrayExtent final {
    std::uint64_t magnitude;
    auto operator==(const TargetArrayExtent&) const noexcept -> bool = default;
};

struct TargetArrayType final {
    TargetTypeID element_type_id;
    TargetArrayExtent extent;
    auto operator==(const TargetArrayType&) const noexcept -> bool = default;
};

struct TargetFunctionType final {
    std::vector<TargetTypeID> parameters;
    TargetTypeID result;
    auto operator==(const TargetFunctionType&) const noexcept -> bool = default;
};

struct TargetPointerType final {
    TargetTypeID pointee;
    auto operator==(const TargetPointerType&) const noexcept -> bool = default;
};

struct TargetReferenceType final {
    TargetTypeID referent;
    bool const_qualified;
    bool rvalue;
    auto operator==(const TargetReferenceType&) const noexcept -> bool = default;
};

class TargetDeducedType final {
public:
    explicit TargetDeducedType(TargetExpr expression) noexcept;
    auto expression() const noexcept -> const TargetExpr&;
    auto operator==(const TargetDeducedType& other) const noexcept -> bool;

private:
    TargetExpr queried_expression;
};

using TargetTypeValue = std::variant<
    TargetNamedType,
    TargetIntrinsicType,
    TargetArrayType,
    TargetFunctionType,
    TargetPointerType,
    TargetReferenceType,
    TargetDeducedType>;

struct TargetType final {
    TargetTypeValue value;
    bool const_qualified;
    auto operator==(const TargetType&) const noexcept -> bool = default;
};
