module carven:semantic.semir.generic;

import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.ids;
import :semantic.semir.type;
import std;

// Object types and completion effects occupy distinct parameter domains.
// Construction environments retain unsolved references; instance keys do not.
using GenericArgument = std::variant<ConstructionTypeRef, ConstructionFailureRef>;
using GenericEnvironment = std::vector<GenericArgument>;
using CanonicalGenericArgument = std::variant<TypeID, FailureSetID>;
using CanonicalGenericEnvironment = std::vector<CanonicalGenericArgument>;

auto generic_environment(std::span<const CanonicalGenericArgument> arguments) noexcept
    -> GenericEnvironment {
    auto result = GenericEnvironment();
    for (const auto& argument : arguments) {
        argument.visit([&](const auto value) noexcept {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, TypeID>) {
                result.emplace_back(ConstructionTypeRef(value));
            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, FailureSetID>) {
                result.emplace_back(ConstructionFailureRef(value));
            }
        });
    }
    return result;
}

// Parameter identities remain rigid while a definition is checked.
struct GenericTypeParameter final {
    GenericDeclarationID definition;
    std::uint32_t index;
    auto operator<=>(const GenericTypeParameter&) const noexcept = default;
};

struct GenericArrayType final {
    ConstructionTypeRef element;
    std::uint64_t extent;
    auto operator<=>(const GenericArrayType&) const noexcept = default;
};

struct GenericTupleType final {
    std::vector<ConstructionTypeRef> elements;
    auto operator<=>(const GenericTupleType&) const noexcept = default;
};

struct GenericSliceType final {
    ConstructionTypeRef element;
    auto operator<=>(const GenericSliceType&) const noexcept = default;
};

struct GenericVectorType final {
    ConstructionTypeRef element;
    auto operator<=>(const GenericVectorType&) const noexcept = default;
};

struct GenericPointerType final {
    ConstructionTypeRef target;
    PointerAccess access;
    auto operator<=>(const GenericPointerType&) const noexcept = default;
};

struct GenericOperationType final {
    ConstructionTypeRef success;
    ConstructionFailureRef failures;
    auto operator<=>(const GenericOperationType&) const noexcept = default;
};

struct GenericCallableViewType final {
    std::vector<ConstructionCallableParameter> parameters;
    ConstructionTypeRef result;
    ConstructionFailureRef failures;
    auto operator<=>(const GenericCallableViewType&) const noexcept = default;
};

struct GenericClosureType final {
    GenericDeclarationID definition;
    GenericEnvironment arguments;
    auto operator<=>(const GenericClosureType&) const noexcept = default;
};

struct GenericNominalApplication final {
    GenericDeclarationID definition;
    GenericEnvironment arguments;
    auto operator<=>(const GenericNominalApplication&) const noexcept = default;
};

struct GenericCppNamedType final {
    CppNameReference name;
    std::vector<ConstructionTypeRef> arguments;
    auto operator<=>(const GenericCppNamedType&) const noexcept = default;
};

using GenericCppQueryType = BasicCppQueryType<ConstructionTypeRef>;

using GenericTypeExpression = std::variant<
    TypeID,
    GenericTypeParameter,
    GenericArrayType,
    GenericTupleType,
    GenericSliceType,
    GenericVectorType,
    GenericPointerType,
    GenericOperationType,
    GenericCallableViewType,
    GenericClosureType,
    GenericNominalApplication,
    GenericCppNamedType,
    GenericCppQueryType>;

template<typename Visitor>
// A traversal reuses the same visitor; invoking an rvalue must not consume it.
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
auto visit_generic_type_children(const GenericTypeExpression& type, Visitor&& visitor) noexcept
    -> void {
    type.visit([&](const auto& shape) noexcept {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, GenericArrayType>
                      || std::same_as<Shape, GenericSliceType>
                      || std::same_as<Shape, GenericVectorType>) {
            visitor(shape.element);
        } else if constexpr (std::same_as<Shape, GenericTupleType>) {
            for (const auto element : shape.elements) {
                visitor(element);
            }
        } else if constexpr (std::same_as<Shape, GenericPointerType>) {
            visitor(shape.target);
        } else if constexpr (std::same_as<Shape, GenericOperationType>) {
            visitor(shape.success);
        } else if constexpr (std::same_as<Shape, GenericCallableViewType>) {
            for (const auto& parameter : shape.parameters) {
                visitor(parameter.type);
            }
            visitor(shape.result);
        } else if constexpr (std::same_as<Shape, GenericCppNamedType>) {
            for (const auto argument : shape.arguments) {
                visitor(argument);
            }
        } else if constexpr (std::same_as<Shape, GenericCppQueryType>) {
            visit_cpp_query_types(shape, visitor);
        } else if constexpr (std::same_as<Shape, GenericNominalApplication>
                             || std::same_as<Shape, GenericClosureType>) {
            for (const auto& argument : shape.arguments) {
                if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                    visitor(*type);
                }
            }
        }
    });
}

template<typename Visitor>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
auto visit_construction_type_children(const ConstructionType& type, Visitor&& visitor) noexcept
    -> void {
    type.value.visit([&](const auto& shape) noexcept {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, ConstructionArrayTypeValue>
                      || std::same_as<Shape, ConstructionSliceTypeValue>) {
            visitor(shape.element);
        } else if constexpr (std::same_as<Shape, ConstructionTupleTypeValue>) {
            for (const auto element : shape.elements) {
                visitor(element);
            }
        } else if constexpr (std::same_as<Shape, ConstructionOperationTypeValue>) {
            visitor(shape.success);
        } else if constexpr (std::same_as<Shape, ConstructionCallableViewTypeValue>) {
            for (const auto& parameter : shape.parameters) {
                visitor(parameter.type);
            }
            visitor(shape.result);
        }
    });
}

struct GenericAudienceDependency final {
    std::variant<TypeID, GenericDeclarationID> reference;
    ProgramOriginID origin;
};

enum class GenericParameterKind { Type, FailureSet };

enum class BuiltinCapability { Hash, Equal };

template<typename Argument>
auto generic_argument_kind(const Argument& argument) noexcept -> GenericParameterKind {
    return argument.visit([](const auto& value) static noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        return std::same_as<Value, ConstructionTypeRef> || std::same_as<Value, TypeID>
            ? GenericParameterKind::Type
            : GenericParameterKind::FailureSet;
    });
}

template<typename Argument>
auto generic_argument_matches(GenericParameterKind kind, const Argument& argument) noexcept
    -> bool {
    return generic_argument_kind(argument) == kind;
}

template<typename Visitor>
// Preserve the lvalue visitor protocol used by the child traversals.
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
auto visit_generic_argument_types(const GenericArgument& argument, Visitor&& visitor) noexcept
    -> void {
    if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
        visitor(*type);
    }
}

template<typename Visitor>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
auto visit_generic_argument_failures(const GenericArgument& argument, Visitor&& visitor) noexcept
    -> void {
    if (const auto* failures = std::get_if<ConstructionFailureRef>(&argument)) {
        visitor(*failures);
    }
}

// Concrete dependencies of a checked parameterized definition are needed by
// its producer even before any consumer requests a concrete instance.
struct GenericImplementationDependencies final {
    std::vector<TypeID> types;
    std::vector<CallableID> callables;
    std::vector<GenericDeclarationID> definitions;
};

struct GenericDeclarationContract final {
    ModuleID module_id;
    std::optional<ProgramSpellingID> name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    // Lexical closures use their root definition's parameter contract.
    std::optional<GenericDeclarationID> parameter_scope;
    std::vector<ProgramSpellingID> parameters;
    std::vector<GenericAudienceDependency> audience_dependencies;
    std::vector<GenericParameterKind> parameter_kinds;
    std::vector<std::vector<BuiltinCapability>> parameter_bounds;
    GenericImplementationDependencies implementation_dependencies;
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

enum class TypeFormationRequirementKind { NonVoidObject };

enum class TypeFormationStatus { Supported, Rejected, Dependent, NativeDelegated };

struct TypeFormationRequirement final {
    TypeFormationRequirementKind kind;
    ConstructionTypeRef type;
    ProgramOriginID origin;
};

enum class TypeFormationClosure { Complete, Pending };

struct GenericCallableDefinition final {
    GenericDeclarationContract contract;
    ConstructionCallableContract signature;
    bool is_const;
    bool infer_result;
    std::vector<TypeFormationRequirement> type_formations;
};

using GenericDeclarationDefinition =
    std::variant<GenericRecordDefinition, GenericEnumDefinition, GenericCallableDefinition>;

// Published instances refer to ordinary concrete declarations. Their checked
// parameterized fields are construction state and never reach lowering.
struct GenericNominalInstance final {
    GenericDeclarationID definition;
    CanonicalGenericEnvironment arguments;
    NominalDeclarationRef declaration;
    ProgramOriginID origin;
};

struct GenericCallableInstance final {
    GenericDeclarationID definition;
    CanonicalGenericEnvironment arguments;
    CallableID callable;
};
