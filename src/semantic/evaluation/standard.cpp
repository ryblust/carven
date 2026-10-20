module;

#include <carven/runtime/call/passing.hpp>
#include <carven/std/build/native.hpp>
#include <carven/std/channel/native.hpp>
#include <carven/std/collections/native.hpp>
#include <carven/std/compiler/native.hpp>
#include <carven/std/fs/native.hpp>
#include <carven/std/io/native.hpp>
#include <carven/std/net/native.hpp>
#include <carven/std/process/native.hpp>

module carven:semantic.evaluation.standard.impl;

import :semantic.evaluation.standard;
import :semantic.evaluation.value;
import :semantic.semir.delegation;
import :semantic.semir.type;
import :support.invariant;
import std;

namespace {

namespace runtime = carven::runtime;
namespace standard = carven::standard;

using NativeResult = std::expected<ExecutionHostResult, ExecutionCallFailure>;

enum class NativeOperation { Function, Method, Field, Conversion };

template<typename T>
struct NativeSequence final {
    static constexpr auto kind = StandardNativeKind::Opaque;
};

template<typename T>
struct NativeSequence<runtime::Vector<T>> final {
    using Element = T;
    static constexpr auto kind = StandardNativeKind::Vector;
};

template<typename T>
struct NativeSequence<runtime::Slice<T>> final {
    using Element = T;
    static constexpr auto kind = StandardNativeKind::Slice;
};

template<typename T>
inline constexpr auto native_name = std::string_view();
#define CARVEN_STANDARD_TYPE(type)                                                                 \
    template<>                                                                                     \
    inline constexpr auto native_name<standard::type> =                                            \
        std::string_view("carven::standard::" #type);
CARVEN_STANDARD_TYPE(compiler::CompilationResult)
CARVEN_STANDARD_TYPE(compiler::Implementation)
CARVEN_STANDARD_TYPE(channel::Handle)
CARVEN_STANDARD_TYPE(channel::Result)
CARVEN_STANDARD_TYPE(fs::ReadResult)
CARVEN_STANDARD_TYPE(fs::StatusResult)
CARVEN_STANDARD_TYPE(fs::RemovalResult)
CARVEN_STANDARD_TYPE(fs::PathResult)
CARVEN_STANDARD_TYPE(fs::PathsResult)
CARVEN_STANDARD_TYPE(net::View)
CARVEN_STANDARD_TYPE(net::ReadResult)
CARVEN_STANDARD_TYPE(net::WriteResult)
CARVEN_STANDARD_TYPE(process::Handle)
CARVEN_STANDARD_TYPE(process::Group)
CARVEN_STANDARD_TYPE(process::SpawnResult)
CARVEN_STANDARD_TYPE(process::Result)
CARVEN_STANDARD_TYPE(process::RunResult)
CARVEN_STANDARD_TYPE(process::TextResult)
CARVEN_STANDARD_TYPE(process::ArgumentsResult)
CARVEN_STANDARD_TYPE(process::Exit)
#undef CARVEN_STANDARD_TYPE

template<typename T>
auto native_type() noexcept -> const StandardNativeType& {
    using Value = std::remove_cvref_t<T>;
    static const auto type = StandardNativeType {
        .identity = runtime::hosting::detail::type_key<Value>(),
        .kind =
            []() static noexcept {
                if constexpr (std::is_void_v<Value>) {
                    return StandardNativeKind::Void;
                } else if constexpr (std::same_as<Value, bool>) {
                    return StandardNativeKind::Bool;
                } else if constexpr (std::signed_integral<Value>) {
                    return StandardNativeKind::Signed;
                } else if constexpr (std::unsigned_integral<Value>) {
                    return StandardNativeKind::Unsigned;
                } else if constexpr (std::same_as<Value, runtime::String>) {
                    return StandardNativeKind::String;
                } else if constexpr (std::same_as<Value, std::string_view>) {
                    return StandardNativeKind::Str;
                } else {
                    return NativeSequence<Value>::kind;
                }
            }(),
        .element = []() static noexcept -> const StandardNativeType* {
            if constexpr (requires { typename NativeSequence<Value>::Element; }) {
                return &native_type<typename NativeSequence<Value>::Element>();
            } else {
                return nullptr;
            }
        }(),
        .name = native_name<Value>,
        .integer_bits =
            []() static noexcept {
                if constexpr (std::integral<Value>) {
                    return sizeof(Value) * 8uz;
                } else {
                    return 0uz;
                }
            }(),
    };
    return type;
}

struct NativeParameter final {
    const StandardNativeType* type;
    AccessMode access;
};

struct StandardOperation final {
    std::string_view name;
    NativeOperation kind;
    std::vector<NativeParameter> parameters;
    const StandardNativeType* result;
    bool asynchronous;
    bool effects;
    bool result_is_value;
    NativeResult (*invoke)(
        const ExecutionValueAccess&,
        SemanticExecutionContext&,
        const SemanticExpression&,
        std::span<ExecutionValue>,
        ProgramOriginID,
        bool
    ) noexcept;
};

auto operations() noexcept -> std::span<const StandardOperation>;
auto native_identity(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> const StandardNativeType*;
auto matches_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    const StandardNativeType& native
) noexcept -> bool;

auto global_name(const CppNameReference& reference) noexcept -> std::string {
    auto result = std::string();
    if (reference.lookup == CppNameLookup::Global) {
        for (const auto& part : reference.components) {
            if (!result.empty()) {
                result += "::";
            }
            result += part;
        }
    }
    return result;
}

auto select_operation(
    const ExecutionValueAccess& values,
    NativeOperation kind,
    std::string_view name,
    std::span<const std::pair<ConstructionTypeRef, AccessMode>> parameters,
    std::optional<ConstructionTypeRef> result
) noexcept -> const StandardOperation* {
    for (const auto& entry : operations()) {
        if (entry.kind != kind
            || entry.name != name
            || entry.parameters.size() != parameters.size()) {
            continue;
        }
        auto matches = true;
        for (auto index = 0uz; index < parameters.size(); ++index) {
            const auto& expected = entry.parameters[index];
            if ((expected.access == AccessMode::Write
                 && parameters[index].second == AccessMode::Read)
                || (expected.access == AccessMode::Take
                    && parameters[index].second != AccessMode::Take)
                || !matches_type(values, parameters[index].first, *expected.type)) {
                matches = false;
                break;
            }
        }
        if (matches && (!result || matches_type(values, *result, *entry.result))) {
            return &entry;
        }
    }
    return nullptr;
}

template<typename Operand, typename Argument>
auto select_call(
    const ExecutionValueAccess& values,
    const CppCallee<Operand>& callee,
    std::span<const Argument> arguments
) noexcept -> const StandardOperation* {
    auto parameters = std::vector<std::pair<ConstructionTypeRef, AccessMode>>();
    auto name = std::string();
    auto kind = NativeOperation::Function;
    if (const auto* function = std::get_if<CppNameReference>(&callee)) {
        name = global_name(*function);
    } else if (const auto* method = std::get_if<CppMemberCallee<Operand>>(&callee)) {
        kind = NativeOperation::Method;
        name = method->member;
        if constexpr (std::same_as<Operand, SemCppOperand>) {
            parameters.emplace_back(
                method->receiver.expression->type.construction(),
                method->receiver.access
            );
        } else {
            parameters.emplace_back(method->receiver.type, method->receiver.access);
        }
    } else {
        return nullptr;
    }
    for (const auto& argument : arguments) {
        if constexpr (std::same_as<Argument, SemCallArgument>) {
            parameters.emplace_back(argument.expression.type.construction(), argument.access);
        } else {
            parameters.emplace_back(argument.type, argument.access);
        }
    }
    return select_operation(values, kind, name, parameters, std::nullopt);
}

template<typename Type>
auto query_operation(
    const ExecutionValueAccess& values,
    const BasicCppQueryType<Type>& query
) noexcept -> const StandardOperation* {
    return query.expression.visit([&](const auto& expression) noexcept -> const StandardOperation* {
        using Query = std::remove_cvref_t<decltype(expression)>;
        if constexpr (std::same_as<Query, BasicCppCallQuery<Type>>) {
            return select_call(values, expression.callee, std::span(expression.arguments));
        } else if constexpr (std::same_as<Query, BasicCppMemberQuery<Type>>) {
            const auto parameters = std::array {std::pair<ConstructionTypeRef, AccessMode> {
                expression.receiver.type,
                expression.receiver.access
            }};
            return select_operation(
                values,
                NativeOperation::Field,
                expression.member,
                parameters,
                std::nullopt
            );
        } else {
            return nullptr;
        }
    });
}

template<typename Named>
auto named_identity(const Named& named) noexcept -> const StandardNativeType* {
    const auto name = global_name(named.name);
    const auto find = [&](const StandardNativeType* type) noexcept -> const StandardNativeType* {
        while (type != nullptr) {
            if (!type->name.empty() && type->name == name && named.arguments.empty()) {
                return type;
            }
            type = type->element;
        }
        return nullptr;
    };
    for (const auto& operation : operations()) {
        if (const auto* result = find(operation.result)) {
            return result;
        }
        for (const auto& parameter : operation.parameters) {
            if (const auto* result = find(parameter.type)) {
                return result;
            }
        }
    }
    return nullptr;
}

auto native_identity(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> const StandardNativeType* {
    return visit_execution_type(
        values,
        type,
        [&](const auto& shape) noexcept -> const StandardNativeType* {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, CppTypeValue>) {
                if (const auto* query = std::get_if<CppQueryType>(&shape.form)) {
                    const auto* operation = query_operation(values, *query);
                    return operation == nullptr ? nullptr : operation->result;
                }
                const auto* named = std::get_if<CppNamedType>(&shape.form);
                return named == nullptr ? nullptr : named_identity(*named);
            } else if constexpr (std::same_as<Shape, GenericCppNamedType>) {
                return named_identity(shape);
            } else if constexpr (std::same_as<Shape, GenericCppQueryType>) {
                const auto* operation = query_operation(values, shape);
                return operation == nullptr ? nullptr : operation->result;
            } else {
                return nullptr;
            }
        }
    );
}

auto matches_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    const StandardNativeType& native
) noexcept -> bool {
    if (const auto* identity = native_identity(values, type)) {
        return identity->identity == native.identity;
    }
    return visit_execution_type(values, type, [&](const auto& shape) noexcept -> bool {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
            switch (shape.kind) {
                case BuiltinType::Void: return native.kind == StandardNativeKind::Void;
                case BuiltinType::Bool: return native.kind == StandardNativeKind::Bool;
                case BuiltinType::I8:
                case BuiltinType::I16:
                case BuiltinType::I32:
                case BuiltinType::I64:
                case BuiltinType::Isize:
                    return native.kind == StandardNativeKind::Signed
                        && builtin_integer_width(shape.kind) == native.integer_bits;
                case BuiltinType::U8:
                case BuiltinType::U16:
                case BuiltinType::U32:
                case BuiltinType::U64:
                case BuiltinType::Usize:
                    return native.kind == StandardNativeKind::Unsigned
                        && builtin_integer_width(shape.kind) == native.integer_bits;
                case BuiltinType::Str:    return native.kind == StandardNativeKind::Str;
                case BuiltinType::String: return native.kind == StandardNativeKind::String;
                default:                  return false;
            }
        } else if constexpr (std::same_as<Shape, VectorTypeValue>
                             || std::same_as<Shape, GenericVectorType>) {
            return native.kind == StandardNativeKind::Vector
                && matches_type(values, shape.element, *native.element);
        } else if constexpr (std::same_as<Shape, SliceTypeValue>
                             || std::same_as<Shape, ConstructionSliceTypeValue>
                             || std::same_as<Shape, GenericSliceType>) {
            return native.kind == StandardNativeKind::Slice
                && matches_type(values, shape.element, *native.element);
        } else {
            return false;
        }
    });
}

auto expression_operation(
    const ExecutionValueAccess& values,
    const SemanticExpression& expression
) noexcept -> const StandardOperation* {
    if (const auto* call = std::get_if<SemCppCall>(&expression.value)) {
        const auto* selected = select_call(values, call->callee, std::span(call->arguments));
        if (selected == nullptr) {
            return nullptr;
        }
        if (selected->asynchronous) {
            const auto compatible = visit_execution_type(
                values,
                expression.type.construction(),
                [&](const auto& shape) noexcept {
                    using Shape = std::remove_cvref_t<decltype(shape)>;
                    if constexpr (std::same_as<Shape, OperationTypeValue>
                                  || std::same_as<Shape, ConstructionOperationTypeValue>
                                  || std::same_as<Shape, GenericOperationType>) {
                        return matches_type(values, shape.success, *selected->result);
                    } else {
                        return false;
                    }
                }
            );
            return compatible ? selected : nullptr;
        }
        return matches_type(values, expression.type.construction(), *selected->result) ? selected
                                                                                       : nullptr;
    }
    const auto* operation = std::get_if<SemCpp>(&expression.value);
    if (operation == nullptr) {
        return nullptr;
    }
    auto parameters = std::vector<std::pair<ConstructionTypeRef, AccessMode>>();
    for (const auto& operand : operation->operands) {
        parameters.emplace_back(operand.expression.type.construction(), operand.access);
    }
    if (const auto* field = std::get_if<CppMemberOperation>(&operation->operation)) {
        return select_operation(
            values,
            NativeOperation::Field,
            field->name,
            parameters,
            expression.type.construction()
        );
    }
    if (std::holds_alternative<CppConvertOperation>(operation->operation)) {
        return select_operation(
            values,
            NativeOperation::Conversion,
            "conversion",
            parameters,
            expression.type.construction()
        );
    }
    return nullptr;
}

auto native_error(ProgramOriginID origin, std::string message) noexcept
    -> std::unexpected<ExecutionCallFailure> {
    return std::unexpected<ExecutionCallFailure>(ExecutionEvent {
        .origin = origin,
        .cause =
            ExecutionIssue {
                .reason = ExecutionReason::Admission,
                .message = std::move(message),
                .termination = ExecutionTermination::StopRoot
            },
        .fields = {},
        .calls = {},
        .blocks = {},
    });
}

// Slices and text retain their backing in the typed activation.
template<typename T>
struct ArgumentStorage final {
    using Type = T;
};

template<>
struct ArgumentStorage<std::string_view> final {
    using Type = std::string;
};

template<typename T>
struct ArgumentStorage<runtime::Slice<T>> final {
    using Type = runtime::Vector<T>;
};

template<typename T>
auto decode_native(const ExecutionValueAccess& values, const ExecutionValue& input) noexcept
    -> std::expected<T, std::string> {
    if (const auto* native = std::get_if<ExecutionHostObject>(&input)) {
        if (auto* value = native->reference.template get<T>()) {
            return *value;
        }
        return std::unexpected("standard native argument has the wrong native type");
    }
    if constexpr (std::integral<T>) {
        const auto atom = execution_atom(values, input);
        if (atom) {
            if constexpr (std::same_as<T, bool>) {
                if (const auto* boolean = std::get_if<BooleanConstant>(&atom->value)) {
                    return boolean->value;
                }
            } else if (const auto* integer = std::get_if<IntegerConstant>(&atom->value)) {
                if constexpr (std::is_signed_v<T>) {
                    const auto value = integer->as_signed();
                    if (value && std::in_range<T>(*value)) {
                        return static_cast<T>(*value);
                    }
                } else if (!integer->negative()
                           && integer->magnitude() <= std::numeric_limits<T>::max()) {
                    return static_cast<T>(integer->magnitude());
                }
            }
        }
    } else if constexpr (std::same_as<T, std::string> || std::same_as<T, runtime::String>) {
        if (const auto text = execution_text(values, input)) {
            if constexpr (std::same_as<T, std::string>) {
                return std::string(text->view());
            } else {
                return runtime::String::from_str(text->view());
            }
        }
    } else if constexpr (NativeSequence<T>::kind == StandardNativeKind::Vector) {
        if constexpr (std::same_as<typename NativeSequence<T>::Element, std::uint8_t>) {
            if (const auto* bytes = std::get_if<ExecutionBytes>(&input)) {
                return bytes->bytes;
            }
        }
        if (const auto compound = execution_compound_view(values, input)) {
            if constexpr (std::same_as<typename NativeSequence<T>::Element, std::uint8_t>) {
                if (const auto* bytes = std::get_if<ExecutionByteView>(&compound->elements)) {
                    return T::from_slice(
                        runtime::Slice<std::uint8_t>(std::span(
                            reinterpret_cast<const std::uint8_t*>(bytes->bytes.data()),
                            bytes->bytes.size()
                        ))
                    );
                }
            }
            auto result = T();
            result.reserve(compound->size());
            auto error = std::optional<std::string>();
            compound->elements.visit([&](const auto& elements) noexcept {
                for (auto index = 0uz; index < elements.size() && !error; ++index) {
                    auto element = [&]() noexcept {
                        if constexpr (std::same_as<
                                          std::remove_cvref_t<decltype(elements)>,
                                          std::span<const ConstantID>>) {
                            return decode_native<typename NativeSequence<T>::Element>(
                                values,
                                ExecutionValue(elements[index])
                            );
                        } else {
                            return decode_native<typename NativeSequence<T>::Element>(
                                values,
                                elements[index]
                            );
                        }
                    }();
                    if (!element) {
                        error.emplace(std::move(element.error()));
                    } else {
                        result.push(std::move(*element));
                    }
                }
            });
            if (error) {
                return std::unexpected(std::move(*error));
            }
            return result;
        }
    }
    return std::unexpected("standard native argument does not satisfy its checked contract");
}

template<typename T>
auto decode_argument(
    const ExecutionValueAccess& values,
    ExecutionValue& input,
    AccessMode access
) noexcept -> std::expected<T, std::string> {
    if (access == AccessMode::Take) {
        if constexpr (std::same_as<T, runtime::Vector<std::uint8_t>>) {
            if (auto* bytes = std::get_if<ExecutionBytes>(&input)) {
                return std::move(bytes->bytes);
            }
        } else if constexpr (std::same_as<T, runtime::String>) {
            if (auto* text = std::get_if<ExecutionOwnedText>(&input)) {
                return runtime::String::from_utf8(std::move(*text).into_utf8());
            }
        }
        if constexpr (NativeSequence<T>::kind == StandardNativeKind::Vector) {
            if (auto* aggregate = std::get_if<ExecutionAggregateValue>(&input)) {
                auto result = T();
                result.reserve(aggregate->elements.size());
                for (auto& element : aggregate->elements) {
                    auto decoded = decode_argument<typename NativeSequence<T>::Element>(
                        values,
                        element,
                        access
                    );
                    if (!decoded) {
                        return std::unexpected(std::move(decoded.error()));
                    }
                    result.push(std::move(*decoded));
                }
                return result;
            }
        }
    }
    return decode_native<T>(values, input);
}

template<typename T>
auto encode_native(
    const ExecutionValueAccess& values,
    T value,
    ConstructionTypeRef type,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionValue, ExecutionCallFailure> {
    type = execution_type_reference(values, type);
    const auto opaque = visit_execution_type(values, type, [](const auto& shape) static noexcept {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        return std::same_as<Shape, CppTypeValue>
            || std::same_as<Shape, GenericCppNamedType>
            || std::same_as<Shape, GenericCppQueryType>;
    });
    if (opaque) {
        return ExecutionHostObject(
            type,
            runtime::hosting::ObjectRef<void>::own(std::move(value)),
            std::is_reference_v<runtime::ReadArg<T>>
        );
    }
    if constexpr (std::same_as<T, bool>) {
        return ConstantAtom {
            .type = std::get<TypeID>(type),
            .value = BooleanConstant {.value = value}
        };
    } else if constexpr (std::integral<T>) {
        return ConstantAtom {.type = std::get<TypeID>(type), .value = [&]() noexcept {
                                 if constexpr (std::is_signed_v<T>) {
                                     return IntegerConstant::from_signed(value);
                                 } else {
                                     return IntegerConstant::from_parts(value, false);
                                 }
                             }()};
    } else if constexpr (std::same_as<T, runtime::String>) {
        return ExecutionOwnedText(std::move(value).into_utf8());
    } else if constexpr (NativeSequence<T>::kind == StandardNativeKind::Vector) {
        const auto element = execution_type_reference(
            values,
            visit_execution_type(
                values,
                type,
                [](const auto& shape) static noexcept -> ConstructionTypeRef {
                    using Shape = std::remove_cvref_t<decltype(shape)>;
                    if constexpr (std::same_as<Shape, VectorTypeValue>
                                  || std::same_as<Shape, GenericVectorType>) {
                        return shape.element;
                    } else {
                        invariant_violation("standard native vector result has no element type");
                    }
                }
            )
        );
        if constexpr (std::same_as<typename NativeSequence<T>::Element, std::uint8_t>) {
            return ExecutionBytes {
                .type = type,
                .element = std::get<TypeID>(element),
                .bytes = std::move(value)
            };
        } else {
            auto elements = std::vector<ExecutionValue>();
            elements.reserve(value.size());
            for (auto& input : value) {
                auto encoded = encode_native(values, std::move(input), element, origin);
                if (!encoded) {
                    return std::unexpected(std::move(encoded.error()));
                }
                elements.push_back(std::move(*encoded));
            }
            return ExecutionAggregateValue {
                .type = type,
                .elements = std::move(elements),
                .capacity = value.size()
            };
        }
    }
    return native_error(origin, "standard native result does not satisfy its checked contract");
}

template<typename T>
struct NativeAsync final {
    static constexpr auto asynchronous = false;
    using Value = T;
    using Root = std::monostate;
};

template<typename T>
struct NativeAsync<runtime::execution::AsyncOperation<T>> final {
    static constexpr auto asynchronous = true;
    using Value = T;
    using Root = runtime::execution::Root<T>;
};

template<typename Signature>
struct NativeSignature;

template<typename R, typename... A>
struct NativeSignature<R (*)(A...) noexcept> final {
    using Type = R(A...);
    static constexpr auto kind = NativeOperation::Function;
};

template<typename O, typename R, typename... A>
struct NativeSignature<R (O::*)(A...) noexcept> final {
    using Type = R(O&, A...);
    static constexpr auto kind = NativeOperation::Method;
};

template<typename O, typename R, typename... A>
struct NativeSignature<R (O::*)(A...) const noexcept> final {
    using Type = R(const O&, A...);
    static constexpr auto kind = NativeOperation::Method;
};

template<typename O, typename R>
struct NativeSignature<R O::*> final {
    using Type = R(const O&);
    static constexpr auto kind = NativeOperation::Field;
};

template<auto Function, typename Signature>
class StandardActivation;

template<auto Function, typename R, typename... A>
class StandardActivation<Function, R(A...)> final : public ExecutionExternalOperation {
    static_assert(
        !std::is_reference_v<R>,
        "standard execution requires materialized native results"
    );

    using Async = NativeAsync<R>;
    template<typename T>
    using Value = std::remove_cvref_t<T>;

public:
    StandardActivation(
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        ConstructionTypeRef result,
        ProgramOriginID origin,
        bool effects
    ) noexcept
        : values(values),
          context(context),
          result_type(result),
          origin(origin),
          effects(effects) {}

    ~StandardActivation() noexcept override { cancel(); }

    template<std::size_t... I>
    auto decode(
        std::span<ExecutionValue> arguments,
        std::span<const AccessMode> accesses,
        std::index_sequence<I...>
    ) noexcept -> std::expected<void, std::string> {
        auto error = std::optional<std::string>();
        const auto argument = [&]<std::size_t Index, typename Parameter>() noexcept {
            using T = Value<Parameter>;
            if (error) {
                return;
            }
            if (const auto* native = std::get_if<ExecutionHostObject>(&arguments[Index])) {
                if (auto* pointer = native->reference.template get<T>()) {
                    retained.push_back(native->reference);
                    if constexpr (std::is_reference_v<Parameter>
                                  || std::same_as<T, std::string_view>
                                  || NativeSequence<T>::kind == StandardNativeKind::Slice) {
                        std::get<Index>(inputs) = pointer;
                    } else {
                        if (accesses[Index] == AccessMode::Take) {
                            std::get<Index>(owned).emplace(std::move(*pointer));
                        } else {
                            std::get<Index>(owned).emplace(*pointer);
                        }
                        std::get<Index>(inputs) = &*std::get<Index>(owned);
                    }
                    return;
                }
            }
            auto decoded = decode_argument<typename ArgumentStorage<T>::Type>(
                values,
                arguments[Index],
                accesses[Index]
            );
            if (!decoded) {
                error.emplace(std::move(decoded.error()));
                return;
            }
            auto& storage = std::get<Index>(owned);
            storage.emplace(std::move(*decoded));
            if constexpr (std::same_as<T, std::string_view>) {
                std::get<Index>(views).emplace(*storage);
                std::get<Index>(inputs) = &*std::get<Index>(views);
            } else if constexpr (NativeSequence<T>::kind == StandardNativeKind::Slice) {
                std::get<Index>(views).emplace(
                    std::span<const typename NativeSequence<T>::Element>(
                        storage->begin(),
                        storage->size()
                    )
                );
                std::get<Index>(inputs) = &*std::get<Index>(views);
            } else {
                std::get<Index>(inputs) = &*storage;
            }
        };
        (argument.template operator()<I, A>(), ...);
        if (error) {
            return std::unexpected(std::move(*error));
        }
        return {};
    }

    template<std::size_t... I>
    auto invoke(std::index_sequence<I...>) noexcept -> R {
        return std::invoke(Function, static_cast<A&&>(*std::get<I>(inputs))...);
    }

    static auto call(
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        const SemanticExpression& expression,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin,
        bool effects
    ) noexcept -> NativeResult {
        auto accesses = std::vector<AccessMode>();
        expression.value.visit([&](const auto& operation) noexcept {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemCpp> || std::same_as<Operation, SemCppCall>) {
                visit_cpp_operands(
                    operation,
                    [&](AccessMode access, const SemanticExpression&) noexcept {
                        accesses.push_back(access);
                    }
                );
            }
        });
        auto result = execution_type_reference(values, expression.type.construction());
        if constexpr (Async::asynchronous) {
            result = visit_execution_type(
                values,
                result,
                [](const auto& shape) static noexcept -> ConstructionTypeRef {
                    using Shape = std::remove_cvref_t<decltype(shape)>;
                    if constexpr (std::same_as<Shape, OperationTypeValue>
                                  || std::same_as<Shape, ConstructionOperationTypeValue>
                                  || std::same_as<Shape, GenericOperationType>) {
                        return shape.success;
                    } else {
                        invariant_violation(
                            "standard asynchronous native result has no operation type"
                        );
                    }
                }
            );
            result = execution_type_reference(values, result);
            auto activation =
                std::make_unique<StandardActivation>(values, context, result, origin, effects);
            auto decoded = activation->decode(arguments, accesses, std::index_sequence_for<A...>());
            if (!decoded) {
                return native_error(origin, std::move(decoded.error()));
            }
            activation->operation.emplace(activation->invoke(std::index_sequence_for<A...>()));
            return ExecutionHostResult(
                std::unique_ptr<ExecutionExternalOperation>(std::move(activation))
            );
        } else {
            auto activation = StandardActivation(values, context, result, origin, effects);
            auto decoded = activation.decode(arguments, accesses, std::index_sequence_for<A...>());
            if (!decoded) {
                return native_error(origin, std::move(decoded.error()));
            }
            if constexpr (std::is_void_v<R>) {
                activation.invoke(std::index_sequence_for<A...>());
                return ExecutionHostResult(ExecutionVoid {});
            } else {
                auto encoded = encode_native(
                    values,
                    activation.invoke(std::index_sequence_for<A...>()),
                    result,
                    origin
                );
                if (!encoded) {
                    return std::unexpected(std::move(encoded.error()));
                }
                return ExecutionHostResult(std::move(*encoded));
            }
        }
    }

    auto poll() noexcept
        -> std::optional<std::expected<ExecutionValue, ExecutionCallFailure>> override {
        if constexpr (Async::asynchronous) {
            if (!root) {
                if (effects) {
                    if (const auto rejection = context.host_effects_rejection()) {
                        operation.reset();
                        complete = true;
                        return native_error(origin, std::string(*rejection));
                    }
                }
                root.emplace(std::move(*operation));
                operation.reset();
            }
            auto progress = root->advance();
            while (progress == runtime::execution::Progress::Runnable) {
                progress = root->advance();
            }
            if (progress == runtime::execution::Progress::Waiting) {
                return std::nullopt;
            }
            auto completion = root->take_completion();
            root.reset();
            complete = true;
            if (completion.is_cancelled()) {
                return std::unexpected(ExecutionFailure(
                    ExecutionCancelled {.origin = origin, .calls = {}, .blocks = {}}
                ));
            }
            if (completion.is_stopped()) {
                return native_error(origin, "standard native operation stopped");
            }
            if (auto* success = completion.success_if()) {
                return encode_native(values, std::move(success->value), result_type, origin);
            }
            return native_error(origin, "standard native operation has no completion");
        } else {
            std::unreachable();
        }
    }

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override {
        if constexpr (Async::asynchronous) {
            return root && root->wait(deadline);
        } else {
            return false;
        }
    }

    auto cancel() noexcept -> void override {
        if constexpr (Async::asynchronous) {
            if (complete) {
                return;
            }
            operation.reset();
            if (root) {
                root->cancel();
                while (root->advance() != runtime::execution::Progress::Complete) {
                }
                root->take_completion();
                root.reset();
            }
            complete = true;
        }
    }

    static auto entry(std::string_view name, NativeOperation kind, bool effects) noexcept
        -> StandardOperation {
        return {
            .name = name,
            .kind = kind,
            .parameters = {NativeParameter {
                .type = &native_type<Value<A>>(),
                .access = std::is_rvalue_reference_v<A> ? AccessMode::Take
                    : (std::is_lvalue_reference_v<A>
                       && !std::is_const_v<std::remove_reference_t<A>>)
                    ? AccessMode::Write
                    : AccessMode::Read
            }...},
            .result = &native_type<typename Async::Value>(),
            .asynchronous = Async::asynchronous,
            .effects = effects,
            .result_is_value =
                !std::is_reference_v<typename Async::Value> && kind != NativeOperation::Field,
            .invoke = call
        };
    }

private:
    const ExecutionValueAccess& values;
    SemanticExecutionContext& context;
    ConstructionTypeRef result_type;
    ProgramOriginID origin;
    bool effects;
    std::tuple<std::optional<typename ArgumentStorage<Value<A>>::Type>...> owned;
    std::tuple<std::optional<Value<A>>...> views;
    std::tuple<std::remove_reference_t<A>*...> inputs;
    std::vector<runtime::hosting::ObjectRef<void>> retained;
    std::optional<std::conditional_t<Async::asynchronous, R, std::monostate>> operation;
    std::optional<typename Async::Root> root;
    bool complete = false;
};

template<auto Function>
auto standard_operation(std::string_view name, bool effects) noexcept -> StandardOperation {
    using Signature = NativeSignature<decltype(Function)>;
    constexpr auto kind = Signature::kind;
    if constexpr (kind == NativeOperation::Method || kind == NativeOperation::Field) {
        name.remove_prefix(name.rfind("::") + 2);
    }
    return StandardActivation<Function, typename Signature::Type>::entry(name, kind, effects);
}

template<typename From, typename To = From>
auto standard_conversion() noexcept -> StandardOperation {
    constexpr auto convert = +[](const From& value) static noexcept -> To {
        return static_cast<To>(value);
    };
    return StandardActivation<convert, To(const From&)>::entry(
        "conversion",
        NativeOperation::Conversion,
        false
    );
}

auto operations() noexcept -> std::span<const StandardOperation> {
#define CARVEN_STANDARD_OPERATION(symbol, effects)                                                 \
    standard_operation<&standard::symbol>("carven::standard::" #symbol, effects)
#define CARVEN_STANDARD_CONVERSION(type) standard_conversion<standard::type>()
    static const auto entries = std::array {
        CARVEN_STANDARD_OPERATION(compiler::compile, true),
        CARVEN_STANDARD_OPERATION(compiler::CompilationResult::success, false),
        CARVEN_STANDARD_OPERATION(compiler::CompilationResult::cacheable, false),
        CARVEN_STANDARD_OPERATION(compiler::CompilationResult::take_error, false),
        CARVEN_STANDARD_OPERATION(compiler::CompilationResult::take_implementations, false),
        CARVEN_STANDARD_OPERATION(compiler::CompilationResult::take_include_dirs, false),
        CARVEN_STANDARD_OPERATION(compiler::CompilationResult::take_native_providers, false),
        CARVEN_STANDARD_OPERATION(compiler::Implementation::file, false),
        CARVEN_STANDARD_OPERATION(compiler::Implementation::source, false),
        CARVEN_STANDARD_OPERATION(compiler::Implementation::dependencies, false),

        CARVEN_STANDARD_OPERATION(build::fingerprint, true),
        CARVEN_STANDARD_OPERATION(build::fingerprints, true),

        CARVEN_STANDARD_OPERATION(collections::hash_bytes, false),

        CARVEN_STANDARD_OPERATION(channel::create, false),
        CARVEN_STANDARD_OPERATION(channel::close, false),
        CARVEN_STANDARD_OPERATION(channel::try_send, false),
        CARVEN_STANDARD_OPERATION(channel::try_receive, false),
        CARVEN_STANDARD_OPERATION(channel::send, false),
        CARVEN_STANDARD_OPERATION(channel::receive, false),
        CARVEN_STANDARD_OPERATION(channel::Result::available, false),
        CARVEN_STANDARD_OPERATION(channel::Result::end, false),
        CARVEN_STANDARD_OPERATION(channel::Result::take_bytes, false),

        CARVEN_STANDARD_OPERATION(fs::read_bytes, true),
        CARVEN_STANDARD_OPERATION(fs::write_bytes, true),
        CARVEN_STANDARD_OPERATION(fs::status, true),
        CARVEN_STANDARD_OPERATION(fs::create_directories, true),
        CARVEN_STANDARD_OPERATION(fs::canonical, true),
        CARVEN_STANDARD_OPERATION(fs::resolve, true),
        CARVEN_STANDARD_OPERATION(fs::parent, false),
        CARVEN_STANDARD_OPERATION(fs::join, false),
        CARVEN_STANDARD_OPERATION(fs::remove, true),
        CARVEN_STANDARD_OPERATION(fs::files, true),
        CARVEN_STANDARD_OPERATION(fs::ReadResult::code, false),
        CARVEN_STANDARD_OPERATION(fs::StatusResult::code, false),
        CARVEN_STANDARD_OPERATION(fs::StatusResult::kind, false),
        CARVEN_STANDARD_OPERATION(fs::RemovalResult::code, false),
        CARVEN_STANDARD_OPERATION(fs::RemovalResult::removed, false),
        CARVEN_STANDARD_OPERATION(fs::PathResult::code, false),
        CARVEN_STANDARD_OPERATION(fs::PathsResult::code, false),
        CARVEN_STANDARD_OPERATION(fs::ReadResult::take_bytes, false),
        CARVEN_STANDARD_OPERATION(fs::PathResult::take_path, false),
        CARVEN_STANDARD_OPERATION(fs::PathsResult::take_paths, false),

        CARVEN_STANDARD_OPERATION(io::write_stdout, true),
        CARVEN_STANDARD_OPERATION(io::write_output, true),

        // Network operations need the host-owned Network event source. The CLI
        // does not own one; only their result accessors are shared here.
        CARVEN_STANDARD_OPERATION(net::ReadResult::error, false),
        CARVEN_STANDARD_OPERATION(net::ReadResult::code, false),
        CARVEN_STANDARD_OPERATION(net::ReadResult::end, false),
        CARVEN_STANDARD_OPERATION(net::WriteResult::count, false),
        CARVEN_STANDARD_OPERATION(net::WriteResult::error, false),
        CARVEN_STANDARD_OPERATION(net::WriteResult::code, false),
        CARVEN_STANDARD_OPERATION(net::ReadResult::take_bytes, false),
        CARVEN_STANDARD_OPERATION(net::WriteResult::take_bytes, false),

        CARVEN_STANDARD_OPERATION(process::spawn, true),
        CARVEN_STANDARD_OPERATION(process::spawn_inherited, true),
        CARVEN_STANDARD_OPERATION(process::parallelism, true),
        CARVEN_STANDARD_OPERATION(process::environment, true),
        CARVEN_STANDARD_OPERATION(process::arguments, true),
        CARVEN_STANDARD_OPERATION(process::executable, true),
        CARVEN_STANDARD_OPERATION(process::find_executable, true),
        CARVEN_STANDARD_OPERATION(process::operating_system, false),
        CARVEN_STANDARD_OPERATION(process::run, true),
        CARVEN_STANDARD_OPERATION(process::group_new, false),
        CARVEN_STANDARD_OPERATION(process::group_spawn, true),
        CARVEN_STANDARD_OPERATION(process::group_wait, true),
        CARVEN_STANDARD_OPERATION(process::read, true),
        CARVEN_STANDARD_OPERATION(process::write, true),
        CARVEN_STANDARD_OPERATION(process::wait, true),
        CARVEN_STANDARD_OPERATION(process::close_input, true),
        CARVEN_STANDARD_OPERATION(process::terminate, true),
        CARVEN_STANDARD_OPERATION(process::SpawnResult::code, false),
        CARVEN_STANDARD_OPERATION(process::Result::code, false),
        CARVEN_STANDARD_OPERATION(process::Result::status, false),
        CARVEN_STANDARD_OPERATION(process::Result::end, false),
        CARVEN_STANDARD_OPERATION(process::RunResult::status, false),
        CARVEN_STANDARD_OPERATION(process::RunResult::code, false),
        CARVEN_STANDARD_OPERATION(process::TextResult::code, false),
        CARVEN_STANDARD_OPERATION(process::ArgumentsResult::code, false),
        CARVEN_STANDARD_OPERATION(process::Exit::index, false),
        CARVEN_STANDARD_OPERATION(process::Exit::status, false),
        CARVEN_STANDARD_OPERATION(process::Exit::code, false),
        CARVEN_STANDARD_OPERATION(process::SpawnResult::take_process, false),
        CARVEN_STANDARD_OPERATION(process::Result::take_bytes, false),
        CARVEN_STANDARD_OPERATION(process::TextResult::take_text, false),
        CARVEN_STANDARD_OPERATION(process::ArgumentsResult::take_arguments, false),

        CARVEN_STANDARD_CONVERSION(channel::Handle),
        CARVEN_STANDARD_CONVERSION(process::Handle),
        CARVEN_STANDARD_CONVERSION(process::Group),
        CARVEN_STANDARD_CONVERSION(net::View),

        standard_conversion<bool>(),
        standard_conversion<std::uint8_t>(),
        standard_conversion<std::int32_t>(),
        standard_conversion<std::size_t>(),
        standard_conversion<runtime::String>(),
        standard_conversion<runtime::Vector<runtime::String>>(),
        standard_conversion<std::int32_t, std::size_t>(),
    };
#undef CARVEN_STANDARD_OPERATION
#undef CARVEN_STANDARD_CONVERSION
    return entries;
}

} // namespace

auto standard_execution_call(
    const ExecutionValueAccess& values,
    SemanticExecutionContext& context,
    const SemanticExpression& expression,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::optional<std::expected<ExecutionHostResult, ExecutionCallFailure>> {
    const auto* operation = expression_operation(values, expression);
    if (operation == nullptr) {
        return std::nullopt;
    }
    if (arguments.size() != operation->parameters.size()) {
        invariant_violation("standard native arguments disagree with checked operation");
    }
    if (operation->effects && !operation->asynchronous) {
        if (const auto rejection = context.host_effects_rejection()) {
            return NativeResult(native_error(origin, std::string(*rejection)));
        }
    }
    return operation->invoke(values, context, expression, arguments, origin, operation->effects);
}

auto standard_execution_supported(
    const ExecutionValueAccess& values,
    const SemanticExpression& expression
) noexcept -> bool {
    return expression_operation(values, expression) != nullptr;
}

auto standard_execution_type(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> bool {
    return native_identity(values, type) != nullptr;
}

auto standard_native_result(const ExecutionValueAccess& values, const CppQueryType& query) noexcept
    -> std::optional<StandardNativeResult> {
    const auto* operation = query_operation(values, query);
    if (operation == nullptr) {
        return std::nullopt;
    }
    return StandardNativeResult {
        .type = operation->result,
        .asynchronous = operation->asynchronous,
        .by_value = operation->result_is_value
    };
}

auto standard_native_result(
    const ExecutionValueAccess& values,
    const GenericCppQueryType& query
) noexcept -> std::optional<StandardNativeResult> {
    const auto* operation = query_operation(values, query);
    if (operation == nullptr) {
        return std::nullopt;
    }
    return StandardNativeResult {
        .type = operation->result,
        .asynchronous = operation->asynchronous,
        .by_value = operation->result_is_value
    };
}

auto standard_native_builtin(const StandardNativeType& type) noexcept
    -> std::optional<BuiltinType> {
    switch (type.kind) {
        case StandardNativeKind::Void:   return BuiltinType::Void;
        case StandardNativeKind::Bool:   return BuiltinType::Bool;
        case StandardNativeKind::String: return BuiltinType::String;
        case StandardNativeKind::Signed:
            switch (type.integer_bits) {
                case 8:  return BuiltinType::I8;
                case 16: return BuiltinType::I16;
                case 32: return BuiltinType::I32;
                case 64: return BuiltinType::I64;
                default: return std::nullopt;
            }
        case StandardNativeKind::Unsigned:
            switch (type.integer_bits) {
                case 8:  return BuiltinType::U8;
                case 16: return BuiltinType::U16;
                case 32: return BuiltinType::U32;
                case 64: return BuiltinType::U64;
                default: return std::nullopt;
            }
        default: return std::nullopt;
    }
}

auto standard_native_result_matches(
    const ExecutionValueAccess& values,
    const StandardNativeResult& result,
    ConstructionTypeRef type,
    ModuleID context
) noexcept -> bool {
    if (!result.by_value) {
        return false;
    }
    const auto matches = [&](this const auto& self,
                             const StandardNativeType& native,
                             ConstructionTypeRef target) noexcept -> bool {
        target = execution_type_reference(values, target);
        if (const auto builtin = standard_native_builtin(native)) {
            return target == ConstructionTypeRef(values.builtin_type(*builtin));
        }
        return visit_execution_type(values, target, [&](const auto& shape) noexcept -> bool {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, VectorTypeValue>
                          || std::same_as<Shape, GenericVectorType>) {
                return native.kind == StandardNativeKind::Vector
                    && self(*native.element, shape.element);
            } else if constexpr (std::same_as<Shape, CppTypeValue>) {
                const auto* named = std::get_if<CppNamedType>(&shape.form);
                return native.kind == StandardNativeKind::Opaque
                    && !native.name.empty()
                    && named != nullptr
                    && named->arguments.empty()
                    && named->name.context_module == context
                    && global_name(named->name) == native.name;
            } else if constexpr (std::same_as<Shape, GenericCppNamedType>) {
                return native.kind == StandardNativeKind::Opaque
                    && !native.name.empty()
                    && shape.arguments.empty()
                    && shape.name.context_module == context
                    && global_name(shape.name) == native.name;
            } else {
                return false;
            }
        });
    };
    if (!result.asynchronous) {
        return matches(*result.type, type);
    }
    return visit_execution_type(values, type, [&](const auto& shape) noexcept -> bool {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, OperationTypeValue>) {
            return values.failure_set_copy(shape.failures).members.empty()
                && matches(*result.type, shape.success);
        } else if constexpr (std::same_as<Shape, ConstructionOperationTypeValue>
                             || std::same_as<Shape, GenericOperationType>) {
            const auto* failures = std::get_if<FailureSetID>(&shape.failures);
            return failures != nullptr
                && values.failure_set_copy(*failures).members.empty()
                && matches(*result.type, shape.success);
        } else {
            return false;
        }
    });
}
