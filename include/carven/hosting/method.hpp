#pragma once

#include <carven/hosting/arguments.hpp>
#include <carven/hosting/call.hpp>

#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

template<typename Owner, typename Result, typename... Arguments>
struct HostMethodSignature final {
    using ResultContract = HostCallResult<Result>;
    using Response = typename ResultContract::Response;
    static constexpr auto supported = ResultContract::execution == HostCallExecution::Synchronous
        && HostResponseTraits<Response>::template supported<>
        && (host_call_argument_supported<Arguments> && ...);

    template<auto Method>
    static auto call(
        void* context,
        HostCallContext*,
        std::size_t,
        std::span<const HostCallAccess>,
        bool opaque_result,
        std::span<const HostValue> arguments,
        HostLimits limits,
        std::span<const HostNativeType> native_types
    ) noexcept -> std::expected<HostInvocationResult, HostError> {
        auto decoded = HostArgumentDecoder<Arguments...>::decode(arguments, limits, native_types);
        if (!decoded) {
            return std::unexpected(std::move(decoded.error()));
        }
        auto& object = *static_cast<Owner*>(context);
        auto response = std::apply(
            [&object](auto&... values) noexcept -> Response {
                if constexpr (std::same_as<Result, void>) {
                    std::invoke(Method, object, static_cast<Arguments&&>(values)...);
                    return std::monostate {};
                } else {
                    return ResultContract::response(
                        std::invoke(Method, object, static_cast<Arguments&&>(values)...)
                    );
                }
            },
            *decoded
        );
        auto checked = host_check_native_response(response, limits);
        if (!checked) {
            return std::unexpected(std::move(checked.error()));
        }
        return HostInvocationResult(
            HostResponseTraits<Response>::encode(std::move(response), opaque_result)
        );
    }

    template<auto Method>
    static auto binding(std::string_view name, HostObjectRef object) noexcept -> HostObjectBinding {
        static_assert(Method != nullptr, "host method requires a member function");
        return HostObjectAccess::make(
            {.contract =
                 host_call_contract<Response, Arguments...>(name, HostCallExecution::Synchronous),
             .object = std::move(object),
             .type = host_object_type<Owner>(),
             .call = call<Method>}
        );
    }
};

template<typename T>
struct HostMethodTraits final {};

template<typename Owner, typename Result, typename... Arguments>
struct HostMethodTraits<Result (Owner::*)(Arguments...) noexcept> final {
    using Signature = HostMethodSignature<Owner, Result, Arguments...>;
};

template<typename Owner, typename Result, typename... Arguments>
struct HostMethodTraits<Result (Owner::*)(Arguments...) const noexcept> final {
    using Signature = HostMethodSignature<Owner, Result, Arguments...>;
};

template<auto Method>
auto host_method(std::string_view name, HostObjectRef object) noexcept -> HostObjectBinding {
    using Traits = HostMethodTraits<decltype(Method)>;
    if constexpr (requires { typename Traits::Signature; }) {
        using Signature = typename Traits::Signature;
        static_assert(Signature::supported, "unsupported synchronous host method signature");
        return Signature::template binding<Method>(name, std::move(object));
    } else {
        static_assert(
            false,
            "host methods require an unqualified or const noexcept member function"
        );
    }
}
