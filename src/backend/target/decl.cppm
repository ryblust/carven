module carven:backend.target.decl;

import :backend.target.ids;
import :backend.target.name;
import std;

struct TargetParameter final {
    std::optional<TargetIdentifier> name;
    TargetTypeID type;
    bool maybe_unused;
};

struct TargetFunctionDecl final {
    TargetName name;
    std::vector<TargetParameter> parameters;
    TargetTypeID result;
    std::vector<TargetStmtID> body;
    bool declaration_only;
    bool inline_specifier;
    bool constexpr_specifier;
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
    TargetExprID value;
};

struct TargetConstructorDecl final {
    TargetIdentifier name;
    std::vector<TargetIdentifier> template_type_parameters;
    std::vector<TargetParameter> parameters;
    std::vector<TargetMemberInitializer> initializers;
    bool constexpr_specifier;
    bool explicit_specifier;
    bool defaulted;
};

enum class TargetOperatorName {
    Assignment,
    Equality,
};

using TargetMemberFunctionName = std::variant<TargetIdentifier, TargetOperatorName>;

struct TargetMemberFunctionDecl final {
    TargetMemberFunctionName name;
    std::vector<TargetParameter> parameters;
    TargetTypeID result;
    std::vector<TargetStmtID> body;
    bool static_specifier;
    bool constexpr_specifier;
    bool friend_specifier;
    bool declaration_only;
    bool defaulted;
    bool result_reference;
    bool decltype_auto_result;
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
    TargetExprID value;
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

struct TargetVariableDecl final {
    TargetTypeID type;
    TargetName name;
    TargetExprID initializer;
    bool inline_specifier;
    bool constexpr_specifier;
};

using TargetDecl = std::variant<
    TargetFunctionDecl,
    TargetStructDecl,
    TargetStructForwardDecl,
    TargetEnumDecl,
    TargetEnumForwardDecl,
    TargetClassDecl,
    TargetClassForwardDecl,
    TargetVariableDecl>;
