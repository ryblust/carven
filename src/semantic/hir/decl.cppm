module carven:semantic.hir.decl;

import :semantic.hir.access;
import :semantic.hir.ids;
import :semantic.hir.place;
import :semantic.visibility;
import std;

struct HIRNamedBindingTarget final {
    SymbolID symbol;
};

struct HIRDiscardBindingTarget final {};

using HIRBindingTarget = std::variant<HIRNamedBindingTarget, HIRDiscardBindingTarget>;

struct HIRParameter final {
    HIRAccessMode access;
    HIRBindingTarget target;
    HIRTypeID type;
    ProgramOriginID origin;
};

struct HIRBody final {
    std::vector<HIRParameter> parameters;
    SemanticScopeID scope;
    HIRBlockID root;
};

enum class HIREntryPointKind {
    NoArguments,
    WithArguments,
};

struct HIRFunctionDecl final {
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ProgramSpellingID name;
    CallableID callable;
    HIRTypeID result;
    ProgramOriginID result_origin;
    SymbolID symbol;
    std::optional<HIREntryPointKind> entry_point;
};

struct HIRStructField final {
    ProgramSpellingID name;
    HIRTypeID type;
    ProgramOriginID origin;
};

struct HIRStructDecl final {
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ProgramSpellingID name;
    std::vector<HIRStructField> fields;
    SymbolID symbol;
    bool supports_equality;
};

enum class HIREnumProfile {
    Numeric,
    Payload,
};

struct HIREnumCase final {
    EnumID owner;
    ProgramSpellingID name;
    std::vector<HIRTypeID> payload_types;
    std::optional<HIRConstantID> constant;
    SymbolID symbol;
    ProgramOriginID origin;
};

struct HIREnumDecl final {
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ProgramSpellingID name;
    std::optional<HIRTypeID> underlying_type;
    std::vector<EnumCaseID> cases;
    HIREnumProfile profile;
    SymbolID symbol;
    bool supports_equality;
};

struct HIRTestDecl final {
    ProgramOriginID origin;
    ProgramSpellingID name;
    BodyID body;
};

struct HIRCppRegion final {
    ProgramOriginID origin;
    ProgramSpellingID bytes;
};

using HIRDeclarationRef = std::variant<FunctionID, StructID, EnumID>;
using HIRNominalDeclRef = std::variant<StructID, EnumID>;
using HIRModuleItem = std::variant<FunctionID, StructID, EnumID, TestID, HIRCppRegion>;
