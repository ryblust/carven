module carven:frontend.ast.expr;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.region;
import :frontend.ast.type;
import :source.text;
import std;

enum class ASTPrefixOperator {
    LogicalNot,
    Negate,
    BitwiseNot,
};

enum class ASTBinaryOperator {
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

enum class ASTMemberOperator {
    Dot,
    Scope,
};

struct ASTNameExpr final {
    Span name_span;
};

struct ASTContextualCaseExpr final {
    Span dot_span;
    Span name_span;
};

struct ASTGroupExpr final {
    ASTExprID expression;
};

struct ASTArrayExpr final {
    std::vector<ASTExprID> element_ids;
};

struct ASTPositionalInitializerList final {
    Span span;
    std::vector<ASTExprID> values;
};

struct ASTFieldInitializer final {
    Span span;
    Span name_span;
    ASTExprID value;
};

struct ASTFieldInitializerList final {
    Span span;
    std::vector<ASTFieldInitializer> fields;
};

struct ASTConstructionInitializer final {
    std::variant<std::monostate, ASTPositionalInitializerList, ASTFieldInitializerList> value;
};

struct ASTConstructionExpr final {
    ASTConstructionType type;
    ASTConstructionInitializer initializer;
};

struct ASTPrefixExpr final {
    ASTPrefixOperator op;
    Span operator_span;
    ASTExprID operand_id;
};

struct ASTAccessExpr final {
    ASTAccessMode mode;
    Span marker_span;
    ASTExprID operand_id;
};

struct ASTBinaryExpr final {
    ASTExprID left;
    ASTBinaryOperator op;
    Span operator_span;
    ASTExprID right;
};

struct ASTCastExpr final {
    ASTExprID operand_id;
    Span operator_span;
    ASTTypeID target_type;
};

struct ASTCallArgument final {
    ASTExprID expression;
};

struct ASTCallExpr final {
    ASTExprID callee;
    std::vector<ASTCallArgument> arguments;
};

struct ASTIndexExpr final {
    ASTExprID operand_id;
    ASTExprID index;
};

struct ASTMemberExpr final {
    ASTExprID operand_id;
    ASTMemberOperator op;
    Span operator_span;
    Span name_span;
};

struct ASTLambdaCapture final {
    Span span;
    std::optional<Span> write_marker;
    Span name_span;
};

struct ASTLambdaExpr final {
    std::vector<ASTLambdaCapture> captures;
    std::vector<ASTFunctionParameter> parameters;
    std::optional<ASTTypeID> result_type;
    std::optional<ASTThrowClause> throw_clause;
    ASTBlockID body;
};

struct ASTPropagationExpr final {
    ASTExprID operand_id;
    Span operator_span;
};

struct ASTExpr final {
    Span span;
    std::variant<
        ASTLiteral,
        ASTNameExpr,
        ASTContextualCaseExpr,
        ASTGroupExpr,
        ASTArrayExpr,
        ASTConstructionExpr,
        ASTPrefixExpr,
        ASTAccessExpr,
        ASTBinaryExpr,
        ASTCastExpr,
        ASTCallExpr,
        ASTIndexExpr,
        ASTMemberExpr,
        ASTLambdaExpr,
        ASTPropagationExpr,
        ASTIfForm,
        ASTMatchForm,
        ASTTryForm,
        CppRegion>
        value;
};
