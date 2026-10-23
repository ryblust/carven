module carven:semantic.evaluation.native.call;

import :semantic.evaluation.execution;
import :semantic.evaluation.limits;
import :semantic.evaluation.native.request;
import :semantic.evaluation.native.value;
import :semantic.evaluation.runtime;
import :semantic.evaluation.value;
import :semantic.semir.type;
import std;

struct ExecutionNativeCapability final {
    bool static_execution;
    bool environment_effects;
};

// The selected host owns the driver and first native stop through executor cleanup.
struct ExecutionNativeContext final {
    carven::runtime::execution::Driver driver;
    ExecutionLimits limits;
    std::optional<std::string> stopped;
};

struct ExecutionNativeBinding final {
    ExecutionNativeKind kind;
    std::string name;
    ExecutionNativeCapability capability;
    bool (*matches)(
        const ExecutionValueAccess&,
        const ExecutionNativeRequest&,
        std::span<const ExecutionValue>
    ) noexcept;
    std::expected<ExecutionHostResult, ExecutionCallFailure> (*invoke)(
        ExecutionNativeContext&,
        const ExecutionValueAccess&,
        SemanticExecutionContext&,
        const ExecutionNativeRequest&,
        std::span<ExecutionValue>,
        ProgramOriginID,
        bool,
        carven::runtime::execution::EventSource*
    ) noexcept;
    carven::runtime::execution::EventSource* events;
};

template<typename Signature>
struct ExecutionNativeSignature;

template<typename Result, typename... Arguments>
struct ExecutionNativeSignature<Result (*)(Arguments...) noexcept> final {
    using Type = Result(Arguments...);
    static constexpr auto kind = ExecutionNativeKind::Function;
};

template<typename Owner, typename Result, typename... Arguments>
struct ExecutionNativeSignature<Result (Owner::*)(Arguments...) noexcept> final {
    using Type = Result(Owner&, Arguments...);
    static constexpr auto kind = ExecutionNativeKind::Method;
};

template<typename Owner, typename Result, typename... Arguments>
struct ExecutionNativeSignature<Result (Owner::*)(Arguments...) const noexcept> final {
    using Type = Result(const Owner&, Arguments...);
    static constexpr auto kind = ExecutionNativeKind::Method;
};

template<typename Owner, typename Value>
struct ExecutionNativeSignature<Value Owner::*> final {
    using Type = Value(const Owner&);
    static constexpr auto kind = ExecutionNativeKind::Field;
};

template<typename Result>
struct ExecutionNativeResult final {
    using Value = Result;
    using Root = std::monostate;
    static constexpr auto asynchronous = false;
    static constexpr auto failure_count = 0uz;
};

template<typename Result, typename... Failures>
struct ExecutionNativeResult<carven::runtime::execution::AsyncOperation<Result, Failures...>>
    final {
    using Value = Result;
    using Root = carven::runtime::execution::Root<Result, Failures...>;
    static constexpr auto asynchronous = true;
    static constexpr auto failure_count = sizeof...(Failures);
};

template<auto Function, typename Signature>
class ExecutionNativeFunction;

// The executor owns this activation. Its parameter storage outlives every native
// borrow, including suspension; cancellation drains the existing runtime Root.
template<auto Function, typename Result, typename... Arguments>
class ExecutionNativeFunction<Function, Result(Arguments...)> final
    : public ExecutionExternalOperation {
    using ResultInfo = ExecutionNativeResult<Result>;
    static constexpr auto asynchronous = ResultInfo::asynchronous;

    static_assert(!std::is_reference_v<Result>, "native execution results must be materialized");

public:
    ExecutionNativeFunction(
        ExecutionNativeContext& host,
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        TypeID result_type,
        ProgramOriginID origin,
        bool environment_effects,
        carven::runtime::execution::EventSource* events
    ) noexcept
        : host(host),
          values(values),
          context(context),
          result_type(result_type),
          origin(origin),
          environment_effects(environment_effects),
          events(events),
          budget(host.limits),
          owned(),
          views(),
          inputs(),
          retained(),
          operation(),
          root(),
          outcome() {}

    ~ExecutionNativeFunction() noexcept override { cancel(); }

    template<typename Parameter>
    static auto parameter_matches(
        const ExecutionValueAccess& values,
        const CallableParameter& parameter,
        const ExecutionValue& argument
    ) noexcept -> bool {
        using Value = std::remove_cvref_t<Parameter>;
        if constexpr (std::is_rvalue_reference_v<Parameter>) {
            if (parameter.access != AccessMode::Take) {
                return false;
            }
        } else if constexpr (std::is_lvalue_reference_v<Parameter>
                             && !std::is_const_v<std::remove_reference_t<Parameter>>) {
            if (parameter.access == AccessMode::Read) {
                return false;
            }
        }
        if (const auto* native = std::get_if<ExecutionHostObject>(&argument)) {
            return native->reference.template get<Value>() != nullptr;
        }
        // Mutating a temporary conversion would not update the source place.
        return parameter.access != AccessMode::Write
            && ExecutionNativeValue::matches<Value>(values, parameter.type);
    }

    static auto matches(
        const ExecutionValueAccess& values,
        const ExecutionNativeRequest& request,
        std::span<const ExecutionValue> arguments
    ) noexcept -> bool {
        if (arguments.size() != sizeof...(Arguments)
            || request.parameters.size() != sizeof...(Arguments)
            || (request.execution == CallableExecutionKind::Async) != asynchronous
            || !request.failures.empty()
            || ResultInfo::failure_count != 0uz
            || !ExecutionNativeValue::matches<typename ResultInfo::Value>(values, request.result)) {
            return false;
        }
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) noexcept {
            return (
                parameter_matches<Arguments>(
                    values,
                    request.parameters[Indices],
                    arguments[Indices]
                )
                && ...
            );
        }(std::index_sequence_for<Arguments...>());
    }

    template<std::size_t Index, typename Parameter>
    auto prepare_argument(const CallableParameter& parameter, ExecutionValue& argument) noexcept
        -> std::expected<void, std::string> {
        using Value = std::remove_cvref_t<Parameter>;
        if (auto* native = std::get_if<ExecutionHostObject>(&argument)) {
            auto* value = native->reference.template get<Value>();
            if (value == nullptr) {
                return std::unexpected(
                    "native argument has the wrong carrier or is no longer alive"
                );
            }
            if constexpr (std::is_reference_v<Parameter>
                          || std::same_as<Value, std::string_view>
                          || ExecutionNativeSequence<Value>::borrowed) {
                retained.push_back(native->reference);
                std::get<Index>(inputs) = value;
            } else {
                auto& storage = std::get<Index>(owned);
                if (parameter.access == AccessMode::Take) {
                    storage.emplace(std::move(*value));
                } else {
                    storage.emplace(*value);
                }
                std::get<Index>(inputs) = std::addressof(*storage);
            }
            return {};
        }
        auto decoded = ExecutionNativeValue::decode<Value>(values, argument, budget, 0uz);
        if (!decoded) {
            return std::unexpected(std::move(decoded.error()));
        }
        auto& storage = std::get<Index>(owned);
        storage.emplace(std::move(*decoded));
        if constexpr (std::same_as<Value, std::string_view>) {
            auto& view = std::get<Index>(views);
            view.emplace(*storage);
            std::get<Index>(inputs) = std::addressof(*view);
        } else if constexpr (ExecutionNativeSequence<Value>::enabled) {
            if constexpr (ExecutionNativeSequence<Value>::borrowed) {
                auto& view = std::get<Index>(views);
                view.emplace(
                    std::span<const typename ExecutionNativeSequence<Value>::Element>(
                        storage->begin(),
                        storage->size()
                    )
                );
                std::get<Index>(inputs) = std::addressof(*view);
            } else {
                std::get<Index>(inputs) = std::addressof(*storage);
            }
        } else {
            std::get<Index>(inputs) = std::addressof(*storage);
        }
        return {};
    }

    auto prepare(
        const ExecutionNativeRequest& request,
        std::span<ExecutionValue> arguments
    ) noexcept -> std::expected<void, std::string> {
        auto error = std::optional<std::string>();
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>) noexcept {
            [[maybe_unused]] const auto one =
                [&]<std::size_t Index, typename Parameter>() noexcept {
                    if (!error) {
                        auto result = prepare_argument<Index, Parameter>(
                            request.parameters[Index],
                            arguments[Index]
                        );
                        if (!result) {
                            error = std::move(result.error());
                        }
                    }
                };
            (one.template operator()<Indices, Arguments>(), ...);
        }(std::index_sequence_for<Arguments...>());
        if (error) {
            return std::unexpected(std::move(*error));
        }
        return {};
    }

    auto invoke() noexcept -> Result {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) noexcept -> Result {
            return std::invoke(Function, static_cast<Arguments&&>(*std::get<Indices>(inputs))...);
        }(std::index_sequence_for<Arguments...>());
    }

    template<typename T>
    auto complete(T&& result) noexcept -> std::expected<ExecutionValue, ExecutionCallFailure> {
        auto converted =
            ExecutionNativeValue::encode(values, std::forward<T>(result), result_type, budget, 0uz);
        if (!converted) {
            return execution_native_error(origin, std::move(converted.error()));
        }
        return std::move(*converted);
    }

    auto poll() noexcept
        -> std::optional<std::expected<ExecutionValue, ExecutionCallFailure>> override {
        if constexpr (asynchronous) {
            if (outcome) {
                return std::move(outcome);
            }
            if (host.stopped) {
                cancel();
                return execution_native_error(origin, *host.stopped);
            }
            if (!root) {
                if (environment_effects) {
                    if (const auto reason = context.host_effects_rejection()) {
                        cancel();
                        return execution_native_error(origin, std::string(*reason));
                    }
                }
                root.emplace(host.driver, std::move(*operation), events);
                operation.reset();
            }
            const auto progress = root->advance();
            if (host.stopped) {
                cancel();
                return execution_native_error(origin, *host.stopped);
            }
            if (progress != carven::runtime::execution::Progress::Complete) {
                return std::nullopt;
            }
            auto completion = root->take_completion();
            root.reset();
            if (completion.is_stopped()) {
                return execution_native_error(origin, "native operation stopped");
            }
            if (completion.is_cancelled()) {
                return cancelled();
            }
            if (auto* success = completion.success_if()) {
                if constexpr (std::same_as<typename ResultInfo::Value, void>) {
                    return complete(std::monostate {});
                } else {
                    return complete(std::move(success->value));
                }
            }
            return execution_native_error(
                origin,
                "native operation returned an undeclared failure"
            );
        } else {
            std::unreachable();
        }
    }

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override {
        if constexpr (asynchronous) {
            return root && root->wait(deadline);
        } else {
            return false;
        }
    }

    auto cancelled() const noexcept -> std::unexpected<ExecutionCallFailure> {
        return std::unexpected<ExecutionCallFailure>(
            ExecutionFailure(ExecutionCancelled {.origin = origin, .calls = {}, .blocks = {}})
        );
    }

    auto cancel() noexcept -> void override {
        if constexpr (asynchronous) {
            operation.reset();
            if (root) {
                root->cancel();
                while (root->advance() != carven::runtime::execution::Progress::Complete) {
                }
                root->take_completion();
                root.reset();
            }
            if (!outcome) {
                outcome.emplace(cancelled());
            }
        }
    }

    static auto call(
        ExecutionNativeContext& host,
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        const ExecutionNativeRequest& request,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin,
        bool environment_effects,
        carven::runtime::execution::EventSource* events
    ) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
        if constexpr (asynchronous) {
            auto state = std::make_unique<ExecutionNativeFunction>(
                host,
                values,
                context,
                request.result,
                origin,
                environment_effects,
                events
            );
            auto prepared = state->prepare(request, arguments);
            if (!prepared) {
                return execution_native_error(origin, std::move(prepared.error()));
            }
            state->operation.emplace(state->invoke());
            return ExecutionHostResult(
                std::unique_ptr<ExecutionExternalOperation>(std::move(state))
            );
        } else {
            if (environment_effects) {
                if (const auto reason = context.host_effects_rejection()) {
                    return execution_native_error(origin, std::string(*reason));
                }
            }
            auto state = ExecutionNativeFunction(
                host,
                values,
                context,
                request.result,
                origin,
                environment_effects,
                events
            );
            auto prepared = state.prepare(request, arguments);
            if (!prepared) {
                return execution_native_error(origin, std::move(prepared.error()));
            }
            auto completed = [&]() noexcept {
                if constexpr (std::same_as<Result, void>) {
                    state.invoke();
                    return state.complete(std::monostate {});
                } else {
                    return state.complete(state.invoke());
                }
            }();
            if (!completed) {
                return std::unexpected(std::move(completed.error()));
            }
            return ExecutionHostResult(std::move(*completed));
        }
    }

private:
    ExecutionNativeContext& host;
    const ExecutionValueAccess& values;
    SemanticExecutionContext& context;
    TypeID result_type;
    ProgramOriginID origin;
    bool environment_effects;
    carven::runtime::execution::EventSource* events;
    ExecutionLimits budget;
    std::tuple<std::optional<ExecutionNativeStorage<std::remove_cvref_t<Arguments>>>...> owned;
    std::tuple<std::optional<std::remove_cvref_t<Arguments>>...> views;
    std::tuple<std::remove_reference_t<Arguments>*...> inputs;
    std::vector<carven::runtime::hosting::ObjectRef<void>> retained;
    std::optional<std::conditional_t<asynchronous, Result, std::monostate>> operation;
    std::optional<typename ResultInfo::Root> root;
    std::optional<std::expected<ExecutionValue, ExecutionCallFailure>> outcome;
};

template<auto Function>
auto execution_native_function(
    std::string name,
    ExecutionNativeCapability capability,
    carven::runtime::execution::EventSource* events = nullptr
) noexcept -> ExecutionNativeBinding {
    using Signature = ExecutionNativeSignature<decltype(Function)>;
    using Adapter = ExecutionNativeFunction<Function, typename Signature::Type>;
    if constexpr (Signature::kind != ExecutionNativeKind::Function) {
        const auto separator = name.rfind("::");
        if (separator != std::string::npos) {
            name.erase(0uz, separator + 2uz);
        }
    }
    return {
        .kind = Signature::kind,
        .name = std::move(name),
        .capability = capability,
        .matches = Adapter::matches,
        .invoke = Adapter::call,
        .events = events
    };
}

template<typename From, typename To>
struct ExecutionNativeConversion final {
    static auto convert(From value) noexcept -> To { return static_cast<To>(std::move(value)); }
};

template<typename From, typename To = From>
auto execution_native_conversion(std::string name, ExecutionNativeCapability capability) noexcept
    -> ExecutionNativeBinding {
    const auto source_value = name.empty();
    auto binding = execution_native_function<&ExecutionNativeConversion<From, To>::convert>(
        source_value ? "conversion" : std::move(name),
        capability
    );
    binding.kind = ExecutionNativeKind::Conversion;
    if (source_value) {
        binding.matches = [](const ExecutionValueAccess& values,
                             const ExecutionNativeRequest& request,
                             std::span<const ExecutionValue> arguments) static noexcept {
            using Adapter =
                ExecutionNativeFunction<&ExecutionNativeConversion<From, To>::convert, To(From)>;
            return !std::holds_alternative<CppTypeValue>(values.type_copy(request.result).value)
                && Adapter::matches(values, request, arguments);
        };
    }
    return binding;
}
