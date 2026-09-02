module carven:frontend.ast.interop;

import :source.text;

enum class ASTCppHeaderDelimiter {
    AngleBrackets,
    Quotes,
};

struct ASTCppHeaderImport final {
    Span span;
    ASTCppHeaderDelimiter delimiter;
    Span name_span;
};

struct ASTCppExportForm final {
    Span span;
};

struct ASTCppImportForm final {
    Span span;
};

struct ASTCppSourceFragment final {
    Span form_span;
    Span payload_span;
};
