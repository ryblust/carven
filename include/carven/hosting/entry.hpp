#pragma once

#include <carven/hosting/arguments.hpp>
#include <carven/hosting/call.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

template<auto Function, typename Result, typename... Arguments>
class HostNativeCall final : public HostCall {
public:
    HostNativeCall(
        std::tuple<std::remove_cvref_t<Arguments>...> arguments,
        HostLimits limits
    ) noexcept
        : arguments(std::move(arguments)),
          limits(limits) {}

    auto advance() noexcept -> HostTaskStatus override {
        auto checked = HostArgumentDecoder<Arguments...>::check_objects(arguments);
        if (!checked) {
            result.emplace(std::unexpected(std::move(checked.error())));
            return HostTaskStatus::Finished;
        }
        if constexpr (std::same_as<Result, void>) {
            std::apply(
                [](auto&... values) static noexcept {
                    std::invoke(Function, static_cast<Arguments&&>(values)...);
                },
                arguments
            );
            result.emplace(HostValue(std::monostate {}));
        } else {
            auto response = std::apply(
                [](auto&... values) static noexcept {
                    return HostCallResult<Result>::response(
                        std::invoke(Function, static_cast<Arguments&&>(values)...)
                    );
                },
                arguments
            );
            auto checked_result = host_check_native_response(response, limits);
            if (!checked_result) {
                checked_result.error().kind = HostErrorKind::Execution;
                result.emplace(std::unexpected(std::move(checked_result.error())));
                return HostTaskStatus::Finished;
            }
            result.emplace(
                HostResponseTraits<decltype(response)>::encode(std::move(response), false)
            );
        }
        return HostTaskStatus::Finished;
    }

    auto cancel() noexcept -> void override {}

    auto take_result() noexcept -> std::expected<HostOutcome, HostError> override {
        return std::move(*result);
    }

private:
    std::tuple<std::remove_cvref_t<Arguments>...> arguments;
    HostLimits limits;
    std::optional<std::expected<HostOutcome, HostError>> result;
};

template<auto Function, typename Result, typename... Failures, typename... Arguments>
class HostNativeCall<
    Function,
    carven::runtime::execution::AsyncOperation<Result, Failures...>,
    Arguments...>
    final : public HostCall {
public:
    HostNativeCall(
        std::tuple<std::remove_cvref_t<Arguments>...> arguments,
        HostLimits limits
    ) noexcept
        : arguments(std::move(arguments)),
          limits(limits) {}

    auto advance() noexcept -> HostTaskStatus override {
        if (!root) {
            auto checked = HostArgumentDecoder<Arguments...>::check_objects(arguments);
            if (!checked) {
                error.emplace(std::move(checked.error()));
                return HostTaskStatus::Finished;
            }
            root.emplace(
                std::apply(
                    [](auto&... values) static noexcept {
                        return std::invoke(Function, static_cast<Arguments&&>(values)...);
                    },
                    arguments
                ),
                nullptr
            );
        }
        const auto progress = root->advance();
        switch (progress) {
            case decltype(progress)::Runnable: return HostTaskStatus::Runnable;
            case decltype(progress)::Waiting:  return HostTaskStatus::Waiting;
            case decltype(progress)::Complete: return HostTaskStatus::Finished;
        }
        std::unreachable();
    }

    auto cancel() noexcept -> void override {
        if (root) {
            root->cancel();
        }
    }

    auto take_result() noexcept -> std::expected<HostOutcome, HostError> override {
        if (error) {
            return std::unexpected(std::move(*error));
        }
        auto completion = root->take_completion();
        if (completion.is_stopped()) {
            return std::unexpected(
                hosting_error(HostErrorKind::Execution, "native execution stopped")
            );
        }
        if (completion.is_cancelled()) {
            return HostCancelled {};
        }
        if (auto* success = completion.success_if()) {
            if constexpr (std::same_as<Result, void>) {
                return HostValue(std::monostate {});
            } else {
                auto checked = host_check_native_response(success->value, limits);
                if (!checked) {
                    checked.error().kind = HostErrorKind::Execution;
                    return std::unexpected(std::move(checked.error()));
                }
                return host_encode_native(std::move(success->value), false);
            }
        }
        auto result = std::optional<std::expected<HostOutcome, HostError>>();
        (
            [&]() noexcept {
                if (const auto* failure = completion.template failure_if<Failures>()) {
                    auto checked = host_check_native_response(*failure, limits);
                    if (!checked) {
                        checked.error().kind = HostErrorKind::Execution;
                        result.emplace(std::unexpected(std::move(checked.error())));
                    } else {
                        result.emplace(host_failure(*failure));
                    }
                }
            }(),
            ...);
        if (!result) {
            std::terminate();
        }
        return std::move(*result);
    }

private:
    std::tuple<std::remove_cvref_t<Arguments>...> arguments;
    HostLimits limits;
    std::optional<HostError> error;
    // Root dies before the arguments borrowed by a cold coroutine.
    std::optional<carven::runtime::execution::Root<Result, Failures...>> root;
};

template<typename Result, typename... Arguments>
struct HostEntrySignature final {
    using ResultContract = HostCallResult<Result>;
    using ArgumentTypes = std::tuple<std::remove_cvref_t<Arguments>...>;
    using Response = typename ResultContract::Response;
    static constexpr auto supported =
        ((!std::is_volatile_v<std::remove_reference_t<Arguments>>
          && (!std::is_reference_v<Arguments>
              || (std::is_lvalue_reference_v<Arguments>
                  && std::is_const_v<std::remove_reference_t<Arguments>>)))
         && ...);

    template<auto Function>
    static auto prepare(
        std::span<const HostValue> arguments,
        const HostCallContract& contract,
        HostLimits limits
    ) noexcept -> std::expected<std::unique_ptr<HostCall>, HostError> {
        if (!contract.source_signature
            && !HostResponseTraits<Response>::supported_by(contract.native_types)) {
            return std::unexpected(hosting_error(
                HostErrorKind::Signature,
                "native entry result type is not registered"
            ));
        }
        auto decoded = HostArgumentDecoder<Arguments...>::decode(arguments, contract, limits);
        if (!decoded) {
            return std::unexpected(std::move(decoded.error()));
        }
        return std::make_unique<HostNativeCall<Function, Result, Arguments...>>(
            std::move(*decoded),
            limits
        );
    }

    template<auto Function>
    static auto binding(HostCallContract contract) noexcept -> HostEntry {
        return HostEntry({.contract = std::move(contract), .prepare = prepare<Function>});
    }

    template<auto Function>
    static auto binding(
        std::string_view name,
        std::span<const HostNativeType> native_types
    ) noexcept -> HostEntry {
        return HostEntry(
            {.contract = host_call_contract<Response, Arguments...>(
                 name,
                 HostCallResult<Result>::execution,
                 native_types
             ),
             .prepare = prepare<Function>}
        );
    }
};

template<typename T>
struct HostEntryTraits final {};

template<typename Result, typename... Arguments>
struct HostEntryTraits<Result (*)(Arguments...) noexcept> final {
    using Signature = HostEntrySignature<Result, Arguments...>;
};

// The callback type determines the complete contract, including async results.
template<auto Function>
auto host_entry(std::string_view name, std::span<const HostNativeType> native_types = {}) noexcept
    -> HostEntry {
    using Traits = HostEntryTraits<decltype(Function)>;
    if constexpr (requires { typename Traits::Signature; }) {
        using Signature = typename Traits::Signature;
        static_assert(Function != nullptr, "native entry requires a function");
        static_assert(Signature::supported, "unsupported native entry signature");
        return Signature::template binding<Function>(name, native_types);
    } else {
        static_assert(false, "native entries require a noexcept free function");
    }
}

// Generated entry contracts retain source access even when C++ uses the same ABI.
template<auto Function, typename Contract>
    requires host_source_contract_supported<Contract>
auto host_entry(Contract service) noexcept -> HostEntry {
    using Traits = HostEntryTraits<decltype(Function)>;
    static_assert(Function != nullptr, "native entry requires a function");
    if constexpr (requires { typename Traits::Signature; }) {
        using Signature = typename Traits::Signature;

        static_assert(
            Signature::supported,
            "native entry parameters require supported read access"
        );

        using Native = typename Signature::ResultContract;
        using Source = HostSourceContract<Contract, typename Contract::Signature>;
        using SourceValue = typename HostResponseTraits<typename Contract::Response>::Value;
        constexpr auto is_unit = []<typename T>() static consteval {
            return std::same_as<T, void>
                || std::same_as<T, std::monostate>
                || std::same_as<T, carven::runtime::Void>;
        };

        static_assert(
            std::same_as<typename Signature::ArgumentTypes, typename Source::Values>,
            "native entry parameters must match the generated source contract"
        );

        static_assert(
            std::same_as<typename Native::Value, SourceValue>
                || (is_unit.template operator()<typename Native::Value>()
                    && is_unit.template operator()<SourceValue>()),
            "native entry result must match the generated source contract"
        );

        static_assert(
            std::same_as<typename Native::FailureTypes, typename Contract::FailureTypes>,
            "native entry failures must match the generated source contract"
        );

        static_assert(
            (Native::execution == HostCallExecution::Async) == Contract::asynchronous,
            "native entry execution must match the generated source contract"
        );

        auto contract = Source::contract(
            service.parameter_accesses,
            service.native_types,
            service.source_signature
        );
        const auto separator = contract.name.rfind('.');
        contract.name.erase(0, separator == std::string::npos ? 0 : separator + 1uz);
        return Signature::template binding<Function>(std::move(contract));
    } else {
        static_assert(false, "native entries require a noexcept free function");
    }
}
