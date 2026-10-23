#pragma once

#include <carven/hosting/call.hpp>

#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace carven::runtime::execution {

class EventSource;

} // namespace carven::runtime::execution

// Bind compiled functions and members independently of any source expression.
// The same adapter owns arguments, native results, and asynchronous completion.
template<typename Signature>
struct HostCppSignature;

template<typename Result, typename... Arguments>
struct HostCppSignature<Result (*)(Arguments...) noexcept> final {
    using Type = Result(Arguments...);
    static constexpr auto kind = HostCppOperationKind::Function;
};

template<typename Owner, typename Result, typename... Arguments>
struct HostCppSignature<Result (Owner::*)(Arguments...) noexcept> final {
    using Type = Result(Owner&, Arguments...);
    static constexpr auto kind = HostCppOperationKind::Method;
};

template<typename Owner, typename Result, typename... Arguments>
struct HostCppSignature<Result (Owner::*)(Arguments...) const noexcept> final {
    using Type = Result(const Owner&, Arguments...);
    static constexpr auto kind = HostCppOperationKind::Method;
};

template<typename Owner, typename Value>
struct HostCppSignature<Value Owner::*> final {
    using Type = Value(const Owner&);
    static constexpr auto kind = HostCppOperationKind::Field;
};

template<auto Function, typename Signature>
class HostCppOperation;

template<auto Function, typename Result, typename... Arguments>
class HostCppOperation<Function, Result(Arguments...)> final : public HostCall {
    using ResultContract = HostCallResult<Result>;
    static constexpr auto asynchronous = ResultContract::execution == HostCallExecution::Async;

    static_assert(
        !std::is_reference_v<Result>,
        "interpreted native operations require materialized results"
    );

    static_assert(
        asynchronous || std::is_void_v<Result> || std::is_copy_constructible_v<Result>,
        "interpreted native values require copy construction"
    );

public:
    HostCppOperation(
        bool opaque_result,
        HostLimits limits,
        carven::runtime::execution::Driver* driver,
        carven::runtime::execution::EventSource* events
    ) noexcept
        : opaque_result(opaque_result),
          limits(limits),
          driver(driver),
          events(events) {}

    ~HostCppOperation() noexcept override { cancel(); }

    template<typename T>
    static auto type() noexcept -> HostCallType {
        auto result = host_call_type<std::remove_cvref_t<T>>();
        result.allow_cpp = true;
        return result;
    }

    template<typename T>
    auto import_value(T&& value) noexcept -> std::expected<HostOutcome, HostError> {
        if (!opaque_result) {
            auto checked = host_check_native_value(value, limits, 0uz);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
        }
        return host_encode_native(std::forward<T>(value), opaque_result);
    }

    template<std::size_t... Indices>
    auto decode(
        std::span<const HostCallAccess> accesses,
        std::span<const HostValue> arguments,
        std::index_sequence<Indices...>
    ) noexcept -> std::expected<void, HostError> {
        auto error = std::optional<HostError>();
        auto budget = limits;
        const auto decode = [&]<std::size_t Index, typename Parameter>() noexcept {
            using Value = std::remove_cvref_t<Parameter>;
            if (const auto* box = std::get_if<HostObjectRef>(&arguments[Index])) {
                // Write parameters retain mutable storage; Take parameters move from it.
                // NOLINTNEXTLINE(misc-const-correctness)
                auto* value = box->template get<Value>();
                if (value == nullptr) {
                    error = hosting_error(
                        HostErrorKind::Arguments,
                        "native operation argument has the wrong carrier or is no longer alive"
                    );
                } else {
                    if constexpr (std::is_reference_v<Parameter>
                                  || std::same_as<Value, std::string_view>
                                  || runtime_native_slice<Value>) {
                        retained.push_back(*box);
                        std::get<Index>(inputs) = value;
                    } else {
                        // A by-value native parameter copies a Read/Write operand.
                        // Only source Take may consume the original native owner.
                        auto& storage = std::get<Index>(owned);
                        if (accesses[Index] == HostCallAccess::Take) {
                            storage.emplace(std::move(*value));
                        } else {
                            storage.emplace(*value);
                        }
                        std::get<Index>(inputs) = std::addressof(*storage);
                    }
                }
                return;
            }
            auto decoded =
                host_decode_native<HostNativeArgumentStorage<Value>>(arguments[Index], budget, 0uz);
            if (!decoded) {
                error = std::move(decoded.error());
                return;
            }
            auto& storage = std::get<Index>(owned);
            storage.emplace(std::move(*decoded));
            if constexpr (std::same_as<Value, std::string_view>) {
                auto& view = std::get<Index>(views);
                view.emplace(*storage);
                std::get<Index>(inputs) = std::addressof(*view);
            } else if constexpr (runtime_native_slice<Value>) {
                auto& view = std::get<Index>(views);
                view.emplace(
                    std::span<const HostNativeElement<Value>>(storage->begin(), storage->size())
                );
                std::get<Index>(inputs) = std::addressof(*view);
            } else {
                std::get<Index>(inputs) = std::addressof(*storage);
            }
        };
        (decode.template operator()<Indices, Arguments>(), ...);
        if (error) {
            return std::unexpected(std::move(*error));
        }
        return {};
    }

    template<std::size_t... Indices>
    auto invoke(std::index_sequence<Indices...>) noexcept -> Result {
        return std::invoke(Function, static_cast<Arguments&&>(*std::get<Indices>(inputs))...);
    }

    auto advance() noexcept -> HostTaskStatus override {
        if (result) {
            return HostTaskStatus::Finished;
        }
        if constexpr (asynchronous) {
            if (!root) {
                root.emplace(*driver, std::move(*operation), events);
                operation.reset();
            }
            const auto progress = root->advance();
            if (progress != decltype(progress)::Complete) {
                return progress == decltype(progress)::Runnable ? HostTaskStatus::Runnable
                                                                : HostTaskStatus::Waiting;
            }
            auto completion = root->take_completion();
            root.reset();
            if (completion.is_stopped()) {
                result.emplace(
                    std::unexpected(
                        hosting_error(HostErrorKind::Execution, "native operation stopped")
                    )
                );
            } else if (completion.is_cancelled()) {
                result.emplace(HostCancelled {});
            } else if (auto* success = completion.success_if()) { // NOLINT(misc-const-correctness)
                // Nontrivial success values are transferred into the host response.
                if constexpr (std::same_as<typename ResultContract::Value, void>) {
                    result.emplace(import_value(std::monostate {}));
                } else {
                    result.emplace(import_value(std::move(success->value)));
                }
            } else {
                result.emplace(failure(
                    completion,
                    std::make_index_sequence<
                        std::tuple_size_v<typename ResultContract::FailureTypes>>()
                ));
            }
            return HostTaskStatus::Finished;
        } else {
            std::terminate();
        }
    }

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override {
        if constexpr (asynchronous) {
            return root && root->wait(deadline);
        } else {
            return false;
        }
    }

    auto cancel() noexcept -> void override {
        if constexpr (asynchronous) {
            if (result) {
                return;
            }
            operation.reset(); // A cold frame has no started work to drain.
            if (root) {
                root->cancel();
                while (true) {
                    const auto progress = root->advance();
                    if (progress == decltype(progress)::Complete) {
                        break;
                    }
                }
                root->take_completion();
                root.reset();
            }
            result.emplace(HostCancelled {});
        }
    }

    auto take_result() noexcept -> std::expected<HostOutcome, HostError> override {
        return std::move(*result);
    }

    template<typename Completion, std::size_t... Indices>
    auto failure(Completion& completion, std::index_sequence<Indices...>) noexcept
        -> std::expected<HostOutcome, HostError> {
        auto converted = std::optional<std::expected<HostOutcome, HostError>>();
        [[maybe_unused]] const auto decode = [&]<std::size_t Index>() noexcept {
            using Failure = std::tuple_element_t<Index, typename ResultContract::FailureTypes>;
            if (auto* payload = completion.template failure_if<Failure>()) {
                auto checked = host_check_native_value(*payload, limits, 0uz);
                if (!checked) {
                    converted.emplace(std::unexpected(std::move(checked.error())));
                } else {
                    converted.emplace(host_failure(std::move(*payload)));
                }
            }
        };
        (decode.template operator()<Indices>(), ...);
        if (!converted) {
            return std::unexpected(
                hosting_error(HostErrorKind::Execution, "native operation has no completion")
            );
        }
        return std::move(*converted);
    }

    static auto call(
        void* context,
        HostCallContext* host,
        std::size_t,
        std::span<const HostCallAccess> accesses,
        bool opaque_result,
        std::span<const HostValue> arguments,
        HostLimits limits,
        std::span<const HostNativeType>
    ) noexcept -> std::expected<HostInvocationResult, HostError> {
        if (arguments.size() != sizeof...(Arguments)) {
            return std::unexpected(hosting_error(
                HostErrorKind::Arguments,
                "native operation argument count does not match its registered binding"
            ));
        }
        if constexpr (asynchronous) {
            if (host == nullptr) {
                return std::unexpected(hosting_error(
                    HostErrorKind::Execution,
                    "asynchronous native operation requires an execution context"
                ));
            }
            auto state = std::make_unique<HostCppOperation>(
                opaque_result,
                limits,
                &HostContextAccess::native_driver(*host),
                static_cast<carven::runtime::execution::EventSource*>(context)
            );
            auto decoded =
                state->decode(accesses, arguments, std::index_sequence_for<Arguments...>());
            if (!decoded) {
                return std::unexpected(std::move(decoded.error()));
            }
            state->operation.emplace(state->invoke(std::index_sequence_for<Arguments...>()));
            return HostInvocationResult(std::unique_ptr<HostCall>(std::move(state)));
        } else {
            auto state = HostCppOperation(opaque_result, limits, nullptr, nullptr);
            auto decoded =
                state.decode(accesses, arguments, std::index_sequence_for<Arguments...>());
            if (!decoded) {
                return std::unexpected(std::move(decoded.error()));
            }
            auto converted = [&]() noexcept {
                if constexpr (std::is_void_v<Result>) {
                    state.invoke(std::index_sequence_for<Arguments...>());
                    return state.import_value(std::monostate {});
                } else {
                    return state.import_value(
                        state.invoke(std::index_sequence_for<Arguments...>())
                    );
                }
            }();
            if (!converted) {
                return std::unexpected(std::move(converted.error()));
            }
            return HostInvocationResult(std::move(*converted));
        }
    }

    static auto contract(
        std::string_view name,
        HostCppOperationKind kind,
        std::span<const HostNativeType> native_types
    ) noexcept -> HostCallContract {
        return {
            .name = std::string(name),
            .parameters = {HostCallParameter {
                .type = type<Arguments>(),
                .access = std::is_rvalue_reference_v<Arguments> ? HostCallAccess::Take
                    : std::is_lvalue_reference_v<Arguments>
                        && !std::is_const_v<std::remove_reference_t<Arguments>>
                    ? HostCallAccess::Write
                    : HostCallAccess::Read
            }...},
            .result =
                type<std::conditional_t<asynchronous, typename ResultContract::Value, Result>>(),
            .execution = ResultContract::execution,
            .failures = HostResponseTraits<typename ResultContract::Response>::failures(),
            .native_types = {native_types.begin(), native_types.end()},
            .source_signature = {},
            .cpp_operation = kind,
        };
    }

private:
    bool opaque_result;
    HostLimits limits;
    carven::runtime::execution::Driver* driver;
    carven::runtime::execution::EventSource* events;
    std::tuple<std::optional<HostNativeArgumentStorage<std::remove_cvref_t<Arguments>>>...> owned;
    std::tuple<std::optional<std::remove_cvref_t<Arguments>>...> views;
    std::tuple<std::remove_reference_t<Arguments>*...> inputs;
    std::vector<HostObjectRef> retained;
    std::optional<std::conditional_t<asynchronous, Result, std::monostate>> operation;
    std::optional<typename ResultContract::Root> root;
    std::optional<std::expected<HostOutcome, HostError>> result;
};

template<auto Function>
auto host_cpp_operation(
    std::string_view name,
    std::span<const HostNativeType> native_types = {},
    carven::runtime::execution::EventSource* events = nullptr
) noexcept -> HostServiceBinding {
    using Signature = HostCppSignature<decltype(Function)>;
    using Adapter = HostCppOperation<Function, typename Signature::Type>;
    if (name.starts_with('&')) {
        name.remove_prefix(1);
    }
    if (name.starts_with("::")) {
        name.remove_prefix(2);
    }
    if constexpr (Signature::kind != HostCppOperationKind::Function) {
        // Member lookup also checks the native receiver; its source name is unqualified.
        if (const auto scope = name.rfind("::"); scope != std::string_view::npos) {
            name.remove_prefix(scope + 2);
        }
    }
    return HostServiceAccess::make(
        {.contract = Adapter::contract(name, Signature::kind, native_types),
         .context = events,
         .call = Adapter::call,
         .cancel = nullptr,
         .native = {},
         .static_effects = std::nullopt}
    );
}

template<typename From, typename To = From>
auto host_cpp_conversion(
    std::string_view native_name = {},
    std::span<const HostNativeType> native_types = {}
) noexcept -> HostServiceBinding {
    constexpr auto convert = +[](const From& value) static noexcept -> To {
        return static_cast<To>(value);
    };
    using Adapter = HostCppOperation<convert, To(const From&)>;
    if (native_name.starts_with("::")) {
        native_name.remove_prefix(2);
    }
    auto contract = Adapter::contract(
        native_name.empty() ? "conversion" : native_name,
        HostCppOperationKind::Conversion,
        native_types
    );
    if (native_name.empty()) {
        contract.result.allow_cpp = false;
    }
    return HostServiceAccess::make(
        {.contract = std::move(contract),
         .context = nullptr,
         .call = Adapter::call,
         .cancel = nullptr,
         .native = {},
         .static_effects = std::nullopt}
    );
}
