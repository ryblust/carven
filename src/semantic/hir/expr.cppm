module carven:semantic.hir.expr;

import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.ids;
import :semantic.hir.place;
import std;

struct HIRIntegerLiteralValue final {
    bool negative;
    std::uint64_t magnitude;
};

struct HIRF32LiteralValue final {
    float value;
};

struct HIRF64LiteralValue final {
    double value;
};

struct HIRBooleanLiteralValue final {
    bool value;
};

struct HIRCharacterLiteralValue final {
    char32_t scalar;
};

struct HIRStrLiteralValue final {
    ProgramSpellingID bytes;
};

using HIRLiteralValue = std::variant<
    HIRIntegerLiteralValue,
    HIRF32LiteralValue,
    HIRF64LiteralValue,
    HIRBooleanLiteralValue,
    HIRCharacterLiteralValue,
    HIRStrLiteralValue>;

struct HIRLiteralExpr final {
    HIRLiteralValue value;
};

struct HIRNameExpr final {
    SymbolID symbol;
};

struct HIRArrayExpr final {
    std::vector<HIRExprID> element_ids;
};

struct HIRFieldInitializer final {
    HIRExprID value;
    std::uint32_t declaration_index;
};

struct HIRConstructionExpr final {
    std::vector<HIRFieldInitializer> fields;
};

struct HIRCaseConstructionExpr final {
    EnumCaseID enum_case;
    std::vector<HIRExprID> payload;
};

struct HIRUnaryExpr final {
    enum class Operator {
        LogicalNot,
        Negate,
        BitwiseNot,
    };
    Operator op;
    HIRExprID operand_id;
};

struct HIRBinaryExpr final {
    enum class Operator {
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
    HIRExprID left;
    Operator op;
    HIRExprID right;
};

enum class HIRCastKind {
    Identity,
    IntegerToInteger,
    IntegerToBool,
    BoolToInteger,
    IntegerToFloating,
    FloatingWiden,
    EnumToInteger,
};

struct HIRCastExpr final {
    HIRExprID operand_id;
    HIRCastKind kind;
};

struct HIRCallArgument final {
    HIRAccessMode access;
    HIRExprID expression;
};

struct HIRCallExpr final {
    HIRExprID callee;
    std::vector<HIRCallArgument> arguments;
};

enum class HIRCaptureMode {
    Value,
    Write,
};

struct HIRLambdaCapture final {
    HIRCaptureMode mode;
    SymbolID source;
    SymbolID local;
    HIRTypeID type;
    ProgramOriginID origin;
};

struct HIRClosureExpr final {
    std::vector<HIRLambdaCapture> captures;
    HIRTypeID result;
    CallableID callable;
};

struct HIRCallableViewExpr final {
    HIRExprID source;
};

struct HIRPropagationExpr final {
    HIRExprID operand_id;
};

struct HIRTakeExpr final {
    HIRExprID operand_id;
    ProgramOriginID marker_origin;
};

enum class HIRTextIntrinsic {
    Len,
    IsEmpty,
    Bytes,
    Chars,
};

struct HIRTextIntrinsicExpr final {
    HIRExprID operand_id;
    HIRTextIntrinsic intrinsic;
};

struct HIRIndexExpr final {
    HIRExprID operand_id;
    HIRExprID index;
};

struct HIRUnresolvedMemberTarget final {
    ProgramSpellingID name;
    bool scope;
};

struct HIRStructFieldTarget final {
    StructID owner;
    std::uint32_t index;
};

struct HIREnumCaseTarget final {
    EnumCaseID enum_case;
};

using HIRMemberTarget =
    std::variant<HIRUnresolvedMemberTarget, HIRStructFieldTarget, HIREnumCaseTarget>;

struct HIRMemberExpr final {
    HIRExprID operand_id;
    HIRMemberTarget target;
};

struct HIRConditionalBranch final {
    HIRExprID condition;
    HIRBlockID body;
};

struct HIRIfExpr final {
    std::vector<HIRConditionalBranch> branches;
    std::optional<HIRBlockID> else_branch;
};

struct HIRMatchArm final {
    SemanticScopeID scope;
    HIRPatternID pattern;
    std::optional<HIRExprID> guard;
    HIRBlockID body;
};

struct HIRMatchExpr final {
    HIRExprID subject;
    std::vector<HIRMatchArm> arms;
    bool exhaustive;
};

struct HIRCatchPatternAlternative final {
    std::optional<HIRTypeID> type;
    std::optional<HIRPatternID> inner;
};

struct HIRCatchArm final {
    SemanticScopeID scope;
    std::vector<HIRCatchPatternAlternative> alternatives;
    std::optional<HIRExprID> guard;
    HIRBlockID body;
};

struct HIRTryExpr final {
    HIRBlockID body;
    std::vector<HIRCatchArm> arms;
};

struct HIRCppExpr final {
    ProgramSpellingID bytes;
};

using HIRExprValue = std::variant<
    HIRLiteralExpr,
    HIRNameExpr,
    HIRArrayExpr,
    HIRConstructionExpr,
    HIRCaseConstructionExpr,
    HIRUnaryExpr,
    HIRBinaryExpr,
    HIRCastExpr,
    HIRCallExpr,
    HIRClosureExpr,
    HIRCallableViewExpr,
    HIRPropagationExpr,
    HIRTakeExpr,
    HIRTextIntrinsicExpr,
    HIRIndexExpr,
    HIRMemberExpr,
    HIRIfExpr,
    HIRMatchExpr,
    HIRTryExpr,
    HIRCppExpr>;

struct HIRExpr final {
    ProgramOriginID origin;
    HIRTypeID type;
    std::optional<HIRConstantID> constant;
    HIRExprValue value;
};

struct HIRCatchFacts final {
    FailureSetID accepted_failure_set;
};

struct HIRTryFacts final {
    std::vector<HIRCatchFacts> arms;
    FailureSetID unhandled_failure_set;
};

struct HIRExpressionFacts final {
    FailureSetID pending_failure_set;
    FailureSetID outward_failure_set;
    FailureSetID evaluation_failure_set;
    bool exits_test;
    std::optional<SemanticPlaceUse> place_use;
    EvaluationEffect evaluation_effect;
    std::optional<HIRTryFacts> attempt;
};
