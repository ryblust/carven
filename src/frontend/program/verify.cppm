module carven:frontend.program.verify;

import :frontend.program;
import std;

enum class ParsedBatchErrorKind {
    InvalidProvenance,
    SyntaxTreeCountMismatch,
    SyntaxSourceMismatch,
    RootSpanOutOfBounds,
    ChildIDOutOfBounds,
    SpanOutOfBounds,
};

struct ParsedBatchError final {
    ParsedBatchErrorKind kind;
    std::string message;
};

auto verify_syntax_program(const ParsedBatch& program) noexcept
    -> std::expected<void, ParsedBatchError>;
