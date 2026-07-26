module carven:frontend.ast.pattern;

import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.type;
import :frontend.literal;
import :source.text;
import std;

struct ASTWildcardPattern final {
    Span underscore_span;
};

struct ASTNegativeNumberPattern final {
    Span minus_span;
    Span number_span;
    NumericLiteralValue value;
};

struct ASTBindingPattern final {
    Span name_span;
};

struct ASTQualifiedName final {
    Span span;
    std::vector<Span> components;
};

struct ASTConstraintOperand final {
    Span span;
    std::variant<ASTQualifiedName, ASTArrayType> value;
};

struct ASTConstraintPattern final {
    Span is_span;
    ASTConstraintOperand operand;
};

struct ASTContextualCaseQualifier final {
    Span dot_span;
};

struct ASTQualifiedCaseQualifier final {
    Span span;
    std::vector<Span> components;
    Span separator_span;
};

using ASTCaseQualifier = std::variant<ASTContextualCaseQualifier, ASTQualifiedCaseQualifier>;

struct ASTCasePayload final {
    Span left_parenthesis_span;
    std::vector<ASTPatternID> patterns;
    Span right_parenthesis_span;
};

struct ASTCasePattern final {
    ASTCaseQualifier qualifier;
    Span name_span;
    std::optional<ASTCasePayload> payload;
};

struct ASTOrPattern final {
    std::vector<ASTPatternID> alternatives;
    std::vector<Span> pipe_spans;
};

struct ASTPattern final {
    Span span;
    std::variant<
        ASTWildcardPattern,
        ASTLiteral,
        ASTNegativeNumberPattern,
        ASTBindingPattern,
        ASTConstraintPattern,
        ASTCasePattern,
        ASTOrPattern>
        value;
};
