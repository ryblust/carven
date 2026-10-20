#pragma once

#include <carven/runtime/hosting/enumeration.hpp>
#include <carven/runtime/hosting/object.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime {

template<typename Result, typename... Failures>
class Outcome;

struct Void;
class String;

template<typename Element>
class Vector;

template<typename Element>
class Slice;

namespace execution {

template<typename Result, typename... Failures>
class Completion;

template<typename Result, typename... Failures>
class AsyncOperation;

} // namespace execution

} // namespace carven::runtime

namespace carven::runtime::hosting {

enum class ParameterAccess { Read, Write, Take };

namespace detail {

// One Session-owned registration. Its position matches the provider table.
struct Binding final {
    const void* key;
    const void* target;
};

struct ServiceAccess final {
    template<typename Binding>
    static auto registration(const Binding& binding) noexcept -> detail::Binding {
        return {
            .key = type_key<typename Binding::Contract>(),
            .target = binding.invoke == nullptr ? nullptr : std::addressof(binding)
        };
    }

    template<typename Contract>
    static auto asynchronous(const typename Contract::Start* start) noexcept -> detail::Binding {
        return {.key = type_key<Contract>(), .target = start};
    }

    template<typename Binding>
    static auto accesses(const Binding& binding) noexcept {
        return binding.parameter_accesses;
    }

    template<typename Binding>
    static auto native_types(const Binding& binding) noexcept {
        return binding.native_types;
    }

    template<typename Binding>
    static auto signature(const Binding& binding) noexcept {
        return binding.source_signature;
    }

    template<typename Binding, typename... Arguments>
    static auto invoke(const Binding& binding, Arguments&&... arguments) noexcept {
        return binding.invoke(binding.context, std::forward<Arguments>(arguments)...);
    }
};

template<typename T, typename NativeTypes>
consteval auto native_type_registered() noexcept -> bool {
    return []<std::size_t... Indices>(std::index_sequence<Indices...>) consteval {
        return (
            std::
                is_same_v<T, typename std::tuple_element_t<Indices, NativeTypes>::second_type::type>
            || ...
        );
    }(std::make_index_sequence<std::tuple_size_v<NativeTypes>> {});
}

template<typename T>
constexpr auto runtime_native_text = std::is_same_v<T, carven::runtime::String>;

template<typename T>
constexpr auto runtime_native_record = requires {
    carven_type_name(static_cast<T*>(nullptr));
    carven_fields(static_cast<T*>(nullptr));
};

template<typename T>
constexpr auto runtime_native_vector = false;

template<typename Element>
constexpr auto runtime_native_vector<carven::runtime::Vector<Element>> = true;

template<typename T>
constexpr auto runtime_native_slice = false;

template<typename Element>
constexpr auto runtime_native_slice<carven::runtime::Slice<Element>> = true;

template<typename T>
constexpr auto runtime_native_bytes = std::is_same_v<T, carven::runtime::Vector<std::uint8_t>>
    || std::is_same_v<T, carven::runtime::Slice<std::uint8_t>>;

inline auto native_bytes_error(
    std::size_t size,
    std::size_t& remaining,
    std::size_t per_value_limit
) noexcept -> std::string_view {
    if (size > remaining || size > per_value_limit) {
        return "host bytes exceed their call limit";
    }
    remaining -= size;
    return {};
}

inline auto native_text_error(
    std::string_view value,
    std::size_t& remaining,
    std::size_t per_value_limit,
    bool (*valid_text)(std::string_view) noexcept
) noexcept -> std::string_view {
    if (const auto error = native_bytes_error(value.size(), remaining, per_value_limit);
        !error.empty()) {
        return error;
    }
    if (!valid_text(value)) {
        return "host String requires valid UTF-8";
    }
    return {};
}

inline auto native_elements_error(std::size_t count, std::size_t& remaining) noexcept
    -> std::string_view {
    if (count > remaining) {
        return "host value exceeds its aggregate limit";
    }
    remaining -= count;
    return {};
}

template<typename T>
auto native_value_error(
    const T& value,
    std::size_t& bytes,
    std::size_t& aggregate,
    std::size_t per_value_limit,
    std::size_t depth_limit,
    bool (*valid_text)(std::string_view) noexcept,
    std::size_t depth
) noexcept -> std::string_view {
    if (depth > depth_limit) {
        return "host value exceeds its nesting limit";
    }
    if constexpr (object_reference<T>) {
        if (!value.alive()) {
            return "host value requires a live object reference";
        }
    } else if constexpr (std::is_same_v<T, std::string> || runtime_native_text<T>) {
        const auto text = [&]() noexcept -> std::string_view {
            if constexpr (std::is_same_v<T, std::string>) {
                return value;
            } else {
                return value.as_str();
            }
        }();
        return native_text_error(text, bytes, per_value_limit, valid_text);
    } else if constexpr (runtime_native_bytes<T>) {
        if (!value.empty() && depth == depth_limit) {
            return "host value exceeds its nesting limit";
        }
        return native_bytes_error(value.size(), bytes, per_value_limit);
    } else if constexpr (runtime_native_vector<T>) {
        if (const auto error = native_elements_error(value.size(), aggregate); !error.empty()) {
            return error;
        }
        for (const auto& element : value) {
            const auto error = native_value_error(
                element,
                bytes,
                aggregate,
                per_value_limit,
                depth_limit,
                valid_text,
                depth + 1
            );
            if (!error.empty()) {
                return error;
            }
        }
    } else if constexpr (runtime_native_enum<T>) {
        auto result = std::string_view();
        const auto found =
            visit_native_enum(value, [&](std::string_view, const auto& payload) noexcept {
                result = native_elements_error(
                    std::tuple_size_v<std::remove_cvref_t<decltype(payload)>>,
                    aggregate
                );
                std::apply(
                    [&](const auto&... item) noexcept {
                        (
                            [&]() noexcept {
                                if (result.empty()) {
                                    result = native_value_error(
                                        item,
                                        bytes,
                                        aggregate,
                                        per_value_limit,
                                        depth_limit,
                                        valid_text,
                                        depth + 1
                                    );
                                }
                            }(),
                            ...);
                    },
                    payload
                );
            });
        return found ? result : "host enum has no declared case";
    } else if constexpr (runtime_native_record<T>) {
        const auto fields = carven_fields(static_cast<T*>(nullptr));
        const auto error = native_elements_error(std::tuple_size_v<decltype(fields)>, aggregate);
        if (!error.empty()) {
            return error;
        }
        auto result = std::string_view();
        std::apply(
            [&](const auto&... field) noexcept {
                (
                    [&]() noexcept {
                        if (result.empty()) {
                            result = native_value_error(
                                value.*field.second,
                                bytes,
                                aggregate,
                                per_value_limit,
                                depth_limit,
                                valid_text,
                                depth + 1
                            );
                        }
                    }(),
                    ...);
            },
            fields
        );
        return result;
    }
    return {};
}

// Only this borrow is dynamically scoped. Suspension never retains a Scope.
class Scope final {
public:
    Scope(
        std::span<const Binding> bindings,
        void* call,
        bool (*ready)(void*) noexcept,
        void (*stop)(void*, std::string_view) noexcept,
        std::size_t byte_limit,
        std::size_t per_value_limit,
        std::size_t aggregate_limit,
        std::size_t depth_limit,
        bool (*valid_text)(std::string_view) noexcept
    ) noexcept
        : previous(std::exchange(active, this)),
          bindings(bindings),
          call(call),
          is_ready(ready),
          report(stop),
          byte_limit(byte_limit),
          per_value_limit(per_value_limit),
          aggregate_limit(aggregate_limit),
          depth_limit(depth_limit),
          valid_text(valid_text) {}

    Scope(const Scope&) = delete;
    auto operator=(const Scope&) -> Scope& = delete;

    ~Scope() noexcept { active = previous; }

    static auto current() noexcept -> Scope& {
        if (active == nullptr) {
            std::terminate();
        }
        return *active;
    }

    template<typename Contract>
    auto find() noexcept -> const Binding* {
        if (!is_ready(call)) {
            return nullptr;
        }
        for (const auto& binding : bindings) {
            if (binding.key == type_key<Contract>()) {
                return &binding;
            }
        }
        stop("missing typed host service binding");
        return nullptr;
    }

    auto context() const noexcept -> void* { return call; }

    auto ready() const noexcept -> bool { return is_ready(call); }

    auto index(const Binding& binding) const noexcept -> std::size_t {
        return static_cast<std::size_t>(std::addressof(binding) - bindings.data());
    }

    auto stop(std::string_view message) noexcept -> void { report(call, message); }

    template<typename... Values>
    auto check(const Values&... values) noexcept -> bool {
        if constexpr (sizeof...(Values) != 0) {
            auto bytes = byte_limit;
            auto aggregate = aggregate_limit;
            auto error = std::string_view();
            (
                [&]() noexcept {
                    if (error.empty()) {
                        error = native_value_error(
                            values,
                            bytes,
                            aggregate,
                            per_value_limit,
                            depth_limit,
                            valid_text,
                            0
                        );
                    }
                }(),
                ...);
            if (!error.empty()) {
                stop(error);
                return false;
            }
        }
        return true;
    }

private:
    inline static thread_local Scope* active = nullptr;
    Scope* previous;
    std::span<const Binding> bindings;
    void* call;
    bool (*is_ready)(void*) noexcept;
    void (*report)(void*, std::string_view) noexcept;
    std::size_t byte_limit;
    std::size_t per_value_limit;
    std::size_t aggregate_limit;
    std::size_t depth_limit;
    bool (*valid_text)(std::string_view) noexcept;
};

template<typename Completion>
class Pending {
public:
    virtual ~Pending() noexcept = default;
    virtual auto poll() noexcept -> std::optional<Completion> = 0;
    virtual auto cancel() noexcept -> void = 0;
};

} // namespace detail

template<std::size_t Size>
struct ServiceName final {
    char text[Size];

    constexpr ServiceName(const char (&name)[Size]) noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            text[index] = name[index];
        }
    }
};

template<ServiceName Name, typename Signature, typename NativeDeclarations, typename... Failures>
class Service;

template<
    ServiceName Name,
    typename Result,
    typename NativeDeclarations,
    typename... Arguments,
    typename... Failures>
class Service<Name, Result(Arguments...), NativeDeclarations, Failures...> final {
public:
    using Signature = Result(Arguments...);

    using NativeResult =
        std::conditional_t<std::is_same_v<Result, carven::runtime::Void>, void, Result>;

    using Completion = carven::runtime::Outcome<NativeResult, Failures...>;

    using Response = std::conditional_t<
        sizeof...(Failures) == 0,
        Result,
        carven::runtime::Outcome<
            std::conditional_t<std::is_same_v<Result, carven::runtime::Void>, void, Result>,
            Failures...>>;

    using FailureTypes = std::tuple<Failures...>;
    using NativeTypes = NativeDeclarations;
    static constexpr auto failure_count = sizeof...(Failures);
    static constexpr auto asynchronous = false;
    static constexpr auto name = std::string_view(Name.text, sizeof(Name.text) - 1);
    using Accesses = std::array<ParameterAccess, sizeof...(Arguments)>;
    Accesses parameter_accesses;
    NativeTypes native_types;
    std::string_view source_signature;

    explicit constexpr Service(
        Accesses accesses,
        NativeTypes types,
        std::string_view signature
    ) noexcept
        : parameter_accesses(accesses),
          native_types(types),
          source_signature(signature) {}

    class Binding final {
    public:
        using Contract = Service;

    private:
        constexpr Binding(
            void* context,
            Response (*invoke)(void*, Arguments...) noexcept,
            Accesses accesses,
            NativeTypes types,
            std::string_view signature
        ) noexcept
            : context(context),
              invoke(invoke),
              parameter_accesses(accesses),
              native_types(types),
              source_signature(signature) {}

        void* context;
        Response (*invoke)(void*, Arguments...) noexcept;
        Accesses parameter_accesses;
        NativeTypes native_types;
        std::string_view source_signature;
        friend class Service;
        friend struct detail::ServiceAccess;
    };

    constexpr auto bind(
        void* context,
        Response (*invoke)(void*, Arguments...) noexcept
    ) const noexcept -> Binding {
        return Binding(context, invoke, parameter_accesses, native_types, source_signature);
    }

    auto operator()(Arguments... arguments) const noexcept -> Completion {
        auto& scope = detail::Scope::current();
        const auto* registration = scope.template find<Service>();
        if (registration == nullptr) {
            return Completion::stopped();
        }
        if (!scope.check(arguments...)) {
            return Completion::stopped();
        }
        const auto& binding = *static_cast<const Binding*>(registration->target);
        auto response =
            detail::ServiceAccess::invoke(binding, std::forward<Arguments>(arguments)...);
        if constexpr (failure_count == 0) {
            if (!scope.check(response)) {
                return Completion::stopped();
            }
        } else if (response.is_stopped()) {
            return Completion::stopped();
        } else if (const auto* success = response.success_if()) {
            if constexpr (!std::is_same_v<Result, carven::runtime::Void>) {
                if (!scope.check(success->value)) {
                    return Completion::stopped();
                }
            }
        } else {
            const auto valid =
                (([&]() noexcept {
                     const auto* failure = response.template failure_if<Failures>();
                     return failure == nullptr || scope.check(*failure);
                 }())
                 && ...);
            if (!valid) {
                return Completion::stopped();
            }
        }
        if (!scope.ready()) {
            return Completion::stopped();
        }
        if constexpr (failure_count != 0) {
            return response;
        } else if constexpr (std::is_void_v<NativeResult>) {
            return Completion::success();
        } else {
            return Completion::success_from([&]() noexcept -> NativeResult {
                return std::move(response);
            });
        }
    }
};

template<ServiceName Name, typename Signature, typename NativeDeclarations, typename... Failures>
class AsyncService;

template<
    ServiceName Name,
    typename Result,
    typename NativeDeclarations,
    typename... Arguments,
    typename... Failures>
class AsyncService<Name, Result(Arguments...), NativeDeclarations, Failures...> final {
public:
    using Signature = Result(Arguments...);

    using NativeResult =
        std::conditional_t<std::is_same_v<Result, carven::runtime::Void>, void, Result>;

    using Completion = carven::runtime::execution::Completion<NativeResult, Failures...>;

    using Response = std::conditional_t<
        sizeof...(Failures) == 0,
        Result,
        carven::runtime::Outcome<
            std::conditional_t<std::is_same_v<Result, carven::runtime::Void>, void, Result>,
            Failures...>>;

    using FailureTypes = std::tuple<Failures...>;
    using NativeTypes = NativeDeclarations;

    using Start = auto (*)(void*, std::size_t, Arguments...) noexcept
        -> std::unique_ptr<detail::Pending<Completion>>;

    static constexpr auto failure_count = sizeof...(Failures);
    static constexpr auto asynchronous = true;
    static constexpr auto name = std::string_view(Name.text, sizeof(Name.text) - 1);

    using Accesses = std::array<ParameterAccess, sizeof...(Arguments)>;

    Accesses parameter_accesses;
    NativeTypes native_types;
    std::string_view source_signature;

    explicit constexpr AsyncService(
        Accesses accesses,
        NativeTypes types,
        std::string_view signature
    ) noexcept
        : parameter_accesses(accesses),
          native_types(types),
          source_signature(signature) {}

    auto operator()(Arguments... arguments) const noexcept
        -> carven::runtime::execution::AsyncOperation<NativeResult, Failures...>;

private:
    static auto run(AsyncService service, std::remove_cvref_t<Arguments>... arguments) noexcept
        -> carven::runtime::execution::AsyncOperation<NativeResult, Failures...>;

    auto start(Arguments... arguments) const noexcept
        -> std::unique_ptr<detail::Pending<Completion>> {
        auto& scope = detail::Scope::current();
        const auto* registration = scope.template find<AsyncService>();
        if (registration == nullptr) {
            return nullptr;
        }
        if (!scope.check(arguments...)) {
            return nullptr;
        }
        const auto start = *static_cast<const Start*>(registration->target);
        return start(
            scope.context(),
            scope.index(*registration),
            std::forward<Arguments>(arguments)...
        );
    }
};

namespace detail {

template<ServiceName Name, typename Result, typename Parameters, typename Failures, bool Async>
struct ServiceFactory;

template<
    ServiceName Name,
    typename Result,
    typename... Parameters,
    typename... Failures,
    bool Async>
struct ServiceFactory<Name, Result, std::tuple<Parameters...>, std::tuple<Failures...>, Async>
    final {
    template<typename NativeTypes>
    static constexpr auto make(
        std::array<ParameterAccess, sizeof...(Parameters)> accesses,
        NativeTypes native_types,
        std::string_view source_signature
    ) noexcept {
        if constexpr (Async) {
            return AsyncService<Name, Result(Parameters...), NativeTypes, Failures...>(
                accesses,
                native_types,
                source_signature
            );
        } else {
            return Service<Name, Result(Parameters...), NativeTypes, Failures...>(
                accesses,
                native_types,
                source_signature
            );
        }
    }
};

} // namespace detail

template<
    ServiceName Name,
    typename Result,
    typename Parameters,
    typename Failures,
    bool Async,
    typename NativeTypes>
constexpr auto make_service(
    std::array<ParameterAccess, std::tuple_size_v<Parameters>> accesses,
    NativeTypes native_types,
    std::string_view source_signature
) noexcept {
    return detail::ServiceFactory<Name, Result, Parameters, Failures, Async>::make(
        accesses,
        native_types,
        source_signature
    );
}

} // namespace carven::runtime::hosting
