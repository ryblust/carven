#pragma once

#include <carven/hosting/arguments.hpp>
#include <carven/hosting/call.hpp>
#include <carven/hosting/request.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

template<typename Contract, typename Result, typename... Arguments>
struct HostServiceAdapter<Contract, Result(Arguments...)> final {
    using Response = typename Contract::Response;
    using Values = std::tuple<std::remove_cvref_t<Arguments>...>;

    class Pending final
        : public carven::runtime::hosting::detail::Pending<typename Contract::Completion> {
    public:
        Pending(HostCallContext& host, HostPendingRequest<Response> request) noexcept
            : host(host),
              request(std::move(request)) {}

        auto poll() noexcept -> std::optional<typename Contract::Completion> override {
            auto response = request.consume();
            if (!response) {
                return std::nullopt;
            }
            if (*response) {
                auto checked = host_check_native_response(**response, host.limits());
                if (checked) {
                    using Completion = typename Contract::Completion;
                    return HostResponseTraits<Response>::visit(
                        **response,
                        [](auto&& value) static noexcept -> Completion {
                            if constexpr (std::same_as<typename Contract::NativeResult, void>) {
                                return Completion::success();
                            } else {
                                return Completion::success_from(
                                    [&]() noexcept -> typename Contract::NativeResult {
                                        return std::forward<decltype(value)>(value);
                                    }
                                );
                            }
                        },
                        [](auto&& failure) static noexcept -> Completion {
                            return Completion::failure(std::forward<decltype(failure)>(failure));
                        }
                    );
                }
                checked.error().kind = HostErrorKind::Execution;
                HostContextAccess::stop(host, std::move(checked.error()));
            } else {
                HostContextAccess::stop(host, std::move(response->error()));
            }
            return Contract::Completion::stopped();
        }

        auto cancel() noexcept -> void override { request.cancel(); }

    private:
        HostCallContext& host;
        HostPendingRequest<Response> request;
    };

    template<auto Start>
    class SourcePending final : public HostCall {
    public:
        SourcePending(
            HostCallContext& host,
            std::size_t index,
            Values arguments,
            HostLimits limits,
            bool opaque_result
        ) noexcept
            : host(host),
              index(index),
              arguments(std::move(arguments)),
              limits(limits),
              opaque_result(opaque_result) {}

        auto advance() noexcept -> HostTaskStatus override {
            if (outcome) {
                return HostTaskStatus::Finished;
            }
            if (cancelled) {
                outcome.emplace(HostCancelled {});
                return HostTaskStatus::Finished;
            }
            if (!request) {
                auto started = launch<Start>(host, index, *arguments);
                arguments.reset();
                if (!started) {
                    outcome.emplace(std::unexpected(std::move(started.error())));
                    return HostTaskStatus::Finished;
                }
                request.emplace(std::move(*started));
            }
            auto response = request->consume();
            if (!response) {
                return HostTaskStatus::Waiting;
            }
            if (!*response) {
                outcome.emplace(std::unexpected(std::move(response->error())));
            } else {
                auto checked = host_check_native_response(**response, limits);
                if (!checked) {
                    checked.error().kind = HostErrorKind::Execution;
                    outcome.emplace(std::unexpected(std::move(checked.error())));
                } else {
                    outcome.emplace(
                        HostResponseTraits<Response>::encode(std::move(**response), opaque_result)
                    );
                }
            }
            return HostTaskStatus::Finished;
        }

        auto cancel() noexcept -> void override {
            if (request) {
                request->cancel();
            }
            arguments.reset();
            cancelled = true;
        }

        auto take_result() noexcept -> std::expected<HostOutcome, HostError> override {
            return std::move(*outcome);
        }

    private:
        HostCallContext& host;
        std::size_t index;
        std::optional<Values> arguments;
        HostLimits limits;
        bool opaque_result;
        std::optional<HostPendingRequest<Response>> request;
        bool cancelled = false;
        std::optional<std::expected<HostOutcome, HostError>> outcome;
    };

    template<auto Start>
    static auto launch(HostCallContext& host, std::size_t index, Values& arguments) noexcept
        -> std::expected<HostPendingRequest<Response>, HostError> {
        using Callback =
            auto (*)(void*, std::remove_cvref_t<Arguments>..., HostRequest<Response>) noexcept
            -> void;

        static_assert(
            std::same_as<decltype(Start), Callback>,
            "async service callback must match its contract and typed request"
        );

        auto mailbox = std::make_shared<HostRequestValue<Response>>(nullptr);

        struct Launch final {
            Values& arguments;
            const std::shared_ptr<HostRequestValue<Response>>& mailbox;
        };

        auto launch = Launch {.arguments = arguments, .mailbox = mailbox};
        const auto start = [](void* context,
                              void* state) noexcept -> std::expected<void, HostError> {
            const auto& launch = *static_cast<Launch*>(state);
            std::apply(
                [&](auto&... values) noexcept {
                    Start(context, std::move(values)..., HostRequest<Response>(launch.mailbox));
                },
                launch.arguments
            );
            return {};
        };
        auto admitted = HostContextAccess::attach(host, index, mailbox, &launch, start);
        if (!admitted) {
            return std::unexpected(std::move(admitted.error()));
        }
        return HostPendingRequest<Response>(std::move(mailbox));
    }

    template<auto Start>
    static auto request(void* context, std::size_t index, Arguments... arguments) noexcept
        -> std::unique_ptr<
            carven::runtime::hosting::detail::Pending<typename Contract::Completion>> {
        auto& host = *static_cast<HostCallContext*>(context);
        auto values = Values(std::forward<Arguments>(arguments)...);
        auto pending = launch<Start>(host, index, values);
        if (!pending) {
            HostContextAccess::stop(host, std::move(pending.error()));
            return nullptr;
        }
        return std::make_unique<Pending>(host, std::move(*pending));
    }

    template<auto Start = nullptr>
    static auto call(
        void* context,
        HostCallContext* host,
        std::size_t index,
        std::span<const HostCallAccess>,
        bool opaque_result,
        std::span<const HostValue> arguments,
        HostLimits limits,
        std::span<const HostNativeType> native_types
    ) noexcept -> std::expected<HostInvocationResult, HostError> {
        auto values = HostArgumentDecoder<Arguments...>::decode(arguments, limits, native_types);
        if (!values) {
            return std::unexpected(std::move(values.error()));
        }
        if constexpr (Contract::asynchronous) {
            return HostInvocationResult(
                std::make_unique<
                    SourcePending<Start>>(*host, index, std::move(*values), limits, opaque_result)
            );
        } else {
            const auto& binding = *static_cast<const typename Contract::Binding*>(context);
            // Encoding consumes the response, including move-only native values.
            // NOLINTNEXTLINE(misc-const-correctness)
            auto response = std::apply(
                [&binding](auto&... decoded) noexcept {
                    return carven::runtime::hosting::detail::ServiceAccess::invoke(
                        binding,
                        static_cast<Arguments&&>(decoded)...
                    );
                },
                *values
            );
            auto checked = host_check_native_response(response, limits);
            if (!checked) {
                checked.error().kind = HostErrorKind::Execution;
                return std::unexpected(std::move(checked.error()));
            }
            return HostInvocationResult(
                HostResponseTraits<Response>::encode(std::move(response), opaque_result)
            );
        }
    }
};

template<typename Binding>
auto host_service(Binding& binding) noexcept -> HostServiceBinding {
    using Contract = typename std::remove_cvref_t<Binding>::Contract;

    static_assert(host_source_contract_supported<Contract>, "expected a native service contract");

    static_assert(std::same_as<std::remove_cvref_t<Binding>, typename Contract::Binding>);
    static_assert(!Contract::asynchronous, "synchronous bindings require a synchronous service");
    using Adapter = HostServiceAdapter<Contract, typename Contract::Signature>;
    const auto native = carven::runtime::hosting::detail::ServiceAccess::registration(binding);
    return HostServiceAccess::make(
        {.contract = HostSourceContract<Contract, typename Contract::Signature>::contract(
             carven::runtime::hosting::detail::ServiceAccess::accesses(binding),
             carven::runtime::hosting::detail::ServiceAccess::native_types(binding),
             carven::runtime::hosting::detail::ServiceAccess::signature(binding)
         ),
         .context = const_cast<std::remove_cvref_t<Binding>*>(std::addressof(binding)),
         .call = Adapter::template call<>,
         .cancel = nullptr,
         .native = native,
         .static_effects = std::nullopt}
    );
}

template<auto Start, auto Cancel, typename Contract>
auto host_async_service(Contract service, void* context) noexcept -> HostServiceBinding {
    static_assert(host_source_contract_supported<Contract>, "expected a native service contract");

    static_assert(Contract::asynchronous);
    static_assert(Start != nullptr, "async service requires a start callback");
    using Cancellation = auto (*)(void*, std::uint64_t) noexcept -> void;

    static_assert(
        std::same_as<decltype(Cancel), Cancellation>
            || std::same_as<decltype(Cancel), std::nullptr_t>,
        "async service cancellation callback must take its context and request identity"
    );

    using Adapter = HostServiceAdapter<Contract, typename Contract::Signature>;
    static const typename Contract::Start native_start = Adapter::template request<Start>;
    return HostServiceAccess::make(
        {.contract = HostSourceContract<Contract, typename Contract::Signature>::contract(
             service.parameter_accesses,
             service.native_types,
             service.source_signature
         ),
         .context = context,
         .call = Adapter::template call<Start>,
         .cancel = Cancel,
         .native =
             carven::runtime::hosting::detail::ServiceAccess::asynchronous<Contract>(&native_start),
         .static_effects = std::nullopt}
    );
}
