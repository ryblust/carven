module carven:semantic.semir.constant_access;

import :semantic.semir.constant;
import :semantic.semir.generic;
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

auto constant_has_storage(const ConstantValueReader& values, ConstantID id) noexcept -> bool;

struct EnumCaseTypes final {
    EnumCaseID id;
    std::vector<TypeID> payload_types;
};

struct ExecutionDisplayNames final {
    std::string name;
    bool is_class;
    std::vector<std::string> fields;
    std::vector<std::pair<EnumCaseID, std::string>> cases;
};

class ExecutionValueAccess : public ConstantValueReader {
public:
    virtual auto generic_type_copy(GenericTypeID type) const noexcept -> GenericTypeExpression = 0;
    virtual auto construction_type_copy(TypeTermID type) const noexcept -> ConstructionType = 0;

    virtual auto generic_member_types(GenericTypeID type) const noexcept
        -> std::optional<std::vector<ConstructionTypeRef>> = 0;

    virtual auto callable_signature_copy(CallableSignatureID signature) const noexcept
        -> CallableSignature = 0;

    virtual auto failure_set_copy(FailureSetID failures) const noexcept -> FailureSet = 0;
    virtual auto display_names(TypeID type) const noexcept -> ExecutionDisplayNames = 0;
    virtual auto read_borrows_storage(ConstructionTypeRef type) const noexcept -> bool = 0;

    virtual auto struct_field_types(StructID structure) const noexcept
        -> std::optional<std::vector<TypeID>> = 0;

    virtual auto enum_case_types(EnumID enumeration) const noexcept
        -> std::optional<std::vector<EnumCaseTypes>> = 0;
};

class SemIRProgram;

// The published program outlives this view and all returned borrows.
class PublishedConstantValues final : public ExecutionValueAccess {
public:
    explicit PublishedConstantValues(const SemIRProgram& program) noexcept;
    auto generic_type_copy(GenericTypeID type) const noexcept -> GenericTypeExpression override;
    auto construction_type_copy(TypeTermID type) const noexcept -> ConstructionType override;

    auto generic_member_types(GenericTypeID type) const noexcept
        -> std::optional<std::vector<ConstructionTypeRef>> override;

    auto callable_signature_copy(CallableSignatureID signature) const noexcept
        -> CallableSignature override;

    auto failure_set_copy(FailureSetID failures) const noexcept -> FailureSet override;
    auto display_names(TypeID type) const noexcept -> ExecutionDisplayNames override;
    auto builtin_type(BuiltinType type) const noexcept -> TypeID override;
    auto read_borrows_storage(ConstructionTypeRef type) const noexcept -> bool override;

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
};

// Execution reads the existing construction shapes without completing failure inference.
auto execution_type_reference(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> ConstructionTypeRef;

template<typename Visitor>
auto visit_execution_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    // Borrow the visitor as an lvalue, including through nested variant visits.
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    Visitor&& visitor
) noexcept -> decltype(visitor(BuiltinTypeValue {.kind = BuiltinType::Void})) {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        return values.type_copy(*concrete).value.visit(visitor);
    }
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        return values.construction_type_copy(*term).value.visit(visitor);
    }
    return values.generic_type_copy(std::get<GenericTypeID>(type))
        .visit(
            [&](
                const auto& shape
            ) noexcept -> decltype(visitor(BuiltinTypeValue {.kind = BuiltinType::Void})) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(shape)>, TypeID>) {
                    return values.type_copy(shape).value.visit(visitor);
                } else {
                    return visitor(shape);
                }
            }
        );
}
