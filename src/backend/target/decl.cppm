module carven:backend.target.decl;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.stmt;
import std;

struct TargetParameter final {
    std::optional<TargetIdentifier> name;
    TargetTypeID type;
    std::optional<TargetExpr> default_value = std::nullopt;
};

auto target_parameters(TargetParameter parameter) noexcept -> std::vector<TargetParameter>;
auto target_parameters(TargetParameter first, TargetParameter second) noexcept
    -> std::vector<TargetParameter>;

struct TargetFreeFunctionDeclaration final {};

struct TargetFreeFunctionDefinition final {
    std::vector<TargetStmt> body;
};

using TargetFreeFunctionForm =
    std::variant<TargetFreeFunctionDeclaration, TargetFreeFunctionDefinition>;

struct TargetFunctionDecl final {
    TargetName name;
    std::vector<TargetParameter> parameters;
    TargetTypeID result;
    TargetFreeFunctionForm form;
    bool constexpr_specifier = false;
    bool static_specifier = false;
    bool inline_specifier;
};

struct TargetStructField final {
    TargetIdentifier name;
    TargetTypeID type;
};

struct TargetMemberVariable final {
    TargetTypeID type;
    TargetIdentifier name;
    bool static_specifier;
    bool const_specifier;
};

struct TargetTypeAlias final {
    TargetIdentifier name;
    TargetTypeID type;
};

struct TargetMemberInitializer final {
    TargetIdentifier name;
    TargetExpr value;
};

struct TargetConstructorDecl final {
    TargetIdentifier name;
    std::vector<TargetParameter> parameters;
    std::vector<TargetMemberInitializer> initializers;
    bool constexpr_specifier;
    bool explicit_specifier;
};

enum class TargetOperatorName {
    Assignment,
    Equality,
    Call,
};

using TargetMemberFunctionName = std::variant<TargetIdentifier, TargetOperatorName>;

struct TargetMemberFunctionDeclaration final {};

struct TargetMemberFunctionDefaulted final {};

struct TargetMemberFunctionDefinition final {
    std::vector<TargetStmt> body;
};

using TargetMemberFunctionForm = std::variant<
    TargetMemberFunctionDeclaration,
    TargetMemberFunctionDefaulted,
    TargetMemberFunctionDefinition>;

struct TargetMemberFunctionDecl final {
    TargetMemberFunctionName name;
    std::vector<TargetParameter> parameters;
    TargetTypeID result;
    TargetMemberFunctionForm form;
    bool maybe_unused;
    bool static_specifier;
    bool constexpr_specifier;
    bool friend_specifier;
    bool result_reference;
    bool const_qualified;
};

struct TargetOutOfClassMemberDefinition final {
    TargetName owner;
    TargetMemberFunctionName name;
    std::vector<TargetParameter> parameters;
    TargetTypeID result;
    std::vector<TargetStmt> body;
    bool const_qualified;
};

using TargetRecordMember = std::variant<TargetStructField, TargetMemberFunctionDecl>;

struct TargetStructDecl final {
    TargetIdentifier name;
    std::vector<TargetRecordMember> members;
};

struct TargetStructForwardDecl final {
    TargetIdentifier name;
};

struct TargetEnumCase final {
    TargetIdentifier name;
    TargetExpr value;
};

struct TargetEnumDecl final {
    TargetIdentifier name;
    TargetTypeID underlying_type;
    std::vector<TargetEnumCase> cases;
};

struct TargetEnumForwardDecl final {
    TargetIdentifier name;
    TargetTypeID underlying_type;
};

struct TargetNestedRecord final {
    TargetIdentifier name;
    std::vector<TargetRecordMember> members;
};

using TargetClassMember = std::variant<
    TargetMemberVariable,
    TargetNestedRecord,
    TargetTypeAlias,
    TargetConstructorDecl,
    TargetMemberFunctionDecl>;

enum class TargetClassAccess {
    Public,
    Private,
};

struct TargetClassSection final {
    TargetClassAccess access;
    std::vector<TargetClassMember> members;
};

struct TargetClassDecl final {
    TargetIdentifier name;
    bool final_specifier;
    std::vector<TargetClassSection> sections;
};

struct TargetClassForwardDecl final {
    TargetIdentifier name;
};

using TargetDecl = std::variant<
    TargetFunctionDecl,
    TargetOutOfClassMemberDefinition,
    TargetStructDecl,
    TargetStructForwardDecl,
    TargetEnumDecl,
    TargetEnumForwardDecl,
    TargetClassDecl,
    TargetClassForwardDecl>;
