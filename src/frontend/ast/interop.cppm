module carven:frontend.ast.interop;

import :source.text;
import std;

enum class ASTCppHeaderDelimiter {
    AngleBrackets,
    Quotes,
};

struct ASTCppUsing final {
    Span span;
    std::vector<Span> components;
    bool opens_namespace;
};

struct ASTCppHeaderImport final {
    Span span;
    ASTCppHeaderDelimiter delimiter;
    Span name_span;
    std::vector<ASTCppUsing> bindings;
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
