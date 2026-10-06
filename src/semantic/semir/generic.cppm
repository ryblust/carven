module carven:semantic.semir.generic;

import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.type;
import std;

// Parameter identities remain rigid while a definition is checked.
struct GenericTypeParameter final {
    GenericDeclarationID definition;
    std::uint32_t index;
    auto operator<=>(const GenericTypeParameter&) const noexcept = default;
};

struct GenericArrayType final {
    GenericTypeID element;
    std::uint64_t extent;
    auto operator<=>(const GenericArrayType&) const noexcept = default;
};

struct GenericSliceType final {
    GenericTypeID element;
    auto operator<=>(const GenericSliceType&) const noexcept = default;
};

struct GenericPointerType final {
    GenericTypeID target;
    PointerAccess access;
    auto operator<=>(const GenericPointerType&) const noexcept = default;
};

struct GenericNominalApplication final {
    GenericDeclarationID definition;
    std::vector<GenericTypeID> arguments;
    auto operator<=>(const GenericNominalApplication&) const noexcept = default;
};

using GenericTypeExpression = std::variant<
    TypeID,
    GenericTypeParameter,
    GenericArrayType,
    GenericSliceType,
    GenericPointerType,
    GenericNominalApplication>;

struct GenericAudienceDependency final {
    std::variant<TypeID, GenericDeclarationID> reference;
    ProgramOriginID origin;
};

struct GenericDeclarationContract final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    std::vector<ProgramSpellingID> parameters;
    std::vector<GenericAudienceDependency> audience_dependencies;
};

struct GenericField final {
    ProgramSpellingID name;
    GenericTypeID type;
    ProgramOriginID origin;
};

struct GenericRecordDefinition final {
    GenericDeclarationContract contract;
    RecordKind kind;
    std::vector<GenericField> fields;
};

struct GenericEnumCase final {
    ProgramSpellingID name;
    ProgramOriginID origin;
    std::vector<GenericTypeID> payload_types;
};

struct GenericEnumDefinition final {
    GenericDeclarationContract contract;
    std::vector<GenericEnumCase> cases;
};

using GenericNominalDefinition = std::variant<GenericRecordDefinition, GenericEnumDefinition>;

// Published instances refer to ordinary concrete declarations. Their checked
// parameterized fields are construction state and never reach lowering.
struct GenericNominalInstance final {
    GenericDeclarationID definition;
    std::vector<TypeID> arguments;
    NominalDeclarationRef declaration;
};
