module carven:semantic.semir.constant_access;

import :semantic.semir.constant;
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
    virtual auto type_copy(TypeID type) const noexcept -> CanonicalType = 0;
    virtual auto constant(ConstantID constant) const noexcept -> const ConstantFact& = 0;
    virtual auto spelling(ProgramSpellingID spelling) const noexcept -> std::string_view = 0;
};

class ConstantValueAccess : public ConstantValueReader {
public:
    virtual auto read_borrows_storage(TypeID type) const noexcept -> bool = 0;
    virtual auto struct_field_types(StructID structure) const noexcept
        -> std::optional<std::vector<TypeID>> = 0;
    virtual auto intern_builtin_type(BuiltinType type) noexcept -> TypeID = 0;
    virtual auto intern_constant(ConstantFact fact) noexcept -> ConstantID = 0;
    virtual auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID = 0;
};

class SemIRProgram;

// The published program outlives this view and all returned borrows.
class PublishedConstantValues final : public ConstantValueReader {
public:
    explicit PublishedConstantValues(const SemIRProgram& program) noexcept;
    auto identity() const noexcept -> ProgramIdentity override;
    auto owns(ProgramSpellingID id) const noexcept -> bool override;
    auto type_copy(TypeID type) const noexcept -> CanonicalType override;
    auto constant(ConstantID constant) const noexcept -> const ConstantFact& override;
    auto spelling(ProgramSpellingID spelling) const noexcept -> std::string_view override;

private:
    const SemIRProgram& program;
};
