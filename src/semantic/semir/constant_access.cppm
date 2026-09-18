module carven:semantic.semir.constant_access;

import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.identity;
import :semantic.semir.type;
import std;

// Canonical values and spellings are immutable. Their borrows survive appends
// and end when their owner is moved or sealed. Type construction still returns copies.
class ConstantValueReader {
public:
    virtual ~ConstantValueReader() = default;
    virtual auto identity() const noexcept -> ProgramIdentity = 0;
    virtual auto owns(ProgramSpellingID id) const noexcept -> bool = 0;
    virtual auto builtin_type(BuiltinType type) const noexcept -> TypeID = 0;
    virtual auto type_copy(TypeID type) const noexcept -> CanonicalType = 0;
    virtual auto constant(ConstantID constant) const noexcept -> const ConstantFact& = 0;
    virtual auto spelling(ProgramSpellingID spelling) const noexcept -> std::string_view = 0;
};

struct EnumCaseTypes final {
    EnumCaseID id;
    std::vector<TypeID> payload_types;
};

struct ExecutionDisplayNames final {
    std::string name;
    std::vector<std::string> fields;
    std::vector<std::pair<EnumCaseID, std::string>> cases;
};

class ExecutionValueAccess : public ConstantValueReader {
public:
    virtual auto display_names(TypeID type) const noexcept -> ExecutionDisplayNames = 0;
    virtual auto read_borrows_storage(TypeID type) const noexcept -> bool = 0;
    virtual auto struct_field_types(StructID structure) const noexcept
        -> std::optional<std::vector<TypeID>> = 0;
    virtual auto enum_case_types(EnumID enumeration) const noexcept
        -> std::optional<std::vector<EnumCaseTypes>> = 0;
};

class ConstantValueAccess : public ExecutionValueAccess {
public:
    virtual auto intern_constant(ConstantFact fact) noexcept -> ConstantID = 0;
    virtual auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID = 0;
};

class SemIRProgram;

// The published program outlives this view and all returned borrows.
class PublishedConstantValues final : public ExecutionValueAccess {
public:
    explicit PublishedConstantValues(const SemIRProgram& program) noexcept;
    auto display_names(TypeID type) const noexcept -> ExecutionDisplayNames override;
    auto builtin_type(BuiltinType type) const noexcept -> TypeID override;
    auto read_borrows_storage(TypeID type) const noexcept -> bool override;
    auto struct_field_types(StructID structure) const noexcept
        -> std::optional<std::vector<TypeID>> override;
    auto enum_case_types(EnumID enumeration) const noexcept
        -> std::optional<std::vector<EnumCaseTypes>> override;
    auto identity() const noexcept -> ProgramIdentity override;
    auto owns(ProgramSpellingID id) const noexcept -> bool override;
    auto type_copy(TypeID type) const noexcept -> CanonicalType override;
    auto constant(ConstantID constant) const noexcept -> const ConstantFact& override;
    auto spelling(ProgramSpellingID spelling) const noexcept -> std::string_view override;

private:
    const SemIRProgram& program;
    mutable std::optional<std::vector<TypeContents>> contents;
};
