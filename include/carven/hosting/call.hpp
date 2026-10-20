#pragma once

#include <carven/hosting/value.hpp>
#include <carven/runtime/call/passing.hpp>
#include <carven/runtime/value/outcome.hpp>

#include <algorithm>
#include <concepts>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

template<typename T>
constexpr auto host_scalar_value_supported =
    host_native_integer<T> || std::same_as<T, bool> || std::same_as<T, std::string>;

template<typename T, typename... Seen, typename Native>
constexpr auto host_value_supported(Native native) noexcept -> bool {
    if constexpr ((std::same_as<T, Seen> || ...)) {
        return true;
    } else {
        if (native.template operator()<T>()) {
            return std::is_copy_constructible_v<T>;
        }
        if constexpr (host_scalar_value_supported<T>
                      || runtime_native_text<T>
                      || std::same_as<T, carven::runtime::Void>
                      || std::same_as<T, std::monostate>) {
            return true;
        } else if constexpr (runtime_native_vector<T>) {
            return host_value_supported<HostNativeElement<T>, Seen..., T>(native);
        } else if constexpr (runtime_native_enum<T>) {
            return std::apply(
                [&](const auto&... item) noexcept {
                    return (
                        ([]<typename... Arguments>(
                             std::type_identity<std::tuple<Arguments...>>,
                             Native native
                         ) constexpr {
                            return (
                                host_value_supported<std::remove_cvref_t<Arguments>, Seen..., T>(
                                    native
                                )
                                && ...
                            );
                        }(std::type_identity<
                              NativeEnumArguments<std::remove_cvref_t<decltype(item)>>> {},
                          native))
                        && ...
                    );
                },
                carven_cases(static_cast<T*>(nullptr))
            );
        } else if constexpr (runtime_native_record<T>) {
            return std::apply(
                [&](const auto&... field) noexcept {
                    return (
                        host_value_supported<
                            std::remove_cvref_t<decltype(std::declval<T>().*field.second)>,
                            Seen...,
                            T>(native)
                        && ...
                    );
                },
                carven_fields(static_cast<T*>(nullptr))
            );
        } else {
            return false;
        }
    }
}

template<typename T, typename NativeTypes = std::tuple<>>
constexpr auto host_call_value_supported =
    host_value_supported<T>([]<typename Value>() static constexpr {
        return carven::runtime::hosting::detail::native_type_registered<Value, NativeTypes>();
    });

template<typename T>
auto host_registered_value_supported(std::span<const HostNativeType> types) noexcept -> bool {
    return host_value_supported<T>([types]<typename Value>() noexcept {
        return std::ranges::any_of(types, [](const auto& type) static noexcept {
            return type.type == host_object_type<Value>();
        });
    });
}

template<typename T>
constexpr auto host_call_argument_supported = host_call_value_supported<std::remove_cvref_t<T>>
    && !std::is_volatile_v<std::remove_reference_t<T>>
    && (!std::is_reference_v<T>
        || (std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>>));

template<typename T>
auto host_call_type() noexcept -> HostCallType {
    return {.describe = host_native_type_descriptor<T>, .allow_cpp = false};
}

template<typename Failure>
auto host_failure_type() noexcept -> HostFailureType {
    static_assert(runtime_native_record<Failure> || runtime_native_enum<Failure>);
    auto [module_path, name] = host_native_nominal_name<Failure>();
    return {
        .module_path = std::move(module_path),
        .name = std::move(name),
        .type = host_call_type<Failure>()
    };
}

template<typename Failure>
auto host_failure(Failure failure) noexcept -> HostFailure {
    static_assert(runtime_native_record<Failure> || runtime_native_enum<Failure>);
    auto value = host_encode_native(std::move(failure), false);
    if constexpr (runtime_native_enum<Failure>) {
        return HostFailure(std::get<HostEnum>(std::move(value)));
    } else {
        return HostFailure(std::get<HostRecord>(std::move(value)));
    }
}

template<typename Result>
struct HostResponseTraits final {
    using Value = Result;
    template<typename NativeTypes = std::tuple<>>
    static constexpr auto supported =
        std::same_as<Result, void> || host_call_value_supported<Result, NativeTypes>;

    static auto encode(
        std::conditional_t<std::same_as<Result, void>, std::monostate, Result> result,
        bool opaque
    ) noexcept -> HostOutcome {
        return host_encode_native(std::move(result), opaque);
    }

    template<typename Response, typename Success, typename Failure>
    static auto visit(Response& response, Success success, Failure) noexcept
        -> std::invoke_result_t<Success, Response&&> {
        return success(std::move(response));
    }

    static auto failures() noexcept -> std::vector<HostFailureType> { return {}; }

    static auto stopped(
        const std::conditional_t<std::same_as<Result, void>, std::monostate, Result>&
    ) noexcept -> bool {
        return false;
    }

    static auto supported_by(std::span<const HostNativeType> types) noexcept -> bool {
        if constexpr (std::same_as<Result, void>) {
            return true;
        } else {
            return host_registered_value_supported<Result>(types);
        }
    }
};

template<typename Result, typename... Failures>
struct HostResponseTraits<carven::runtime::Outcome<Result, Failures...>> final {
    using Value = Result;
    template<typename NativeTypes = std::tuple<>>
    static constexpr auto supported = HostResponseTraits<Result>::template supported<NativeTypes>
        && (((runtime_native_record<Failures> || runtime_native_enum<Failures>)
             && host_call_value_supported<Failures, NativeTypes>)
            && ...);

    template<typename Response, typename Success, typename Failure>
    static auto visit(Response& response, Success success, Failure failure) noexcept
        -> std::invoke_result_t<
            Success,
            std::conditional_t<std::same_as<Result, void>, std::monostate, Result>&&> {
        if (auto* value = response.success_if()) {
            if constexpr (std::same_as<Result, void>) {
                return success(std::monostate {});
            } else {
                return success(std::move(value->value));
            }
        }
        using Returned = std::invoke_result_t<
            Success,
            std::conditional_t<std::same_as<Result, void>, std::monostate, Result>&&>;

        auto returned = std::optional<Returned>();
        (
            [&]() noexcept {
                if (auto* value = response.template failure_if<Failures>()) {
                    returned.emplace(failure(std::move(*value)));
                }
            }(),
            ...);
        if (!returned) {
            std::terminate();
        }
        return std::move(*returned);
    }

    static auto encode(carven::runtime::Outcome<Result, Failures...> response, bool opaque) noexcept
        -> HostOutcome {
        return visit(
            response,
            [opaque](auto&& value) noexcept -> HostOutcome {
                return host_encode_native(std::move(value), opaque);
            },
            [](auto&& value) static noexcept -> HostOutcome {
                return host_failure(std::move(value));
            }
        );
    }

    static auto failures() noexcept -> std::vector<HostFailureType> {
        return {host_failure_type<Failures>()...};
    }

    static auto stopped(const carven::runtime::Outcome<Result, Failures...>& response) noexcept
        -> bool {
        return response.is_stopped();
    }

    static auto supported_by(std::span<const HostNativeType> types) noexcept -> bool {
        return HostResponseTraits<Result>::supported_by(types)
            && (host_registered_value_supported<Failures>(types) && ...);
    }
};

template<typename Result, typename... Arguments>
auto host_call_contract(
    std::string_view name,
    HostCallExecution execution,
    std::span<const HostNativeType> native_types = {}
) noexcept -> HostCallContract {
    using Response = HostResponseTraits<Result>;
    return {
        .name = std::string(name),
        .parameters = {HostCallParameter {
            .type = host_call_type<std::remove_cvref_t<Arguments>>(),
            .access = HostCallAccess::Read
        }...},
        .result = host_call_type<typename Response::Value>(),
        .execution = execution,
        .failures = Response::failures(),
        .native_types = {native_types.begin(), native_types.end()},
        .source_signature = {},
        .cpp_operation = std::nullopt,
    };
}

template<typename Response>
auto host_check_native_response(const Response& response, HostLimits limits) noexcept
    -> std::expected<void, HostError> {
    if (HostResponseTraits<Response>::stopped(response)) {
        return std::unexpected(hosting_error(HostErrorKind::Execution, "native execution stopped"));
    }
    limits.aggregate_work = std::min(limits.aggregate_work, host_maximum_aggregate_elements());
    const auto check = [&](const auto& value) noexcept -> std::expected<void, HostError> {
        return host_check_native_value(value, limits, 0uz);
    };
    return HostResponseTraits<Response>::visit(response, check, check);
}

template<typename T>
auto host_native_type(std::string_view name) noexcept -> HostNativeType {
    static_assert(
        std::is_copy_constructible_v<T>,
        "interpreted native values require copy construction"
    );
    return {
        .name = std::string(name),
        .type = host_object_type<T>(),
        .read_borrows = std::is_reference_v<carven::runtime::ReadArg<T>>,
        .encode = [](const HostObjectRef& box,
                     HostLimits& budget,
                     std::size_t depth) static noexcept -> std::expected<HostValue, HostError> {
            const auto* value = box.template get<T>();
            if (value == nullptr) {
                return std::unexpected(
                    hosting_error(HostErrorKind::Execution, "native value has the wrong carrier")
                );
            }
            auto checked = host_check_native_value(*value, budget, depth);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            return host_encode_native(*value, false);
        },
        .decode = [](const HostValue& value,
                     HostLimits& budget,
                     std::size_t depth) static noexcept -> std::expected<HostObjectRef, HostError> {
            auto decoded = host_decode_native<T>(value, budget, depth);
            if (!decoded) {
                return std::unexpected(std::move(decoded.error()));
            }
            return HostObjectRef::own(std::move(*decoded));
        }
    };
}

template<typename NativeTypes>
auto host_native_types(const NativeTypes& types) noexcept -> std::vector<HostNativeType> {
    return std::apply(
        [](const auto&... native) static noexcept {
            return std::vector<HostNativeType> {
                host_native_type<typename std::remove_cvref_t<decltype(native.second)>::type>(
                    native.first
                )...
            };
        },
        types
    );
}

// Definitions are needed only when an async entry is instantiated.
namespace carven::runtime::execution {

template<typename Result, typename... Failures>
class AsyncOperation;

template<typename Result, typename... Failures>
class Root;

} // namespace carven::runtime::execution

template<typename Result>
struct HostCallResult final {
    using Value = Result;
    using Root = std::monostate;
    using Response = std::conditional_t<std::same_as<Result, void>, std::monostate, Result>;
    using FailureTypes = std::tuple<>;
    static constexpr auto execution = HostCallExecution::Synchronous;

    template<typename T>
    static auto response(T value) noexcept -> Response {
        return value;
    }
};

template<typename Result, typename... Failures>
struct HostCallResult<carven::runtime::Outcome<Result, Failures...>> final {
    using Value = Result;
    using Root = std::monostate;
    using Response = carven::runtime::Outcome<Result, Failures...>;
    using FailureTypes = std::tuple<Failures...>;
    static constexpr auto execution = HostCallExecution::Synchronous;

    static auto response(Response value) noexcept -> Response { return value; }
};

template<typename Result, typename... Failures>
struct HostCallResult<carven::runtime::execution::AsyncOperation<Result, Failures...>> final {
    using Value = Result;
    using Root = carven::runtime::execution::Root<Result, Failures...>;
    using StoredValue = std::conditional_t<std::same_as<Result, void>, std::monostate, Result>;

    using Response = std::conditional_t<
        sizeof...(Failures) == 0,
        StoredValue,
        carven::runtime::Outcome<Result, Failures...>>;

    using FailureTypes = std::tuple<Failures...>;
    static constexpr auto execution = HostCallExecution::Async;
};

template<typename Contract, typename Signature>
struct HostSourceContract;

template<typename Contract, typename Result, typename... Arguments>
struct HostSourceContract<Contract, Result(Arguments...)> final {
    using Response = typename Contract::Response;
    using Values = std::tuple<std::remove_cvref_t<Arguments>...>;

    static auto contract(
        std::span<const carven::runtime::hosting::ParameterAccess> accesses,
        const typename Contract::NativeTypes& types,
        std::string_view source_signature
    ) noexcept -> HostCallContract {
        static_assert(
            HostResponseTraits<Response>::template supported<typename Contract::NativeTypes>
        );

        static_assert(
            (host_call_value_supported<
                 std::remove_cvref_t<Arguments>,
                 typename Contract::NativeTypes>
             && ...)
        );

        const auto native = host_native_types(types);
        auto result = host_call_contract<Response, Arguments...>(
            Contract::name,
            Contract::asynchronous ? HostCallExecution::Async : HostCallExecution::Synchronous,
            native
        );
        result.source_signature = std::string(source_signature);
        for (auto index = 0uz; index < accesses.size(); ++index) {
            switch (accesses[index]) {
                case carven::runtime::hosting::ParameterAccess::Read:
                    result.parameters[index].access = HostCallAccess::Read;
                    break;
                case carven::runtime::hosting::ParameterAccess::Write:
                    result.parameters[index].access = HostCallAccess::Write;
                    break;
                case carven::runtime::hosting::ParameterAccess::Take:
                    result.parameters[index].access = HostCallAccess::Take;
                    break;
            }
        }
        return result;
    }
};

template<typename Contract>
constexpr auto host_source_contract_supported = false;

template<
    carven::runtime::hosting::ServiceName Name,
    typename Signature,
    typename NativeTypes,
    typename... Failures>
constexpr auto host_source_contract_supported<
    carven::runtime::hosting::Service<Name, Signature, NativeTypes, Failures...>> = true;

template<
    carven::runtime::hosting::ServiceName Name,
    typename Signature,
    typename NativeTypes,
    typename... Failures>
constexpr auto host_source_contract_supported<
    carven::runtime::hosting::AsyncService<Name, Signature, NativeTypes, Failures...>> = true;
