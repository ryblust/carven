module carven:frontend.ast.decl;

import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.type;
import :source.text;
import std;

struct ASTNamedBindingTarget final {
    Span name_span;
};

struct ASTDiscardBindingTarget final {
    Span underscore_span;
};

using ASTBindingTarget = std::variant<ASTNamedBindingTarget, ASTDiscardBindingTarget>;

auto binding_target_span(const ASTBindingTarget& target) noexcept -> Span;

struct ASTSingleImport final {
    Span name_span;
};

struct ASTWildcardImport final {};

struct ASTImportList final {
    std::vector<Span> names;
};

struct ASTImportSelection final {
    Span span;
    std::variant<ASTSingleImport, ASTWildcardImport, ASTImportList> value;
};

struct ASTDomainRootModuleReference final {
    std::vector<Span> components;
};

struct ASTParentRelativeModuleReference final {
    Span prefix_span;
    std::vector<Span> components;
};

struct ASTCraftQualifiedModuleReference final {
    Span name_span;
    Span separator_span;
    std::vector<Span> components;
};

using ASTModuleReferenceValue = std::variant<
    ASTDomainRootModuleReference,
    ASTParentRelativeModuleReference,
    ASTCraftQualifiedModuleReference>;

struct ASTModuleReference final {
    Span span;
    ASTModuleReferenceValue value;
};

struct ASTModuleImport final {
    Span span;
    ASTModuleReference module_reference;
    ASTImportSelection selection;
};

struct ASTPrivateDeclarationVisibility final {
    Span keyword_span;
};

struct ASTBareDeclarationVisibility final {};

struct ASTExportDeclarationVisibility final {
    Span keyword_span;
};

using ASTDeclarationVisibility = std::variant<
    ASTPrivateDeclarationVisibility,
    ASTBareDeclarationVisibility,
    ASTExportDeclarationVisibility>;

struct ASTEnumCase final {
    Span span;
    Span name_span;
    std::vector<ASTTypeID> payload_types;
    std::optional<ASTExprID> initializer;
};

struct ASTEnumDecl final {
    ASTDeclarationVisibility visibility;
    Span name_span;
    std::optional<ASTTypeID> underlying_type;
    std::vector<ASTEnumCase> cases;
};

struct ASTRecordField final {
    Span span;
    Span name_span;
    ASTTypeID type;
};

enum class ASTRecordKind { Struct, Class };

struct ASTRecordDecl final {
    ASTRecordKind kind;
    ASTDeclarationVisibility visibility;
    Span name_span;
    std::vector<ASTRecordField> fields;
    std::vector<ASTItemID> operations;
};

struct ASTFunctionParameter final {
    Span span;
    ASTAccessSyntax access;
    ASTBindingTarget target;
    std::optional<ASTTypeID> type;
};

struct ASTExpressionBody final {
    Span arrow_span;
    ASTExprID expression;
};

using ASTCallableBody = std::variant<ASTBlockID, ASTExpressionBody>;

struct ASTFunctionBody final {
    ASTCallableBody body;
};

using ASTFunctionImplementation = std::variant<ASTFunctionBody, ASTCppImportForm>;

struct ASTFunctionDecl final {
    bool is_implicit_entry;
    ASTDeclarationVisibility visibility;
    std::optional<ASTCppExportForm> cpp_export;
    std::optional<Span> const_span;
    Span name_span;
    std::vector<ASTFunctionParameter> parameters;
    std::optional<ASTTypeID> result_type;
    std::optional<ASTThrowClause> throw_clause;
    ASTFunctionImplementation implementation;
};

struct ASTConstantDecl final {
    ASTDeclarationVisibility visibility;
    Span name_span;
    std::optional<ASTTypeID> type;
    ASTExprID initializer;
};

struct ASTConstantBlock final {
    Span keyword_span;
    ASTBlockID body;
};

struct ASTTestDecl final {
    bool is_const;
    Span keyword_span;
    Span name_span;
    std::string name;
    ASTBlockID body;
};

struct ASTItem final {
    Span span;
    std::variant<
        ASTEnumDecl,
        ASTRecordDecl,
        ASTFunctionDecl,
        ASTConstantDecl,
        ASTConstantBlock,
        ASTTestDecl>
        value;
};
