#pragma once

#include <carven/hosting/call.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

template<typename... Arguments>
struct HostArgumentDecoder final {
    using Values = std::tuple<std::remove_cvref_t<Arguments>...>;

    template<typename T>
    static auto value(
        const HostValue& source,
        HostLimits& limits,
        std::span<const HostNativeType> native_types
    ) noexcept -> std::expected<T, HostError> {
        if (!host_registered_value_supported<T>(native_types)) {
            return std::unexpected(
                hosting_error(HostErrorKind::Signature, "native argument type is not registered")
            );
        }
        auto decoded = host_decode_native<T>(source, limits, 0uz);
        if (!decoded) {
            return std::unexpected(std::move(decoded.error()));
        }
        return decoded;
    }

    template<std::size_t... Indices>
    static auto decode(
        std::span<const HostValue> arguments,
        HostLimits limits,
        std::span<const HostNativeType> native_types,
        std::index_sequence<Indices...>
    ) noexcept -> std::expected<Values, HostError> {
        auto decoded = std::tuple {
            value<std::remove_cvref_t<Arguments>>(arguments[Indices], limits, native_types)...
        };
        auto error = std::optional<HostError>();
        [[maybe_unused]] const auto check = [&](auto& argument) noexcept {
            if (!argument && !error) {
                error = std::move(argument.error());
            }
        };
        (check(std::get<Indices>(decoded)), ...);
        if (error) {
            return std::unexpected(std::move(*error));
        }
        return Values(std::move(*std::get<Indices>(decoded))...);
    }

    static auto decode(
        std::span<const HostValue> arguments,
        HostLimits limits,
        std::span<const HostNativeType> native_types = {}
    ) noexcept -> std::expected<Values, HostError> {
        if (arguments.size() != sizeof...(Arguments)) {
            return std::unexpected(hosting_error(
                HostErrorKind::Arguments,
                "call argument count does not match its contract"
            ));
        }
        return decode(arguments, limits, native_types, std::index_sequence_for<Arguments...>());
    }

    static auto decode(
        std::span<const HostValue> arguments,
        const HostCallContract& contract,
        HostLimits limits
    ) noexcept -> std::expected<Values, HostError> {
        return decode(arguments, limits, contract.native_types);
    }

    template<typename T>
    static auto objects_alive(const T& value) noexcept -> bool {
        if constexpr (carven::runtime::hosting::detail::object_reference<T>) {
            return value.alive();
        } else if constexpr (std::same_as<T, std::string>
                             || runtime_native_text<T>
                             || runtime_native_bytes<T>) {
            return true;
        } else if constexpr (runtime_native_vector<T>) {
            return std::ranges::all_of(value, [](const auto& element) static noexcept {
                return objects_alive(element);
            });
        } else if constexpr (runtime_native_enum<T>) {
            auto alive = true;
            const auto found = carven::runtime::hosting::detail::visit_native_enum(
                value,
                [&](std::string_view, const auto& payload) noexcept {
                    alive = std::apply(
                        [](const auto&... item) static noexcept {
                            return (objects_alive(item) && ...);
                        },
                        payload
                    );
                }
            );
            return found && alive;
        } else if constexpr (runtime_native_record<T>) {
            return std::apply(
                [&](const auto&... field) noexcept {
                    return (objects_alive(value.*field.second) && ...);
                },
                carven_fields(static_cast<T*>(nullptr))
            );
        } else {
            return true;
        }
    }

    static auto check_objects(const Values& arguments) noexcept -> std::expected<void, HostError> {
        const auto alive = std::apply(
            [](const auto&... value) static noexcept { return (objects_alive(value) && ...); },
            arguments
        );
        if (!alive) {
            return std::unexpected(
                hosting_error(HostErrorKind::Arguments, "native argument object is no longer alive")
            );
        }
        return {};
    }
};
