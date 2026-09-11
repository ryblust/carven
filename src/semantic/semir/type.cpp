module carven:semantic.semir.type.impl;

import :semantic.semir.type;
import :support.invariant;
import std;

namespace {

auto require_owner(ProgramIdentity owner, ProgramIdentity expected, std::string_view fact) noexcept
    -> void {
    if (owner != expected) {
        invariant_violation(fact);
    }
}

auto type_key(const CanonicalType& type, ProgramIdentity owner) noexcept -> std::size_t {
    auto hash = type.value.index();
    const auto mix = [&](std::size_t value) noexcept {
        hash ^= value + 0x9e3779b9uz + (hash << 6u) + (hash >> 2u);
    };
    const auto child = [&](auto id, std::string_view message) noexcept {
        require_owner(id.owner(), owner, message);
        mix(id.index());
    };
    const auto text = [&](std::string_view value) noexcept {
        mix(std::hash<std::string_view>()(value));
    };
    std::visit(
        [&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, BuiltinTypeValue>) {
                mix(static_cast<std::size_t>(value.kind));
            } else if constexpr (std::same_as<Value, StructTypeValue>) {
                child(value.structure, "struct type used a foreign program");
            } else if constexpr (std::same_as<Value, EnumTypeValue>) {
                child(value.enumeration, "enum type used a foreign program");
            } else if constexpr (std::same_as<Value, PointerTypeValue>) {
                child(value.target, "ptr type used a foreign target");
                mix(static_cast<std::size_t>(value.access));
            } else if constexpr (std::same_as<Value, SliceTypeValue>) {
                child(value.element, "slice type used a foreign element type");
            } else if constexpr (std::same_as<Value, ArrayTypeValue>) {
                child(value.element, "array type used a foreign element type");
                mix(std::hash<std::uint64_t>()(value.extent));
            } else if constexpr (std::same_as<Value, FunctionTypeValue>
                                 || std::same_as<Value, ClosureTypeValue>) {
                child(value.callable, "callable type used a foreign program");
            } else if constexpr (std::same_as<Value, CallableViewTypeValue>) {
                child(value.signature, "callable view type used a foreign signature");
            } else {
                static_assert(std::same_as<Value, CppTypeValue>);
                mix(value.form.index());
                if (const auto* name = cpp_type_name(value)) {
                    child(name->context_module, "C++ type used a foreign module");
                    mix(static_cast<std::size_t>(name->lookup));
                    for (const auto& component : name->components) {
                        text(component);
                    }
                }
                for (const auto reference : cpp_type_references(value)) {
                    child(reference, "C++ type used a foreign argument");
                }
                if (const auto* query = std::get_if<CppQueryType>(&value.form)) {
                    mix(query->expression.index());
                    std::visit(
                        [&](const auto& expression) noexcept {
                            using Expression = std::remove_cvref_t<decltype(expression)>;
                            if constexpr (std::same_as<Expression, CppMemberQuery>) {
                                text(expression.member);
                            } else if constexpr (std::same_as<Expression, CppUnaryQuery>
                                                 || std::same_as<Expression, CppBinaryQuery>) {
                                mix(static_cast<std::size_t>(expression.operation));
                            } else if constexpr (std::same_as<Expression, CppCallQuery>) {
                                mix(expression.callee.index());
                                if (const auto* member =
                                        std::get_if<CppMemberCallee<CppTypeOperand>>(
                                            &expression.callee
                                        )) {
                                    text(member->member);
                                }
                            }
                        },
                        query->expression
                    );
                }
            }
        },
        type.value
    );
    return hash;
}

auto validate_signature_owner(const CallableSignature& signature, ProgramIdentity owner) noexcept
    -> void {
    require_owner(signature.result.owner(), owner, "callable result used a foreign type");
    require_owner(
        signature.failures.owner(),
        owner,
        "callable signature used a foreign failure set"
    );
    for (const auto& parameter : signature.parameters) {
        require_owner(parameter.type.owner(), owner, "callable parameter used a foreign type");
    }
}

} // namespace

auto builtin_is_integer(BuiltinType type) noexcept -> bool {
    switch (type) {
        case BuiltinType::I8:
        case BuiltinType::I16:
        case BuiltinType::I32:
        case BuiltinType::I64:
        case BuiltinType::U8:
        case BuiltinType::U16:
        case BuiltinType::U32:
        case BuiltinType::U64:
        case BuiltinType::Isize:
        case BuiltinType::Usize:        return true;
        case BuiltinType::Bool:
        case BuiltinType::Char:
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::String:
        case BuiltinType::Str:
        case BuiltinType::StrCharsView:
        case BuiltinType::Void:
        case BuiltinType::EntryArgs:    return false;
    }
    std::unreachable();
}

auto builtin_is_signed_integer(BuiltinType type) noexcept -> bool {
    switch (type) {
        case BuiltinType::I8:
        case BuiltinType::I16:
        case BuiltinType::I32:
        case BuiltinType::I64:
        case BuiltinType::Isize:        return true;
        case BuiltinType::Bool:
        case BuiltinType::Char:
        case BuiltinType::U8:
        case BuiltinType::U16:
        case BuiltinType::U32:
        case BuiltinType::U64:
        case BuiltinType::Usize:
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::String:
        case BuiltinType::Str:
        case BuiltinType::StrCharsView:
        case BuiltinType::Void:
        case BuiltinType::EntryArgs:    return false;
    }
    std::unreachable();
}

auto builtin_is_numeric(BuiltinType type) noexcept -> bool {
    return builtin_is_integer(type) || type == BuiltinType::F32 || type == BuiltinType::F64;
}

auto builtin_integer_width(BuiltinType type) noexcept -> std::optional<std::uint8_t> {
    switch (type) {
        case BuiltinType::I8:
        case BuiltinType::U8:     return 8u;
        case BuiltinType::I16:
        case BuiltinType::U16:    return 16u;
        case BuiltinType::I32:
        case BuiltinType::U32:    return 32u;
        case BuiltinType::I64:
        case BuiltinType::U64:    return 64u;
        case BuiltinType::Isize:  return static_cast<std::uint8_t>(sizeof(std::ptrdiff_t) * 8uz);
        case BuiltinType::Usize:  return static_cast<std::uint8_t>(sizeof(std::size_t) * 8uz);
        case BuiltinType::Bool:
        case BuiltinType::Char:
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::String:
        case BuiltinType::Str:
        case BuiltinType::StrCharsView:
        case BuiltinType::Void:
        case BuiltinType::EntryArgs:    return std::nullopt;
    }
    std::unreachable();
}

CanonicalTypeStore::CanonicalTypeStore(ImmutableProgramTable<CanonicalType, TypeID> values) noexcept
    : rows(std::move(values)) {}

auto CanonicalTypeStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto CanonicalTypeStore::contains(TypeID id) const noexcept -> bool {
    return rows.contains(id);
}

auto CanonicalTypeStore::type(TypeID id) const noexcept -> const CanonicalType& {
    return rows.get(id);
}

auto CanonicalTypeStore::entries() const noexcept
    -> IDTableEntries<TypeID, CanonicalType, ProgramIdentity> {
    return rows.entries();
}

auto CanonicalTypeStore::size() const noexcept -> std::size_t {
    return rows.size();
}

CanonicalTypeStoreBuilder::CanonicalTypeStoreBuilder(ProgramIdentity owner) noexcept
    : rows(owner) {}

auto CanonicalTypeStoreBuilder::intern(const CanonicalType& type) noexcept -> TypeID {
    if (std::holds_alternative<CallableViewTypeValue>(type.value)) {
        invariant_violation(
            "callable view types must be interned through construction type resolution"
        );
    }
    return intern_row(type);
}

auto CanonicalTypeStoreBuilder::intern_row(const CanonicalType& type) noexcept -> TypeID {
    const auto key = type_key(type, rows.owner());
    // Candidate keys may collide; complete type equality determines identity.
    const auto [begin, end] = candidates.equal_range(key);
    for (auto candidate = begin; candidate != end; ++candidate) {
        if (rows.copy(candidate->second) == type) {
            return candidate->second;
        }
    }
    const auto id = rows.add(type);
    candidates.emplace(key, id);
    return id;
}

auto CanonicalTypeStoreBuilder::intern_builtin(BuiltinType type) noexcept -> TypeID {
    return intern(CanonicalType {.value = BuiltinTypeValue {.kind = type}});
}

auto CanonicalTypeStoreBuilder::copy(TypeID id) const noexcept -> CanonicalType {
    return rows.copy(id);
}

auto CanonicalTypeStoreBuilder::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto CanonicalTypeStoreBuilder::intern_resolved_callable_view(
    CallableSignatureID signature,
    const CallableSignatureStoreBuilder& signatures
) noexcept -> TypeID {
    if (signature.owner() != rows.owner() || signatures.owner() != rows.owner()) {
        invariant_violation("resolved callable view mixed semantic program owners");
    }
    static_cast<void>(signatures.copy(signature));
    return intern_row(
        CanonicalType {
            .value = CallableViewTypeValue {.signature = signature},
        }
    );
}

auto CanonicalTypeStoreBuilder::seal() && noexcept -> CanonicalTypeStore {
    return CanonicalTypeStore(std::move(rows).seal());
}

FailureSetStore::FailureSetStore(ImmutableProgramTable<FailureSet, FailureSetID> values) noexcept
    : rows(std::move(values)) {}

auto FailureSetStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto FailureSetStore::contains(FailureSetID id) const noexcept -> bool {
    return rows.contains(id);
}

auto FailureSetStore::failure_set(FailureSetID id) const noexcept -> const FailureSet& {
    return rows.get(id);
}

auto FailureSetStore::entries() const noexcept
    -> IDTableEntries<FailureSetID, FailureSet, ProgramIdentity> {
    return rows.entries();
}

auto FailureSetStore::size() const noexcept -> std::size_t {
    return rows.size();
}

FailureSetStoreBuilder::FailureSetStoreBuilder(ProgramIdentity owner) noexcept
    : rows(owner) {}

auto FailureSetStoreBuilder::intern(std::vector<TypeID> members) noexcept -> FailureSetID {
    for (const auto member : members) {
        require_owner(member.owner(), rows.owner(), "failure set used a foreign type");
    }
    std::ranges::sort(members, {}, &TypeID::index);
    const auto duplicates = std::ranges::unique(members);
    members.erase(duplicates.begin(), duplicates.end());
    return rows.intern(FailureSet {.members = std::move(members)});
}

auto FailureSetStoreBuilder::empty_set() noexcept -> FailureSetID {
    return intern({});
}

auto FailureSetStoreBuilder::copy(FailureSetID id) const noexcept -> FailureSet {
    return rows.copy(id);
}

auto FailureSetStoreBuilder::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto FailureSetStoreBuilder::seal() && noexcept -> FailureSetStore {
    return FailureSetStore(std::move(rows).seal());
}

CallableSignatureStore::CallableSignatureStore(
    ImmutableProgramTable<CallableSignature, CallableSignatureID> values
) noexcept
    : rows(std::move(values)) {}

auto CallableSignatureStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto CallableSignatureStore::contains(CallableSignatureID id) const noexcept -> bool {
    return rows.contains(id);
}

auto CallableSignatureStore::signature(CallableSignatureID id) const noexcept
    -> const CallableSignature& {
    return rows.get(id);
}

auto CallableSignatureStore::entries() const noexcept
    -> IDTableEntries<CallableSignatureID, CallableSignature, ProgramIdentity> {
    return rows.entries();
}

auto CallableSignatureStore::size() const noexcept -> std::size_t {
    return rows.size();
}

CallableSignatureStoreBuilder::CallableSignatureStoreBuilder(ProgramIdentity owner) noexcept
    : rows(owner) {}

auto CallableSignatureStoreBuilder::intern(CallableSignature signature) noexcept
    -> CallableSignatureID {
    validate_signature_owner(signature, rows.owner());
    return rows.intern(std::move(signature));
}

auto CallableSignatureStoreBuilder::copy(CallableSignatureID id) const noexcept
    -> CallableSignature {
    return rows.copy(id);
}

auto CallableSignatureStoreBuilder::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto CallableSignatureStoreBuilder::seal() && noexcept -> CallableSignatureStore {
    return CallableSignatureStore(std::move(rows).seal());
}

auto TypeResolution::type(TypeTermID term) const noexcept -> TypeID {
    if (!contains(term)) {
        invariant_violation("type resolution lookup used a foreign or invalid term");
    }
    return resolved_types[term.index()];
}

ConstructionTypeStore::ConstructionTypeStore(ProgramIdentity owner) noexcept
    : rows(owner) {}

auto ConstructionTypeStore::append(ConstructionType type) noexcept -> TypeTermID {
    const auto validate_ref = [this](ConstructionTypeRef ref) noexcept {
        std::visit(
            [this](const auto id) noexcept {
                using ID = std::remove_cvref_t<decltype(id)>;
                static_assert(std::same_as<ID, TypeID> || std::same_as<ID, TypeTermID>);
                require_owner(id.owner(), rows.owner(), "construction type used a foreign child");
                if constexpr (std::same_as<ID, TypeTermID>) {
                    if (!rows.contains(id)) {
                        invariant_violation("construction type requires an existing child term");
                    }
                }
            },
            ref
        );
    };
    std::visit(
        [&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, ConstructionArrayTypeValue>
                          || std::same_as<Value, ConstructionSliceTypeValue>) {
                validate_ref(value.element);
            } else if constexpr (std::same_as<Value, ConstructionCallableViewTypeValue>) {
                for (const auto& parameter : value.parameters) {
                    validate_ref(parameter.type);
                }
                validate_ref(value.result);
                require_owner(
                    value.failures.owner(),
                    rows.owner(),
                    "construction callable type used a foreign failure term"
                );
            } else {
                static_assert(std::same_as<Value, void>);
            }
        },
        type.value
    );
    return rows.add(std::move(type));
}

auto ConstructionTypeStore::copy(TypeTermID id) const noexcept -> ConstructionType {
    return rows.copy(id);
}

auto ConstructionTypeStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto ConstructionTypeStore::size() const noexcept -> std::size_t {
    return rows.size();
}
