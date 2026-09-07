module carven:backend.target.expr;

import :backend.target.ids;
import :backend.target.name;
import :backend.target.symbol;
import :support.unique_indirect;
import std;

struct TargetExpr;
struct TargetStmt;

enum class TargetPrefixOperator {
    Increment,
    Decrement,
    AddressOf,
    Dereference,
    LogicalNot,
    Negate,
    BitwiseNot,
};

enum class TargetBinaryOperator {
    LogicalOr,
    LogicalAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseAnd,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LeftShift,
    RightShift,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
};

enum class TargetPrecedence {
    Lowest,
    Conditional,
    LogicalOr,
    LogicalAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseAnd,
    Equality,
    Relational,
    Shift,
    Additive,
    Multiplicative,
    Prefix,
    Postfix,
    Primary,
};

struct TargetNameExpr final {
    TargetName name;
};

struct TargetIntrinsicNameExpr final {
    TargetSymbol symbol;
};

enum class TargetIntegerSuffix {
    None,
    Unsigned,
    LongLong,
    UnsignedLongLong,
};

struct TargetIntegerLiteral final {
    bool negative;
    std::uint64_t magnitude;
    TargetIntegerSuffix suffix;
};

struct TargetFloatLiteral final {
    std::variant<float, double> value;
};

struct TargetCharacterLiteral final {
    char32_t scalar;
};

enum class TargetStringLiteralKind {
    String,
    StringView,
};

struct TargetStringLiteral final {
    std::string bytes;
    TargetStringLiteralKind kind;
};

using TargetLiteralValue = std::variant<
    bool,
    TargetIntegerLiteral,
    TargetFloatLiteral,
    TargetCharacterLiteral,
    TargetStringLiteral>;

struct TargetLiteralExpr final {
    TargetLiteralValue value;
};

struct TargetPrefixExpr final {
    TargetPrefixOperator op;
    UniqueIndirect<TargetExpr> operand;
};

struct TargetBinaryExpr final {
    UniqueIndirect<TargetExpr> left;
    TargetBinaryOperator op;
    UniqueIndirect<TargetExpr> right;
};

struct TargetConditionalExpr final {
    UniqueIndirect<TargetExpr> condition;
    UniqueIndirect<TargetExpr> true_value;
    UniqueIndirect<TargetExpr> false_value;
};

struct TargetCallExpr final {
    UniqueIndirect<TargetExpr> callee;
    std::vector<TargetTypeID> template_argument_type_ids;
    std::vector<TargetExpr> arguments;
};

struct TargetArrayExpr final {
    TargetTypeID element_type_id;
    UniqueIndirect<TargetExpr> extent;
    std::vector<TargetExpr> elements;
};

struct TargetFieldInitializer final {
    TargetIdentifier name;
    UniqueIndirect<TargetExpr> value;
};

struct TargetConstructionExpr final {
    TargetTypeID type;
    std::variant<std::monostate, std::vector<TargetExpr>, std::vector<TargetFieldInitializer>>
        initializer;
};

struct TargetIndexExpr final {
    UniqueIndirect<TargetExpr> operand;
    UniqueIndirect<TargetExpr> index;
};

struct TargetMemberExpr final {
    UniqueIndirect<TargetExpr> operand;
    TargetMemberName name;
};

struct TargetScopeMemberExpr final {
    UniqueIndirect<TargetExpr> operand;
    TargetMemberName name;
};

struct TargetStaticMemberExpr final {
    TargetTypeID owner;
    TargetIdentifier name;
};

struct TargetStaticCastExpr final {
    TargetTypeID type;
    UniqueIndirect<TargetExpr> operand;
};

struct TargetPlacementNewExpr final {
    TargetTypeID type;
    UniqueIndirect<TargetExpr> address;
    UniqueIndirect<TargetExpr> initializer;
};

struct TargetLambdaParameter final {
    TargetIdentifier name;
    TargetTypeID type;
};

struct TargetLambdaExpr final {
    std::vector<TargetLambdaParameter> parameters;
    TargetTypeID result;
    std::vector<TargetStmt> body;
};

using TargetExprValue = std::variant<
    TargetNameExpr,
    TargetIntrinsicNameExpr,
    TargetLiteralExpr,
    TargetPrefixExpr,
    TargetBinaryExpr,
    TargetConditionalExpr,
    TargetCallExpr,
    TargetArrayExpr,
    TargetConstructionExpr,
    TargetIndexExpr,
    TargetMemberExpr,
    TargetScopeMemberExpr,
    TargetStaticMemberExpr,
    TargetStaticCastExpr,
    TargetPlacementNewExpr,
    TargetLambdaExpr>;

struct TargetExpr final {
    TargetExprValue value;
};
