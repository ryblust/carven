#pragma once

#include <carven/hosting/type.hpp>
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/hosting/object.hpp>
#include <carven/runtime/hosting/service.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

struct HostNativeType;
struct HostRequestState;
struct HostEnvironment;
struct HostCallContextState;
struct HostContextAccess;
struct HostServiceAccess;
struct HostObjectAccess;

namespace carven::runtime::execution {

class Driver;

} // namespace carven::runtime::execution

template<typename Contract, typename Signature>
struct HostServiceAdapter;

enum class HostErrorKind { Input, Analysis, Lookup, Signature, Arguments, Execution };

struct HostLocation final {
    std::string file;
    std::uint32_t line;
    std::uint32_t column;
};

struct HostError final {
    HostErrorKind kind;
    std::string message;
    std::optional<HostLocation> location;
    std::vector<HostLocation> calls;
};

using HostObjectRef = carven::runtime::hosting::ObjectRef<void>;
using HostObject = carven::runtime::hosting::Object;

struct HostValue;
struct HostField;

struct HostRecord final {
    std::string module_path;
    std::string name;
    std::vector<HostField> fields;
    auto field(std::string_view name) const noexcept -> const HostValue*;
};

struct HostEnum final {
    std::string module_path;
    std::string name;
    std::string case_name;
    std::vector<HostValue> payload;
};

// Values own their contents; nominal values carry their source module and type names.
struct HostValue final : std::variant<
                             std::monostate,
                             std::int32_t,
                             std::uint8_t,
                             std::int64_t,
                             std::uint64_t,
                             bool,
                             std::string,
                             carven::runtime::Vector<std::uint8_t>,
                             std::vector<HostValue>,
                             HostRecord,
                             HostEnum,
                             HostObjectRef> {
    using variant::variant;
    using variant::operator=;

    HostValue() noexcept;
    HostValue(const HostValue& other) noexcept;
    HostValue(HostValue&&) = default;
    auto operator=(const HostValue& other) noexcept -> HostValue&;
    auto operator=(HostValue&&) -> HostValue& = default;
    HostValue(HostObjectRef reference) noexcept;

    template<typename Target>
    HostValue(carven::runtime::hosting::ObjectRef<Target> reference) noexcept;
};

struct HostField final {
    std::string name;
    HostValue value;
};

inline HostValue::HostValue() noexcept = default;

template<typename Target>
HostValue::HostValue(carven::runtime::hosting::ObjectRef<Target> reference) noexcept
    : variant(std::in_place_type<HostObjectRef>, HostObjectRef::own(std::move(reference))) {}

struct HostLimits final {
    std::size_t steps;
    std::size_t byte_work;
    std::size_t aggregate_work;
};

struct HostOutput final {
    void* context;
    void (*write)(void* context, bool error, std::string_view bytes) noexcept;
};

// Owns its nominal payload independently of any loaded source program.
class HostFailure final {
public:
    explicit HostFailure(HostRecord payload) noexcept;
    explicit HostFailure(HostEnum payload) noexcept;
    auto module_path() const noexcept -> std::string_view;
    auto name() const noexcept -> std::string_view;
    auto payload() const& noexcept -> const HostValue&;
    auto payload() && noexcept -> HostValue;
    auto same_type(const HostFailure& other) const noexcept -> bool;

private:
    HostValue value;
};

template<typename T>
auto host_object_type() noexcept -> const void* {
    return carven::runtime::hosting::detail::type_key<T>();
}

// A canonical native source name and its exact C++ carrier codec.
struct HostNativeType final {
    std::string name;
    const void* type;
    bool read_borrows;
    std::expected<HostValue, HostError> (*encode)(
        const HostObjectRef&,
        HostLimits&,
        std::size_t
    ) noexcept;
    std::expected<HostObjectRef, HostError> (*decode)(
        const HostValue&,
        HostLimits&,
        std::size_t
    ) noexcept;
};

enum class HostCppOperationKind { Function, Method, Field, Conversion };

enum class HostCallExecution { Synchronous, Async };
enum class HostCallAccess { Read, Write, Take };

struct HostCallParameter final {
    HostCallType type;
    HostCallAccess access;
};

// Typed adapters provide value mappings; source declarations and C++ signatures provide access.
struct HostCallContract final {
    std::string name;
    std::vector<HostCallParameter> parameters;
    HostCallType result;
    HostCallExecution execution;
    std::vector<HostFailureType> failures;
    std::vector<HostNativeType> native_types;
    std::optional<std::string> source_signature;
    std::optional<HostCppOperationKind> cpp_operation;
};

template<typename Response>
class HostRequest;

class HostCallContext;

class HostCall;

struct HostCancelled final {};

using HostOutcome = std::variant<HostValue, HostFailure, HostCancelled>;
using HostInvocationResult = std::variant<HostOutcome, std::unique_ptr<HostCall>>;

using HostTypedCall = auto (*)(
    void*,
    HostCallContext*,
    std::size_t,
    std::span<const HostCallAccess>,
    bool,
    std::span<const HostValue>,
    HostLimits,
    std::span<const HostNativeType>
) noexcept -> std::expected<HostInvocationResult, HostError>;

class HostObjectBinding final {
private:
    struct State final {
        HostCallContract contract;
        HostObjectRef object;
        const void* type;
        HostTypedCall call;
    };

    explicit HostObjectBinding(State state) noexcept;
    State state;
    friend struct HostObjectAccess;
};

// Typed factories own native registration plumbing and value contracts.
enum class HostServiceEffects { None, Environment };

class HostServiceBinding final {
public:
    // Static execution is an explicit host capability, independent of runtime registration.
    auto with_static_execution(HostServiceEffects effects) && noexcept -> HostServiceBinding;

private:
    struct State final {
        HostCallContract contract;
        void* context;
        HostTypedCall call;
        void (*cancel)(void*, std::uint64_t) noexcept;
        carven::runtime::hosting::detail::Binding native;
        std::optional<HostServiceEffects> static_effects;
    };

    explicit HostServiceBinding(State state) noexcept;
    State state;
    friend struct HostServiceAccess;
};

struct HostObjectAccess final {
    static auto make(HostObjectBinding::State state) noexcept -> HostObjectBinding;

    static auto registration(const HostObjectBinding& binding) noexcept
        -> const HostObjectBinding::State&;
};

struct HostServiceAccess final {
    static auto make(HostServiceBinding::State state) noexcept -> HostServiceBinding;

    static auto registration(const HostServiceBinding& binding) noexcept
        -> const HostServiceBinding::State&;
};

struct HostCallOptions final {
    HostOutput output;
    // Environment effects declared by native providers require explicit permission.
    bool allow_host_effects;
    HostLimits limits;
    std::span<const HostNativeType> native_types;
};

auto host_call_options() noexcept -> HostCallOptions;

// A serialized borrow of one call's native services and configuration. The
// invocation keeps it alive through completion. Tokens may outlive it; requests
// are detached when the call is cancelled or the context is destroyed.
class HostCallContext final {
public:
    HostCallContext(const HostCallContext&) = delete;
    HostCallContext(HostCallContext&&) = delete;
    auto operator=(const HostCallContext&) -> HostCallContext& = delete;
    auto operator=(HostCallContext&&) -> HostCallContext& = delete;
    ~HostCallContext() noexcept;
    auto limits() const noexcept -> HostLimits;

private:
    explicit HostCallContext(const HostEnvironment& environment) noexcept;
    std::unique_ptr<HostCallContextState> state;

    friend struct HostContextAccess;
};

// Internal execution owners use this access contract without exposing SDK state.
struct HostContextAccess final {
    static auto make(const HostEnvironment& environment) noexcept -> HostCallContext;
    static auto cancel(HostCallContext& context) noexcept -> void;

    static auto native_driver(HostCallContext& context) noexcept
        -> carven::runtime::execution::Driver&;

    static auto native_scope(HostCallContext& context) noexcept
        -> carven::runtime::hosting::detail::Scope;

    static auto stopped_error(const HostCallContext& context) noexcept -> const HostError*;
    static auto stop(HostCallContext& context, HostError error) noexcept -> void;

    static auto attach(
        HostCallContext& context,
        std::size_t provider,
        const std::shared_ptr<HostRequestState>& mailbox,
        void* arguments,
        std::expected<void, HostError> (*launch)(void*, void*) noexcept
    ) noexcept -> std::expected<void, HostError>;
};

enum class HostTaskStatus { Runnable, Waiting, Finished };

// One execution activation, driven by its execution host. Source and compiled
// asynchronous adapters implement this same protocol.
// Each advance is bounded and nonblocking. Cancellation must eventually finish
// through advance, releasing external borrows before the call is destroyed.
class HostCall {
public:
    virtual ~HostCall() noexcept = default;
    virtual auto advance() noexcept -> HostTaskStatus = 0;

    // False means readiness must be advanced by an external owner.
    virtual auto wait(std::optional<std::int64_t>) noexcept -> bool { return false; }

    virtual auto cancel() noexcept -> void = 0;
    virtual auto take_result() noexcept -> std::expected<HostOutcome, HostError> = 0;
};

// Shared argument-boundary operations used by typed adapters and dynamic values.
auto hosting_error(HostErrorKind kind, std::string message) noexcept -> HostError;

auto host_check_text(std::string_view text, HostLimits limits) noexcept
    -> std::expected<void, HostError>;

struct HostProgramState;
struct HostFunctionState;
struct HostInvocationState;
class HostSourceCall;
struct HostTaskState;
struct HostSessionState;
struct HostEventState;
struct HostEntryAccess;

template<typename Result, typename... Arguments>
struct HostEntrySignature;

// Only typed factories construct entries. Session copies the contract and
// prepares owned arguments before scheduling the callback.
class HostEntry final {
private:
    struct State final {
        HostCallContract contract;
        std::expected<std::unique_ptr<HostCall>, HostError> (*prepare)(
            std::span<const HostValue>,
            const HostCallContract&,
            HostLimits
        ) noexcept;
    };

    explicit HostEntry(State state) noexcept;
    State state;

    template<typename Result, typename... Arguments>
    friend struct HostEntrySignature;

    friend struct HostEntryAccess;
};

struct HostEntryAccess final {
    static auto registration(const HostEntry& entry) noexcept -> const HostEntry::State&;
};

class HostFunction final {
public:
    // Synchronous functions only; each call owns its execution domain.
    // Options and callback contexts are borrowed until this call returns.
    auto call(std::span<const HostValue> arguments, HostCallOptions options) const noexcept
        -> std::expected<HostOutcome, HostError>;

private:
    explicit HostFunction(std::shared_ptr<const HostFunctionState> state) noexcept;
    std::shared_ptr<const HostFunctionState> state;
    friend class HostProgram;
    friend class HostSession;
};

struct HostSource final {
    std::string module_path;
    std::string origin;
    std::string text;
};

// Resolves a canonical module name during load; an empty result leaves the
// dependency unavailable. The callback and its context are borrowed until load returns.
struct HostSourceResolver final {
    void* context;
    std::expected<std::optional<HostSource>, HostError> (*resolve)(
        void*,
        std::string_view
    ) noexcept;
};

class HostProgram final {
public:
    // Loads one explicit source module without native providers.
    // Session::load additionally supplies its explicitly enabled static bindings.
    static auto load(
        std::string_view module_path,
        std::string_view text,
        HostOutput output = {.context = nullptr, .write = nullptr}
    ) noexcept -> std::expected<HostProgram, HostError>;

    // Every explicit source is analyzed. An optional resolver supplies source
    // dependencies outside that batch; entry selection does not discard roots.
    static auto load(
        std::string_view module_path,
        std::span<const HostSource> sources,
        HostOutput output = {.context = nullptr, .write = nullptr},
        HostSourceResolver dependencies = {.context = nullptr, .resolve = nullptr}
    ) noexcept -> std::expected<HostProgram, HostError>;

    auto diagnostics() const noexcept -> std::string_view;

    // Selects a non-private function declared in the chosen entry module.
    // Bare declarations are sufficient; imported bindings are not entry declarations.
    auto function(std::string_view name) const noexcept -> std::expected<HostFunction, HostError>;

private:
    explicit HostProgram(std::shared_ptr<const HostProgramState> state) noexcept;
    std::shared_ptr<const HostProgramState> state;
    friend class HostSession;
    friend class HostFunction;
};

struct HostSessionOptions final {
    HostCallOptions calls;
    std::span<const HostObjectBinding> objects;
    std::span<const HostEntry> entries;
    std::span<const HostServiceBinding> services;
};

auto host_session_options() noexcept -> HostSessionOptions;

enum class HostSessionStatus { Idle, Runnable, Waiting, Closed };

class HostTask final {
public:
    auto status() const noexcept -> HostTaskStatus;
    auto cancel() const noexcept -> void;
    // Null until finished, then stable for the lifetime of this handle.
    auto result() const noexcept -> const std::expected<HostOutcome, HostError>*;

private:
    explicit HostTask(std::shared_ptr<HostTaskState> state) noexcept;
    std::shared_ptr<HostTaskState> state;
    friend class HostSession;
    friend class HostFunction;
    friend class HostEvent;
};

// Copyable posting endpoint. Thread-safe post only queues owned arguments;
// task inspection/cancellation still belongs to the session thread.
class HostEvent final {
public:
    // Null when revoked, closed, or the 256-entry mailbox is full. A returned
    // task observes admission/execution errors as well as completion/cancellation.
    auto post(std::span<const HostValue> arguments) const noexcept -> std::optional<HostTask>;

private:
    explicit HostEvent(std::weak_ptr<HostEventState> state) noexcept;
    std::weak_ptr<HostEventState> state;
    friend class HostSubscription;
};

class HostSubscription final {
public:
    HostSubscription(const HostSubscription&) = delete;
    HostSubscription(HostSubscription&&) noexcept;
    auto operator=(const HostSubscription&) -> HostSubscription& = delete;
    auto operator=(HostSubscription&&) -> HostSubscription& = delete;
    ~HostSubscription() noexcept;
    auto event() const noexcept -> HostEvent;
    // Cancels queued notifications. Already admitted calls finish independently.
    auto cancel() noexcept -> void;

private:
    explicit HostSubscription(std::shared_ptr<HostEventState> state) noexcept;
    std::shared_ptr<HostEventState> state;
    friend class HostSession;
};

// Session/task/object/subscription controls are serialized by the host. Provider
// and output callbacks must not reenter them. Request completion and event post
// are thread-safe and never resume source inline.
class HostSession final {
public:
    // Starts with registered compiled entries; source is first analyzed on reload.
    static auto create(std::string_view module_path, HostSessionOptions options) noexcept
        -> std::expected<HostSession, HostError>;

    static auto create(const HostProgram& program, HostSessionOptions options) noexcept
        -> std::expected<HostSession, HostError>;

    static auto load(
        std::string_view module_path,
        std::string_view text,
        HostSessionOptions options
    ) noexcept -> std::expected<HostSession, HostError>;

    static auto load(
        std::string_view module_path,
        std::span<const HostSource> batch,
        HostSessionOptions options,
        HostSourceResolver dependencies = {.context = nullptr, .resolve = nullptr}
    ) noexcept -> std::expected<HostSession, HostError>;

    // Replaces registered entries with matching non-private source functions.
    // Omitted or private declarations leave the existing implementation in place.
    // All candidates are checked before publication. Admitted calls keep their code.
    auto reload(std::string_view text) noexcept -> std::expected<void, HostError>;
    auto reload(
        std::span<const HostSource> batch,
        HostSourceResolver dependencies = {.context = nullptr, .resolve = nullptr}
    ) noexcept -> std::expected<void, HostError>;
    // Borrow ends on successful reload or close.
    auto diagnostics() const noexcept -> std::string_view;
    HostSession(const HostSession&) = delete;
    HostSession(HostSession&&) noexcept;
    auto operator=(const HostSession&) -> HostSession& = delete;
    auto operator=(HostSession&&) -> HostSession& = delete;
    ~HostSession() noexcept;

    auto submit(const HostFunction& function, std::span<const HostValue> arguments) noexcept
        -> std::expected<HostTask, HostError>;

    // Selects the current compiled/source implementation at submission time.
    auto submit(std::string_view name, std::span<const HostValue> arguments) noexcept
        -> std::expected<HostTask, HostError>;

    // Subscriptions resolve entry names when polled, against current code.
    auto subscribe(HostObjectRef owner, std::string_view entry) noexcept
        -> std::expected<HostSubscription, HostError>;

    // At most quantum event admissions/call slices. Native callbacks
    // must return promptly; event producers cannot monopolize the entire quantum.
    auto poll(std::size_t quantum) noexcept -> HostSessionStatus;
    auto active_count() const noexcept -> std::size_t;
    // Cancels and drains invocations, detaching providers before returning.
    auto close() noexcept -> void;

private:
    explicit HostSession(std::unique_ptr<HostSessionState> state) noexcept;
    std::unique_ptr<HostSessionState> state;
};
