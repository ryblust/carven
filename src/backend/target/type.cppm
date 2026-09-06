module carven:backend.target.type;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
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

struct TargetQueryCall final {
    auto operator==(const TargetQueryCall&) const noexcept -> bool = default;
};
struct TargetQueryIndex final {
    auto operator==(const TargetQueryIndex&) const noexcept -> bool = default;
};
struct TargetQueryMember final {
    TargetIdentifier name;
    auto operator==(const TargetQueryMember&) const noexcept -> bool = default;
};
struct TargetTypeQuery final {
    std::variant<
        TargetTypeID,
        TargetName,
        TargetQueryCall,
        TargetQueryIndex,
        TargetQueryMember,
        TargetPrefixOperator,
        TargetBinaryOperator>
        operation;
    std::vector<TargetTypeQuery> operands;
    auto operator==(const TargetTypeQuery&) const noexcept -> bool = default;
};
struct TargetDeducedType final {
    TargetTypeQuery query;
    auto operator==(const TargetDeducedType&) const noexcept -> bool = default;
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
