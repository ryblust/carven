module carven:frontend.ast.interop;

import :source.text;
import std;

enum class ASTCppHeaderDelimiter {
    AngleBrackets,
    Quotes,
};

struct ASTCppSingleSelection final {
    Span name;
};

struct ASTCppListSelection final {
    std::vector<Span> names;
};

struct ASTCppNamespaceSelection final {
    Span star;
};

struct ASTCppUsing final {
    Span span;
    std::vector<Span> prefix;
    std::variant<ASTCppSingleSelection, ASTCppListSelection, ASTCppNamespaceSelection> selection;
};

struct ASTCppHeaderImport final {
    Span span;
    ASTCppHeaderDelimiter delimiter;
    Span name_span;
    std::optional<ASTCppUsing> using_clause;
};

struct ASTCppExportForm final {
    Span span;
};

struct ASTCppFunctionTarget final {
    Span span;
    std::vector<Span> components;
};

struct ASTCppImportForm final {
    Span span;
    std::optional<ASTCppFunctionTarget> target;
};

struct ASTCppSourceFragment final {
    Span form_span;
    Span payload_span;
};
