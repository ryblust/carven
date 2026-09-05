module carven:frontend.program.verify;

import :frontend.program;
import std;

enum class SyntaxProgramErrorKind {
    InvalidProvenance,
    SyntaxTreeCountMismatch,
    SyntaxSourceMismatch,
    InvalidReference,
    InvalidRootOwnership,
    InvalidResolvedImportGraph,
};

struct SyntaxProgramError final {
    SyntaxProgramErrorKind kind;
    std::string message;
};

auto verify_syntax_program(const SyntaxProgram& program) noexcept
    -> std::expected<void, SyntaxProgramError>;
