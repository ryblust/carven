module carven:frontend.ast.control;

import :frontend.ast.ids;
import :source.text;
import std;

enum class ASTControlTransferKind {
    Return,
    Break,
    Continue,
    Throw,
    Rethrow,
};

struct ASTControlTransfer final {
    Span span;
    ASTControlTransferKind kind;
    Span keyword_span;
    std::optional<ASTExprID> value;
};

struct ASTIfForm final {
    Span span;
    struct Branch final {
        Span keyword_span;
        ASTExprID condition;
        ASTBranchBlockID body;
    };
    std::vector<Branch> branches;
    std::optional<ASTBranchBlockID> else_branch;
};

struct ASTMatchArmBody final {
    Span span;
    std::variant<ASTExprID, ASTControlTransfer, ASTBranchBlockID> value;
};

struct ASTGuard final {
    Span keyword_span;
    ASTExprID expression;
};

struct ASTMatchArm final {
    Span span;
    ASTPatternID pattern;
    std::optional<ASTGuard> guard;
    Span arrow_span;
    ASTMatchArmBody body;
};

struct ASTMatchForm final {
    Span span;
    Span keyword_span;
    ASTExprID subject;
    std::vector<ASTMatchArm> arms;
};

struct ASTCatchWildcardPattern final {
    Span underscore_span;
};

struct ASTCatchTypedPattern final {
    ASTTypeID type;
    Span left_parenthesis_span;
    ASTPatternID inner;
    Span right_parenthesis_span;
};

struct ASTCatchPatternAtom final {
    Span span;
    std::variant<ASTCatchWildcardPattern, ASTCatchTypedPattern> value;
};

struct ASTCatchPattern final {
    Span span;
    std::vector<ASTCatchPatternAtom> alternatives;
    std::vector<Span> pipe_spans;
};

struct ASTCatchArm final {
    Span span;
    ASTCatchPattern pattern;
    std::optional<ASTGuard> guard;
    Span arrow_span;
    ASTMatchArmBody body;
};

struct ASTTryForm final {
    Span span;
    Span try_span;
    ASTBranchBlockID body;
    Span catch_span;
    std::vector<ASTCatchArm> arms;
};
