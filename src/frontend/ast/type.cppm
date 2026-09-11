module carven:frontend.ast.type;

import :frontend.ast.ids;
import :source.text;
import std;

enum class ASTAccessMode {
    Read,
    Write,
    Take,
};

struct ASTAccessSyntax final {
    ASTAccessMode mode;
    std::optional<Span> marker;
};

struct ASTTypeNameComponent final {
    Span name_span;
};

struct ASTNamedType final {
    std::optional<Span> global_root;
    std::vector<ASTTypeNameComponent> components;
    std::vector<ASTTypeID> arguments;
};

struct ASTPointerType final {
    ASTTypeID target;
    ASTAccessSyntax access;
};

struct ASTArrayType final {
    ASTTypeID element_type;
    ASTExprID extent;
};

struct ASTSliceType final {
    ASTTypeID element_type;
};

struct ASTFunctionTypeParameter final {
    Span span;
    ASTAccessSyntax access;
    ASTTypeID type;
};

struct ASTThrowClause final {
    Span span;
    Span keyword_span;
    std::vector<ASTTypeID> failures;
    std::vector<Span> plus_spans;
};

struct ASTFunctionType final {
    std::vector<ASTFunctionTypeParameter> parameters;
    ASTTypeID result_type;
    std::optional<ASTThrowClause> throw_clause;
};

struct ASTType final {
    Span span;
    std::variant<ASTNamedType, ASTArrayType, ASTSliceType, ASTFunctionType, ASTPointerType> value;
};

struct ASTConstructionType final {
    Span span;
    std::variant<ASTNamedType, ASTFunctionType> value;
};
