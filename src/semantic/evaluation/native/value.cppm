module carven:semantic.evaluation.native.value;

import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import :semantic.evaluation.runtime;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.type;
import :semantic.semir.values;
import :support.utf8;
import std;

auto execution_native_error(ProgramOriginID origin, std::string message) noexcept
    -> std::unexpected<ExecutionCallFailure>;

template<typename T>
struct ExecutionNativeSequence final {
    static constexpr auto enabled = false;
    static constexpr auto borrowed = false;
};

template<typename T>
struct ExecutionNativeSequence<carven::runtime::Vector<T>> final {
    using Element = T;
    static constexpr auto enabled = true;
    static constexpr auto borrowed = false;
};

template<typename T>
struct ExecutionNativeSequence<carven::runtime::Slice<T>> final {
    using Element = T;
    static constexpr auto enabled = true;
    static constexpr auto borrowed = true;
};

template<typename T>
constexpr auto execution_native_storage() noexcept {
    if constexpr (std::same_as<T, std::string_view>) {
        return std::type_identity<std::string>();
    } else if constexpr (ExecutionNativeSequence<T>::enabled) {
        return std::type_identity<
            carven::runtime::Vector<typename ExecutionNativeSequence<T>::Element>>();
    } else {
        return std::type_identity<T>();
    }
}

template<typename T>
using ExecutionNativeStorage = typename decltype(execution_native_storage<T>())::type;

// Converts execution storage directly. Opaque objects retain their exact C++ identity;
// source aggregates are observed through the existing execution value readers.
class ExecutionNativeValue final {
public:
    static auto element_type(const ExecutionValueAccess& values, TypeID type) noexcept
        -> std::optional<TypeID>;

    static auto charge_bytes(std::size_t size, ExecutionLimits& budget) noexcept
        -> std::expected<void, std::string>;

    static auto charge_elements(std::size_t size, ExecutionLimits& budget) noexcept
        -> std::expected<void, std::string>;

    template<typename T>
    static auto matches(const ExecutionValueAccess& values, TypeID type) noexcept -> bool {
        using Value = std::remove_cvref_t<T>;
        const auto shape = values.type_copy(type);
        if (std::holds_alternative<CppTypeValue>(shape.value)) {
            return true;
        }
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&shape.value)) {
            if constexpr (std::same_as<Value, void> || std::same_as<Value, std::monostate>) {
                return builtin->kind == BuiltinType::Void;
            } else if constexpr (std::same_as<Value, bool>) {
                return builtin->kind == BuiltinType::Bool;
            } else if constexpr (std::integral<Value>) {
                return builtin_is_integer(builtin->kind)
                    && builtin_is_signed_integer(builtin->kind) == std::signed_integral<Value>
                    && builtin_integer_width(builtin->kind) == sizeof(Value) * 8uz;
            } else if constexpr (std::same_as<Value, std::string_view>) {
                return builtin->kind == BuiltinType::Str;
            } else if constexpr (std::same_as<Value, std::string>
                                 || std::same_as<Value, carven::runtime::String>) {
                return builtin->kind == BuiltinType::String;
            }
        }
        if constexpr (ExecutionNativeSequence<Value>::enabled) {
            const auto* vector = std::get_if<VectorTypeValue>(&shape.value);
            const auto* slice = std::get_if<SliceTypeValue>(&shape.value);
            if (ExecutionNativeSequence<Value>::borrowed ? slice == nullptr : vector == nullptr) {
                return false;
            }
            return matches<typename ExecutionNativeSequence<Value>::Element>(
                values,
                vector ? vector->element : slice->element
            );
        }
        return false;
    }

    template<typename T>
    static auto decode(
        const ExecutionValueAccess& values,
        const ExecutionValue& source,
        ExecutionLimits& budget,
        std::size_t depth
    ) noexcept -> std::expected<ExecutionNativeStorage<T>, std::string> {
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected("native value exceeds its nesting limit");
        }
        if constexpr (std::same_as<T, bool> || std::integral<T>) {
            const auto atom = execution_atom(values, source);
            if (atom) {
                if constexpr (std::same_as<T, bool>) {
                    if (const auto* boolean = std::get_if<BooleanConstant>(&atom->value)) {
                        return boolean->value;
                    }
                } else if (const auto* integer = std::get_if<IntegerConstant>(&atom->value)) {
                    if constexpr (std::signed_integral<T>) {
                        const auto number = integer->as_signed();
                        if (number && std::in_range<T>(*number)) {
                            return static_cast<T>(*number);
                        }
                    } else {
                        const auto number = integer->as_unsigned();
                        if (number && std::in_range<T>(*number)) {
                            return static_cast<T>(*number);
                        }
                    }
                }
            }
        } else if constexpr (std::same_as<T, std::string_view>
                             || std::same_as<T, std::string>
                             || std::same_as<T, carven::runtime::String>) {
            const auto text = execution_text(values, source);
            if (text) {
                const auto checked = charge_bytes(text->view().size(), budget);
                if (!checked) {
                    return std::unexpected(checked.error());
                }
                if constexpr (std::same_as<T, carven::runtime::String>) {
                    return carven::runtime::String::from_utf8(std::string(text->view()));
                } else {
                    return std::string(text->view());
                }
            }
        } else if constexpr (ExecutionNativeSequence<T>::enabled) {
            using Element = typename ExecutionNativeSequence<T>::Element;
            const auto view = execution_compound_view(values, source);
            if (view) {
                const auto checked = std::same_as<Element, std::uint8_t>
                    ? charge_bytes(view->size(), budget)
                    : charge_elements(view->size(), budget);
                if (!checked) {
                    return std::unexpected(checked.error());
                }
                if (view->size() != 0uz && depth == maximum_constant_aggregate_depth) {
                    return std::unexpected("native value exceeds its nesting limit");
                }
                return view->elements.visit(
                    [&](
                        const auto& items
                    ) noexcept -> std::expected<ExecutionNativeStorage<T>, std::string> {
                        if constexpr (std::same_as<Element, std::uint8_t>
                                      && std::same_as<
                                          std::remove_cvref_t<decltype(items)>,
                                          ExecutionByteView>) {
                            return carven::runtime::Vector<Element>::from_slice(
                                carven::runtime::Slice<Element>(std::span<const Element>(
                                    reinterpret_cast<const Element*>(items.bytes.data()),
                                    items.size()
                                ))
                            );
                        } else {
                            auto result = carven::runtime::Vector<Element>();
                            result.reserve(items.size());
                            for (auto index = 0uz; index < items.size(); ++index) {
                                auto item = [&]() noexcept {
                                    if constexpr (std::same_as<
                                                      std::remove_cvref_t<decltype(items[index])>,
                                                      ConstantID>) {
                                        return decode<Element>(
                                            values,
                                            ExecutionValue(items[index]),
                                            budget,
                                            depth + 1uz
                                        );
                                    } else {
                                        return decode<Element>(
                                            values,
                                            items[index],
                                            budget,
                                            depth + 1uz
                                        );
                                    }
                                }();
                                if (!item) {
                                    return std::unexpected(std::move(item.error()));
                                }
                                result.push(std::move(*item));
                            }
                            return result;
                        }
                    }
                );
            }
        }
        return std::unexpected("native argument does not match its C++ carrier");
    }

    template<typename T>
    static auto encode(
        const ExecutionValueAccess& values,
        T&& source,
        TypeID target,
        ExecutionLimits& budget,
        std::size_t depth
    ) noexcept -> std::expected<ExecutionValue, std::string> {
        using Value = std::remove_cvref_t<T>;
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected("native value exceeds its nesting limit");
        }
        if (std::holds_alternative<CppTypeValue>(values.type_copy(target).value)) {
            constexpr auto borrows = std::is_reference_v<carven::runtime::ReadArg<Value>>;
            return ExecutionHostObject(
                target,
                carven::runtime::hosting::ObjectRef<void>::own(std::forward<T>(source)),
                borrows
            );
        }
        if constexpr (std::same_as<Value, std::monostate>) {
            return ExecutionVoid {};
        } else if constexpr (std::same_as<Value, bool>) {
            return ConstantAtom {.type = target, .value = BooleanConstant {.value = source}};
        } else if constexpr (std::integral<Value>) {
            const auto number = [&]() noexcept {
                if constexpr (std::signed_integral<Value>) {
                    return IntegerConstant::from_signed(source);
                } else {
                    return IntegerConstant::from_parts(source, false);
                }
            }();
            const auto shape = values.type_copy(target);
            const auto* builtin = std::get_if<BuiltinTypeValue>(&shape.value);
            if (builtin && integer_constant_fits(number, builtin->kind)) {
                return ConstantAtom {.type = target, .value = number};
            }
        } else if constexpr (std::same_as<Value, std::string_view>
                             || std::same_as<Value, std::string>
                             || std::same_as<Value, carven::runtime::String>) {
            auto bytes = [&]() noexcept -> std::string {
                if constexpr (std::same_as<Value, carven::runtime::String>) {
                    if constexpr (std::is_rvalue_reference_v<T&&>
                                  && !std::is_const_v<std::remove_reference_t<T>>) {
                        return std::move(source).into_utf8();
                    } else {
                        return std::string(source.as_str());
                    }
                } else {
                    return std::string(std::forward<T>(source));
                }
            }();
            const auto checked = charge_bytes(bytes.size(), budget);
            if (!checked) {
                return std::unexpected(checked.error());
            }
            if (!UTF8Decoder::is_valid(bytes)) {
                return std::unexpected("native result is not valid UTF-8");
            }
            if (target == values.builtin_type(BuiltinType::Str)) {
                return ExecutionText(std::move(bytes));
            }
            return ExecutionOwnedText(std::move(bytes));
        } else if constexpr (ExecutionNativeSequence<Value>::enabled) {
            using Element = typename ExecutionNativeSequence<Value>::Element;
            const auto element = element_type(values, target);
            if (!element) {
                return std::unexpected("native sequence has no source element type");
            }
            const auto checked = std::same_as<Element, std::uint8_t>
                ? charge_bytes(source.size(), budget)
                : charge_elements(source.size(), budget);
            if (!checked) {
                return std::unexpected(checked.error());
            }
            if constexpr (std::same_as<Element, std::uint8_t>) {
                auto bytes = [&]() noexcept {
                    if constexpr (ExecutionNativeSequence<Value>::borrowed) {
                        return carven::runtime::Vector<Element>::from_slice(source);
                    } else {
                        return std::forward<T>(source);
                    }
                }();
                return ExecutionBytes {
                    .type = target,
                    .element = *element,
                    .bytes = std::move(bytes)
                };
            } else {
                auto items = std::vector<ExecutionValue>();
                items.reserve(source.size());
                for (auto& item : source) {
                    auto value =
                        encode(values, std::forward_like<T>(item), *element, budget, depth + 1uz);
                    if (!value) {
                        return std::unexpected(std::move(value.error()));
                    }
                    items.push_back(std::move(*value));
                }
                return ExecutionAggregateValue {
                    .type = target,
                    .elements = std::move(items),
                    .capacity = source.size()
                };
            }
        }
        return std::unexpected("native result does not match its source type");
    }
};
