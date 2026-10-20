module carven:frontend.ast.imports;

import :source.text;
import std;

struct ASTImportItem final {
    Span name_span;
    std::optional<Span> alias_span;

    auto local_name_span() const noexcept -> Span;
};

struct ASTSingleImport final {
    ASTImportItem item;
};

struct ASTWildcardImport final {};

struct ASTImportList final {
    std::vector<ASTImportItem> names;
};

struct ASTImportSelection final {
    Span span;
    std::variant<ASTSingleImport, ASTWildcardImport, ASTImportList> value;
};
