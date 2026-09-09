module carven:frontend.ast.literal;

import :frontend.literal;
import :source.text;
import std;

using ASTLiteralValue = std::variant<
    IntegerLiteralValue,
    FloatingLiteralValue,
    StringLiteralValue,
    CStringLiteralValue,
    CharacterLiteralValue,
    BooleanLiteralValue,
    NullPointerLiteralValue>;

struct ASTLiteral final {
    Span span;
    ASTLiteralValue value;
};
