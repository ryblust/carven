module carven:semantic.semir.type;

import :semantic.semir.delegation;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.table;
import :support.invariant;
import std;

enum class BuiltinType {
    Bool,
    Char,
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Isize,
    Usize,
    F32,
    F64,
    String,
    Str,
    StrCharsView,
    Void,
    EntryArgs,
};

auto builtin_is_integer(BuiltinType type) noexcept -> bool;
auto builtin_is_signed_integer(BuiltinType type) noexcept -> bool;
auto builtin_is_numeric(BuiltinType type) noexcept -> bool;
auto builtin_integer_width(BuiltinType type) noexcept -> std::optional<std::uint8_t>;

struct BuiltinTypeValue final {
    BuiltinType kind;
    constexpr auto operator==(const BuiltinTypeValue&) const noexcept -> bool = default;
};

struct StructTypeValue final {
    StructID structure;
    constexpr auto operator==(const StructTypeValue&) const noexcept -> bool = default;
};

struct EnumTypeValue final {
    EnumID enumeration;
    constexpr auto operator==(const EnumTypeValue&) const noexcept -> bool = default;
};

enum class PointerAccess { Read, Write };

struct PointerTypeValue final {
    TypeID target;
    PointerAccess access;
    constexpr auto operator==(const PointerTypeValue&) const noexcept -> bool = default;
};

struct ArrayTypeValue final {
    TypeID element;
    std::uint64_t extent;
    constexpr auto operator==(const ArrayTypeValue&) const noexcept -> bool = default;
};

struct SliceTypeValue final {
    TypeID element;
    constexpr auto operator==(const SliceTypeValue&) const noexcept -> bool = default;
};

struct FunctionTypeValue final {
    CallableID callable;
    constexpr auto operator==(const FunctionTypeValue&) const noexcept -> bool = default;
};

struct ClosureTypeValue final {
    CallableID callable;
    constexpr auto operator==(const ClosureTypeValue&) const noexcept -> bool = default;
};

struct CallableViewTypeValue final {
    CallableSignatureID signature;
    constexpr auto operator==(const CallableViewTypeValue&) const noexcept -> bool = default;
};

using CanonicalTypeValue = std::variant<
    BuiltinTypeValue,
    StructTypeValue,
    EnumTypeValue,
    ArrayTypeValue,
    SliceTypeValue,
    PointerTypeValue,
    FunctionTypeValue,
    ClosureTypeValue,
    CallableViewTypeValue,
    CppTypeValue>;

struct CanonicalType final {
    CanonicalTypeValue value;
    auto operator==(const CanonicalType&) const noexcept -> bool = default;
};

struct FailureSet final {
    std::vector<TypeID> members;
    auto operator==(const FailureSet&) const noexcept -> bool = default;
};

struct CallableParameter final {
    AccessMode access;
    TypeID type;
    constexpr auto operator==(const CallableParameter&) const noexcept -> bool = default;
};

struct CallableSignature final {
    std::vector<CallableParameter> parameters;
    TypeID result;
    FailureSetID failures;
    auto operator==(const CallableSignature&) const noexcept -> bool = default;
};

using ConstructionTypeRef = std::variant<TypeID, TypeTermID>;

struct ConstructionCallableParameter final {
    AccessMode access;
    ConstructionTypeRef type;
};

struct ConstructionArrayTypeValue final {
    ConstructionTypeRef element;
    std::uint64_t extent;
};

struct ConstructionSliceTypeValue final {
    ConstructionTypeRef element;
};

struct ConstructionCallableViewTypeValue final {
    std::vector<ConstructionCallableParameter> parameters;
    ConstructionTypeRef result;
    FailureTermID failures;
};

using ConstructionTypeValue = std::variant<
    ConstructionArrayTypeValue,
    ConstructionSliceTypeValue,
    ConstructionCallableViewTypeValue>;

struct ConstructionType final {
    ConstructionTypeValue value;
};

template<typename Reader>
concept FailureResolutionReader = requires (const Reader& reader, FailureTermID term) {
    { reader.owner() } noexcept -> std::same_as<ProgramIdentity>;
    { reader.contains(term) } noexcept -> std::same_as<bool>;
    { reader.failure_set(term) } noexcept -> std::same_as<FailureSetID>;
};

class TypeResolution final {
public:
    TypeResolution(const TypeResolution&) = delete;
    TypeResolution(TypeResolution&&) = default;
    ~TypeResolution() = default;
    auto operator=(const TypeResolution&) -> TypeResolution& = delete;
    auto operator=(TypeResolution&&) -> TypeResolution& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(TypeTermID term) const noexcept -> bool;
    auto type(TypeTermID term) const noexcept -> TypeID;
    auto resolve(ConstructionTypeRef reference) const noexcept -> TypeID;

private:
    TypeResolution(ProgramIdentity identity, std::vector<TypeID> types) noexcept;

    ProgramIdentity program_identity;
    std::vector<TypeID> resolved_types;

    friend class ConstructionTypeStore;
};

class CanonicalTypeStore final {
public:
    CanonicalTypeStore(const CanonicalTypeStore&) = delete;
    CanonicalTypeStore(CanonicalTypeStore&&) = default;
    ~CanonicalTypeStore() = default;
    auto operator=(const CanonicalTypeStore&) -> CanonicalTypeStore& = delete;
    auto operator=(CanonicalTypeStore&&) -> CanonicalTypeStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(TypeID id) const noexcept -> bool;
    auto type(TypeID id) const noexcept -> const CanonicalType&;
    auto entries() const noexcept -> IDTableEntries<TypeID, CanonicalType, ProgramIdentity>;
    auto size() const noexcept -> std::size_t;

private:
    explicit CanonicalTypeStore(ImmutableProgramTable<CanonicalType, TypeID> rows) noexcept;

    ImmutableProgramTable<CanonicalType, TypeID> rows;

    friend class CanonicalTypeStoreBuilder;
};

class CallableSignatureStoreBuilder;

class CanonicalTypeStoreBuilder final {
public:
    explicit CanonicalTypeStoreBuilder(ProgramIdentity owner) noexcept;
    CanonicalTypeStoreBuilder(const CanonicalTypeStoreBuilder&) = delete;
    CanonicalTypeStoreBuilder(CanonicalTypeStoreBuilder&&) = default;
    ~CanonicalTypeStoreBuilder() = default;
    auto operator=(const CanonicalTypeStoreBuilder&) -> CanonicalTypeStoreBuilder& = delete;
    auto operator=(CanonicalTypeStoreBuilder&&) -> CanonicalTypeStoreBuilder& = delete;
    auto intern(const CanonicalType& type) noexcept -> TypeID;
    auto intern_builtin(BuiltinType type) noexcept -> TypeID;
    auto copy(TypeID id) const noexcept -> CanonicalType;
    auto owner() const noexcept -> ProgramIdentity;
    auto seal() && noexcept -> CanonicalTypeStore;

private:
    auto intern_row(const CanonicalType& type) noexcept -> TypeID;
    auto intern_resolved_callable_view(
        CallableSignatureID signature,
        const CallableSignatureStoreBuilder& signatures
    ) noexcept -> TypeID;

    MutableProgramTable<CanonicalType, TypeID> rows;
    std::unordered_multimap<std::size_t, TypeID> candidates;

    friend class ConstructionTypeStore;
};

class FailureSetStore final {
public:
    FailureSetStore(const FailureSetStore&) = delete;
    FailureSetStore(FailureSetStore&&) = default;
    ~FailureSetStore() = default;
    auto operator=(const FailureSetStore&) -> FailureSetStore& = delete;
    auto operator=(FailureSetStore&&) -> FailureSetStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(FailureSetID id) const noexcept -> bool;
    auto failure_set(FailureSetID id) const noexcept -> const FailureSet&;
    auto entries() const noexcept -> IDTableEntries<FailureSetID, FailureSet, ProgramIdentity>;
    auto size() const noexcept -> std::size_t;

private:
    explicit FailureSetStore(ImmutableProgramTable<FailureSet, FailureSetID> rows) noexcept;

    ImmutableProgramTable<FailureSet, FailureSetID> rows;

    friend class FailureSetStoreBuilder;
};

class FailureSetStoreBuilder final {
public:
    explicit FailureSetStoreBuilder(ProgramIdentity owner) noexcept;
    FailureSetStoreBuilder(const FailureSetStoreBuilder&) = delete;
    FailureSetStoreBuilder(FailureSetStoreBuilder&&) = default;
    ~FailureSetStoreBuilder() = default;
    auto operator=(const FailureSetStoreBuilder&) -> FailureSetStoreBuilder& = delete;
    auto operator=(FailureSetStoreBuilder&&) -> FailureSetStoreBuilder& = delete;
    auto intern(std::vector<TypeID> members) noexcept -> FailureSetID;
    auto empty_set() noexcept -> FailureSetID;
    auto copy(FailureSetID id) const noexcept -> FailureSet;
    auto owner() const noexcept -> ProgramIdentity;
    auto seal() && noexcept -> FailureSetStore;

private:
    MutableProgramTable<FailureSet, FailureSetID> rows;
};

class CallableSignatureStore final {
public:
    CallableSignatureStore(const CallableSignatureStore&) = delete;
    CallableSignatureStore(CallableSignatureStore&&) = default;
    ~CallableSignatureStore() = default;
    auto operator=(const CallableSignatureStore&) -> CallableSignatureStore& = delete;
    auto operator=(CallableSignatureStore&&) -> CallableSignatureStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(CallableSignatureID id) const noexcept -> bool;
    auto signature(CallableSignatureID id) const noexcept -> const CallableSignature&;
    auto entries() const noexcept
        -> IDTableEntries<CallableSignatureID, CallableSignature, ProgramIdentity>;
    auto size() const noexcept -> std::size_t;

private:
    explicit CallableSignatureStore(
        ImmutableProgramTable<CallableSignature, CallableSignatureID> rows
    ) noexcept;

    ImmutableProgramTable<CallableSignature, CallableSignatureID> rows;

    friend class CallableSignatureStoreBuilder;
};

class CallableSignatureStoreBuilder final {
public:
    explicit CallableSignatureStoreBuilder(ProgramIdentity owner) noexcept;
    CallableSignatureStoreBuilder(const CallableSignatureStoreBuilder&) = delete;
    CallableSignatureStoreBuilder(CallableSignatureStoreBuilder&&) = default;
    ~CallableSignatureStoreBuilder() = default;
    auto operator=(const CallableSignatureStoreBuilder&) -> CallableSignatureStoreBuilder& = delete;
    auto operator=(CallableSignatureStoreBuilder&&) -> CallableSignatureStoreBuilder& = delete;
    auto intern(CallableSignature signature) noexcept -> CallableSignatureID;
    auto copy(CallableSignatureID id) const noexcept -> CallableSignature;
    auto owner() const noexcept -> ProgramIdentity;
    auto seal() && noexcept -> CallableSignatureStore;

private:
    MutableProgramTable<CallableSignature, CallableSignatureID> rows;
};

class ConstructionTypeStore final {
public:
    explicit ConstructionTypeStore(ProgramIdentity owner) noexcept;
    ConstructionTypeStore(const ConstructionTypeStore&) = delete;
    ConstructionTypeStore(ConstructionTypeStore&&) = default;
    ~ConstructionTypeStore() = default;
    auto operator=(const ConstructionTypeStore&) -> ConstructionTypeStore& = delete;
    auto operator=(ConstructionTypeStore&&) -> ConstructionTypeStore& = delete;
    auto append(ConstructionType type) noexcept -> TypeTermID;
    auto copy(TypeTermID id) const noexcept -> ConstructionType;
    auto owner() const noexcept -> ProgramIdentity;
    auto size() const noexcept -> std::size_t;

    template<typename TypeResolver, typename FailureResolver>
    static auto canonicalize_type(
        const ConstructionType& type,
        CanonicalTypeStoreBuilder& types,
        CallableSignatureStoreBuilder& signatures,
        TypeResolver resolve_ref,
        FailureResolver resolve_failure
    ) noexcept -> TypeID {
        return std::visit(
            [&](const auto& value) noexcept -> TypeID {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, ConstructionArrayTypeValue>) {
                    return types.intern(
                        CanonicalType {
                            .value = ArrayTypeValue {
                                .element = resolve_ref(value.element),
                                .extent = value.extent,
                            },
                        }
                    );
                } else if constexpr (std::same_as<Value, ConstructionSliceTypeValue>) {
                    return types.intern(
                        {.value = SliceTypeValue {.element = resolve_ref(value.element)}}
                    );
                } else if constexpr (std::same_as<Value, ConstructionCallableViewTypeValue>) {
                    auto parameters = std::vector<CallableParameter>();
                    parameters.reserve(value.parameters.size());
                    for (const auto& parameter : value.parameters) {
                        parameters.push_back(
                            CallableParameter {
                                .access = parameter.access,
                                .type = resolve_ref(parameter.type),
                            }
                        );
                    }
                    const auto signature = signatures.intern(
                        CallableSignature {
                            .parameters = std::move(parameters),
                            .result = resolve_ref(value.result),
                            .failures = resolve_failure(value.failures),
                        }
                    );
                    return types.intern_resolved_callable_view(signature, signatures);
                } else {
                    static_assert(std::same_as<Value, void>, "unhandled construction type shape");
                }
            },
            type.value
        );
    }

    template<FailureResolutionReader FailureReader>
    auto canonicalize(
        const FailureReader& failures,
        CanonicalTypeStoreBuilder& types,
        CallableSignatureStoreBuilder& signatures
    ) && noexcept -> TypeResolution {
        if (failures.owner() != owner()
            || types.owner() != owner()
            || signatures.owner() != owner()) {
            invariant_violation("construction type canonicalization mixed program owners");
        }

        const auto terms = std::move(rows).seal();
        auto resolved = std::vector<TypeID>();
        resolved.reserve(terms.size());
        const auto resolve_ref = [&](ConstructionTypeRef ref) noexcept -> TypeID {
            if (const auto* concrete = std::get_if<TypeID>(&ref)) {
                return *concrete;
            }
            return resolved[std::get<TypeTermID>(ref).index()];
        };
        for (const auto entry : terms.entries()) {
            const auto concrete = canonicalize_type(
                entry.value,
                types,
                signatures,
                resolve_ref,
                [&](FailureTermID term) noexcept {
                    if (!failures.contains(term)) {
                        invariant_violation(
                            "construction callable type used an unresolved failure term"
                        );
                    }
                    return failures.failure_set(term);
                }
            );
            resolved.push_back(concrete);
        }
        return TypeResolution(terms.owner(), std::move(resolved));
    }

private:
    MutableProgramTable<ConstructionType, TypeTermID> rows;
};
