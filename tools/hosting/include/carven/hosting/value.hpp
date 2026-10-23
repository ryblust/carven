#pragma once

#include <carven/hosting/api.hpp>
#include <carven/runtime/call/passing.hpp>
#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/hosting/service.hpp>
#include <carven/runtime/text/string.hpp>
#include <carven/runtime/value/void.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

auto host_maximum_bytes() noexcept -> std::size_t;
auto host_maximum_aggregate_depth() noexcept -> std::size_t;
auto host_maximum_aggregate_elements() noexcept -> std::size_t;

// These adapters consume generated declarations; no native nominal schema is authored here.
using carven::runtime::hosting::detail::runtime_native_bytes;
using carven::runtime::hosting::detail::runtime_native_record;
using carven::runtime::hosting::detail::runtime_native_enum;
using carven::runtime::hosting::detail::NativeEnumArguments;
using carven::runtime::hosting::detail::runtime_native_slice;
using carven::runtime::hosting::detail::runtime_native_text;
using carven::runtime::hosting::detail::runtime_native_vector;

template<typename T>
using HostNativeElement = std::remove_cvref_t<decltype(*std::declval<const T&>().begin())>;

template<typename T>
constexpr auto host_native_integer = std::integral<T>
    && !std::same_as<T, bool>
    && !std::same_as<T, char32_t>
    && sizeof(T) <= sizeof(std::uint64_t);

template<typename T>
using HostNativeIntegerCarrier = std::conditional_t<
    std::same_as<T, std::int32_t> || std::same_as<T, std::uint8_t>,
    T,
    std::conditional_t<std::signed_integral<T>, std::int64_t, std::uint64_t>>;

// Borrowed native arguments use the same decoder as their owned backing. The
// typed call retains that backing until the C++ function has returned.
template<typename T>
constexpr auto host_native_argument_storage() noexcept {
    if constexpr (std::same_as<T, std::string_view>) {
        return std::type_identity<std::string>();
    } else if constexpr (runtime_native_slice<T>) {
        return std::type_identity<carven::runtime::Vector<HostNativeElement<T>>>();
    } else {
        return std::type_identity<T>();
    }
}

template<typename T>
using HostNativeArgumentStorage = typename decltype(host_native_argument_storage<T>())::type;

template<typename T>
auto host_native_type_descriptor() noexcept -> const HostType&;

template<typename Case>
auto host_native_case_payload() noexcept -> std::span<const HostTypeField> {
    using Arguments = NativeEnumArguments<Case>;
    static const auto fields =
        []<std::size_t... Indices>(std::index_sequence<Indices...>) noexcept {
            return std::array<HostTypeField, sizeof...(Indices)> {HostTypeField {
                .name = {},
                .describe = host_native_type_descriptor<
                    std::remove_cvref_t<std::tuple_element_t<Indices, Arguments>>>
            }...};
        }(std::make_index_sequence<std::tuple_size_v<Arguments>>());
    return fields;
}

template<typename T>
auto host_native_type_descriptor() noexcept -> const HostType& {
    static const auto description = []() noexcept -> HostType {
        constexpr auto read_borrows = []() noexcept {
            if constexpr (std::same_as<T, void>) {
                return false;
            } else {
                return std::is_reference_v<carven::runtime::ReadArg<T>>;
            }
        }();
        auto result = HostType {
            .kind = HostTypeKind::Opaque,
            .native_type = host_object_type<T>(),
            .read_borrows = read_borrows,
            .integer_bits = 0,
            .integer_signed = false,
            .name = {},
            .element = nullptr,
            .fields = {},
            .cases = {}
        };
        if constexpr (std::same_as<T, void>
                      || std::same_as<T, carven::runtime::Void>
                      || std::same_as<T, std::monostate>) {
            result.kind = HostTypeKind::Void;
        } else if constexpr (host_native_integer<T>) {
            result.kind = HostTypeKind::Integer;
            result.integer_bits = sizeof(T) * 8uz;
            result.integer_signed = std::signed_integral<T>;
        } else if constexpr (std::same_as<T, bool>) {
            result.kind = HostTypeKind::Boolean;
        } else if constexpr (std::same_as<T, std::string> || runtime_native_text<T>) {
            result.kind = HostTypeKind::String;
        } else if constexpr (std::same_as<T, std::string_view>) {
            result.kind = HostTypeKind::Str;
        } else if constexpr (runtime_native_vector<T> || runtime_native_slice<T>) {
            result.kind = runtime_native_vector<T> ? HostTypeKind::Vector : HostTypeKind::Slice;
            result.element = host_native_type_descriptor<HostNativeElement<T>>;
        } else if constexpr (runtime_native_enum<T>) {
            static const auto cases = std::apply(
                [](const auto&... item) noexcept {
                    return std::array<HostTypeCase, sizeof...(item)> {HostTypeCase {
                        .name = item.first,
                        .payload = host_native_case_payload<std::remove_cvref_t<decltype(item)>>(),
                        .numeric_value = [&]() noexcept -> std::optional<std::uint64_t> {
                            if constexpr (std::is_enum_v<T>) {
                                return static_cast<std::uint64_t>(item.second);
                            } else {
                                return std::nullopt;
                            }
                        }()
                    }...};
                },
                carven_cases(static_cast<T*>(nullptr))
            );
            result.kind = HostTypeKind::Enum;
            if constexpr (std::is_enum_v<T>) {
                result.integer_bits = sizeof(std::underlying_type_t<T>) * 8uz;
                result.integer_signed = std::is_signed_v<std::underlying_type_t<T>>;
            }
            result.name = carven_type_name(static_cast<T*>(nullptr));
            result.cases = cases;
        } else if constexpr (runtime_native_record<T>) {
            static const auto fields = std::apply(
                [](const auto&... field) noexcept {
                    return std::array<HostTypeField, sizeof...(field)> {HostTypeField {
                        .name = field.first,
                        .describe = host_native_type_descriptor<
                            std::remove_cvref_t<decltype(std::declval<T>().*field.second)>>
                    }...};
                },
                carven_fields(static_cast<T*>(nullptr))
            );
            result.kind = HostTypeKind::Record;
            result.name = carven_type_name(static_cast<T*>(nullptr));
            result.fields = fields;
        }
        return result;
    }();
    return description;
}

template<typename T>
auto host_native_nominal_name() noexcept -> std::pair<std::string, std::string> {
    const auto name = carven_type_name(static_cast<T*>(nullptr));
    const auto split = name.rfind('.');
    return {std::string(name.substr(0, split)), std::string(name.substr(split + 1uz))};
}

auto host_native_elements(std::size_t count, HostLimits& budget) noexcept
    -> std::expected<void, HostError>;

auto host_native_text_check(std::string_view text, HostLimits& budget) noexcept
    -> std::expected<void, HostError>;

auto host_native_bytes_check(std::size_t size, HostLimits& budget) noexcept
    -> std::expected<void, HostError>;

auto hosting_valid_text(std::string_view text) noexcept -> bool;

template<typename T>
auto host_check_native_value(const T& value, HostLimits& budget, std::size_t depth) noexcept
    -> std::expected<void, HostError> {
    const auto error = carven::runtime::hosting::detail::native_value_error(
        value,
        budget.byte_work,
        budget.aggregate_work,
        host_maximum_bytes(),
        host_maximum_aggregate_depth(),
        hosting_valid_text,
        depth
    );
    if (!error.empty()) {
        return std::unexpected(hosting_error(HostErrorKind::Arguments, std::string(error)));
    }
    return {};
}

template<typename T>
auto host_encode_native(T value, bool opaque) noexcept -> HostValue {
    if (opaque) {
        return HostValue(HostObjectRef::own(std::move(value)));
    }
    if constexpr (carven::runtime::hosting::detail::object_reference<T>) {
        return HostValue(HostObjectRef::own(std::move(value)));
    } else if constexpr (std::same_as<T, carven::runtime::Void>
                         || std::same_as<T, std::monostate>) {
        return std::monostate {};
    } else if constexpr (std::same_as<T, bool> || std::same_as<T, std::string>) {
        return HostValue(std::move(value));
    } else if constexpr (host_native_integer<T>) {
        return HostValue(static_cast<HostNativeIntegerCarrier<T>>(value));
    } else if constexpr (std::same_as<T, std::string_view>) {
        return std::string(value);
    } else if constexpr (runtime_native_text<T>) {
        return std::move(value).into_utf8();
    } else if constexpr (runtime_native_bytes<T>) {
        if constexpr (runtime_native_slice<T>) {
            return carven::runtime::Vector<std::uint8_t>::from_slice(value);
        } else {
            return HostValue(std::move(value));
        }
    } else if constexpr (runtime_native_vector<T> || runtime_native_slice<T>) {
        auto elements = std::vector<HostValue>();
        elements.reserve(value.size());
        // Owned vectors transfer their elements; borrowed slices copy them.
        // NOLINTNEXTLINE(misc-const-correctness)
        for (auto& element : value) {
            if constexpr (runtime_native_slice<T>) {
                elements.push_back(host_encode_native(element, false));
            } else {
                elements.push_back(host_encode_native(std::move(element), false));
            }
        }
        return elements;
    } else if constexpr (runtime_native_enum<T>) {
        auto [module_path, name] = host_native_nominal_name<T>();
        auto result = HostEnum {
            .module_path = std::move(module_path),
            .name = std::move(name),
            .case_name = {},
            .payload = {}
        };
        carven::runtime::hosting::detail::visit_native_enum(
            std::move(value),
            [&](std::string_view name, auto&& payload) noexcept {
                result.case_name = name;
                std::apply(
                    [&](auto&&... item) noexcept {
                        (result.payload.push_back(
                             host_encode_native(std::forward<decltype(item)>(item), false)
                         ),
                         ...);
                    },
                    std::forward<decltype(payload)>(payload)
                );
            }
        );
        return result;
    } else if constexpr (runtime_native_record<T>) {
        auto [module_path, name] = host_native_nominal_name<T>();
        auto record = HostRecord {
            .module_path = std::move(module_path),
            .name = std::move(name),
            .fields = {}
        };
        std::apply(
            [&](const auto&... field) noexcept {
                (record.fields.push_back(
                     {.name = std::string(field.first),
                      .value = host_encode_native(std::move(value.*field.second), false)}
                 ),
                 ...);
            },
            carven_fields(static_cast<T*>(nullptr))
        );
        return record;
    } else {
        return HostValue(HostObjectRef::own(std::move(value)));
    }
}

template<typename Types, typename Read, typename Build, std::size_t... Indices>
auto host_native_construct_values(Read read, Build build, std::index_sequence<Indices...>) noexcept
    -> std::
        expected<std::invoke_result_t<Build, std::tuple_element_t<Indices, Types>...>, HostError> {
    // Nonempty tuples transfer their decoded values and errors.
    // NOLINTNEXTLINE(misc-const-correctness)
    auto values = std::tuple {
        read.template operator()<std::remove_cvref_t<std::tuple_element_t<Indices, Types>>>(
            std::integral_constant<std::size_t, Indices> {}
        )...
    };
    auto error = std::optional<HostError>();
    std::apply(
        [&](auto&... value) noexcept {
            (
                [&]() noexcept {
                    if (!value && !error) {
                        error = std::move(value.error());
                    }
                }(),
                ...);
        },
        values
    );
    if (error) {
        return std::unexpected(std::move(*error));
    }
    return std::apply([&](auto&... value) noexcept { return build(std::move(*value)...); }, values);
}

template<typename T, typename Read, typename Fields, std::size_t... Indices>
auto host_native_construct(
    Read read,
    const Fields& fields,
    std::index_sequence<Indices...> indices
) noexcept -> std::expected<T, HostError> {
    using Types = std::tuple<
        std::remove_cvref_t<decltype(std::declval<T>().*std::get<Indices>(fields).second)>...>;
    return host_native_construct_values<Types>(
        read,
        [](auto&&... value) static noexcept -> T {
            return T {std::forward<decltype(value)>(value)...};
        },
        indices
    );
}

template<typename T>
auto host_decode_native(const HostValue& source, HostLimits& budget, std::size_t depth) noexcept
    -> std::expected<T, HostError> {
    if (depth > host_maximum_aggregate_depth()) {
        return std::unexpected(
            hosting_error(HostErrorKind::Arguments, "native value exceeds its nesting limit")
        );
    }

    const auto mismatch = []() static noexcept {
        return std::unexpected(hosting_error(
            HostErrorKind::Arguments,
            "value does not match its generated native type"
        ));
    };
    if (const auto* box = std::get_if<HostObjectRef>(&source)) {
        if (const auto* value = box->template get<T>()) {
            auto checked = host_check_native_value(*value, budget, depth);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return *value;
        }
    }
    if constexpr (std::same_as<T, carven::runtime::Void> || std::same_as<T, std::monostate>) {
        if (std::holds_alternative<std::monostate>(source)) {
            return T {};
        }
    } else if constexpr (host_native_integer<T>) {
        if (const auto* value = std::get_if<HostNativeIntegerCarrier<T>>(&source)) {
            if (*value >= std::numeric_limits<T>::min()
                && *value <= std::numeric_limits<T>::max()) {
                return static_cast<T>(*value);
            }
        }
    } else if constexpr (std::same_as<T, bool> || std::same_as<T, std::string>) {
        if (const auto* value = std::get_if<T>(&source)) {
            auto checked = host_check_native_value(*value, budget, depth);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return *value;
        }
    } else if constexpr (runtime_native_text<T>) {
        if (const auto* text = std::get_if<std::string>(&source)) {
            auto checked = host_native_text_check(*text, budget);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return T::from_str(*text);
        }
    } else if constexpr (std::same_as<T, carven::runtime::Vector<std::uint8_t>>) {
        if (const auto* bytes = std::get_if<T>(&source)) {
            auto checked = host_check_native_value(*bytes, budget, depth);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return *bytes;
        }
    } else if constexpr (runtime_native_vector<T>) {
        if (const auto* values = std::get_if<std::vector<HostValue>>(&source)) {
            auto checked = host_native_elements(values->size(), budget);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            auto result = T();
            for (const auto& value : *values) {
                auto decoded = host_decode_native<HostNativeElement<T>>(value, budget, depth + 1uz);
                if (!decoded) {
                    return std::unexpected(std::move(decoded.error()));
                }
                result.push(std::move(*decoded));
            }
            return result;
        }
    } else if constexpr (runtime_native_enum<T>) {
        if (const auto* value = std::get_if<HostEnum>(&source)) {
            const auto [module_path, name] = host_native_nominal_name<T>();
            if (value->module_path != module_path || value->name != name) {
                return mismatch();
            }
            auto result = std::optional<std::expected<T, HostError>>();
            const auto cases = carven_cases(static_cast<T*>(nullptr));
            std::apply(
                [&](const auto&... item) noexcept {
                    (
                        [&]() noexcept {
                            if (result || item.first != value->case_name) {
                                return;
                            }
                            using Arguments =
                                NativeEnumArguments<std::remove_cvref_t<decltype(item)>>;
                            if (value->payload.size() != std::tuple_size_v<Arguments>) {
                                result.emplace(mismatch());
                                return;
                            }
                            auto checked = host_native_elements(value->payload.size(), budget);
                            if (!checked) {
                                result.emplace(std::unexpected(std::move(checked.error())));
                                return;
                            }
                            result.emplace(
                                host_native_construct_values<Arguments>(
                                    [&]<typename Payload>(auto index) noexcept {
                                        return host_decode_native<Payload>(
                                            value->payload[index],
                                            budget,
                                            depth + 1uz
                                        );
                                    },
                                    [&](auto&&... payload) noexcept -> T {
                                        if constexpr (std::is_enum_v<T>) {
                                            return item.second;
                                        } else {
                                            return std::invoke(
                                                item.second,
                                                std::forward<decltype(payload)>(payload)...
                                            );
                                        }
                                    },
                                    std::make_index_sequence<std::tuple_size_v<Arguments>>()
                                )
                            );
                        }(),
                        ...);
                },
                cases
            );
            if (result) {
                return std::move(*result);
            }
        }
    } else if constexpr (runtime_native_record<T>) {
        if (const auto* record = std::get_if<HostRecord>(&source)) {
            const auto fields = carven_fields(static_cast<T*>(nullptr));
            const auto [module_path, name] = host_native_nominal_name<T>();
            if (record->module_path != module_path
                || record->name != name
                || record->fields.size() != std::tuple_size_v<decltype(fields)>) {
                return mismatch();
            }
            auto checked = host_native_elements(record->fields.size(), budget);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return host_native_construct<T>(
                [&]<typename Value>(auto index) noexcept -> std::expected<Value, HostError> {
                    const auto field_name = std::get<decltype(index)::value>(fields).first;
                    if (record->fields[index].name != field_name) {
                        return mismatch();
                    }
                    return host_decode_native<Value>(
                        record->fields[index].value,
                        budget,
                        depth + 1uz
                    );
                },
                fields,
                std::make_index_sequence<std::tuple_size_v<decltype(fields)>>()
            );
        }
    }
    return mismatch();
}
