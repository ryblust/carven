module carven:backend.target.expr;

import :backend.target.ids;
import :backend.target.name;
import :backend.target.raw;
import :backend.target.symbol;
import std;

enum class TargetPrefixOperator {
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
    TargetExprID operand_id;
};

struct TargetBinaryExpr final {
    TargetExprID left;
    TargetBinaryOperator op;
    TargetExprID right;
};

struct TargetCallExpr final {
    TargetExprID callee;
    std::vector<TargetTemplateArgument> template_arguments;
    std::vector<TargetExprID> arguments;
};

struct TargetArrayExpr final {
    TargetTypeID element_type_id;
    TargetExprID extent;
    std::vector<TargetExprID> element_ids;
};

struct TargetFieldInitializer final {
    TargetIdentifier name;
    TargetExprID value;
};

struct TargetConstructionExpr final {
    TargetTypeID type;
    std::variant<std::monostate, std::vector<TargetExprID>, std::vector<TargetFieldInitializer>>
        initializer;
};

struct TargetIndexExpr final {
    TargetExprID operand_id;
    TargetExprID index;
};

struct TargetMemberExpr final {
    TargetExprID operand_id;
    TargetMemberName name;
};

struct TargetScopeMemberExpr final {
    std::variant<TargetExprID, TargetRawFragment> operand;
    TargetMemberName name;
};

struct TargetStaticMemberExpr final {
    TargetTypeID owner;
    TargetIdentifier name;
};

struct TargetForwardExpr final {
    TargetIdentifier name;
};

struct TargetStaticCastExpr final {
    TargetTypeID type;
    TargetExprID operand_id;
};

enum class TargetIIFEReason {
    ConditionalExpression,
    MatchExpression,
    TryBody,
    FailureBoundary,
};

struct TargetLambdaExpr final {
    TargetIIFEReason reason;
    std::vector<TargetStmtID> body;
};

enum class TargetCaptureMode {
    Value,
    Write,
};

struct TargetClosureCapture final {
    TargetCaptureMode mode;
    TargetIdentifier source;
    TargetIdentifier name;
};

struct TargetClosureParameter final {
    std::optional<TargetIdentifier> name;
    TargetTypeID type;
    bool maybe_unused;
};

struct TargetClosureExpr final {
    std::vector<TargetClosureCapture> captures;
    std::vector<TargetClosureParameter> parameters;
    TargetTypeID result;
    std::vector<TargetStmtID> body;
};

using TargetExprValue = std::variant<
    TargetNameExpr,
    TargetIntrinsicNameExpr,
    TargetLiteralExpr,
    TargetPrefixExpr,
    TargetBinaryExpr,
    TargetCallExpr,
    TargetArrayExpr,
    TargetConstructionExpr,
    TargetIndexExpr,
    TargetMemberExpr,
    TargetScopeMemberExpr,
    TargetStaticMemberExpr,
    TargetForwardExpr,
    TargetStaticCastExpr,
    TargetRawFragment,
    TargetLambdaExpr,
    TargetClosureExpr>;

struct TargetExpr final {
    TargetExprValue value;
};
