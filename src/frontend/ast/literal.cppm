module carven:frontend.ast.literal;

import :frontend.literal;
import :source.text;
import std;

using ASTLiteralValue = std::variant<
    IntegerLiteralValue,
    FloatingLiteralValue,
    StringLiteralValue,
    CharacterLiteralValue,
    BooleanLiteralValue>;

struct ASTLiteral final {
    Span span;
    ASTLiteralValue value;
};
