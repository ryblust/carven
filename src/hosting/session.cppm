module;

#include <carven/hosting/api.hpp>

module carven:hosting.session;

import :hosting.event;
import :hosting.program;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.program;
import std;

extern "C++" {

struct HostProvider final {
    void* context;
    HostTypedCall call;
    void (*cancel)(void*, std::uint64_t) noexcept;
    std::optional<HostObjectRef> object;
    const void* object_type;
    HostCallContract contract;
};

struct HostEnvironment final {
    std::vector<HostProvider> providers;
    std::vector<carven::runtime::hosting::detail::Binding> native;
    HostOutput output;
    HostEffects host_effects;
    HostLimits limits;
    std::vector<HostNativeType> native_types;
};

// Session-owned, at a stable address. Entry bindings and active invocations borrow it.
struct HostLoadedProgram final {
    std::shared_ptr<const HostProgramState> owner;
    std::vector<std::pair<CallableID, std::size_t>> providers;
    std::size_t active;
};

struct HostSourceImplementation final {
    HostLoadedProgram* program;
    FunctionID function;
};

using HostEntryPrepare =
    decltype(HostEntryAccess::registration(std::declval<const HostEntry&>()).prepare);

using HostEntryImplementation = std::variant<HostEntryPrepare, HostSourceImplementation>;

struct HostBoundEntry final {
    HostCallContract contract;
    HostEntryImplementation implementation;
};

// Handles share only this mailbox. The active invocation borrow is common to all
// implementations and is severed before execution storage is released.
struct HostTaskState final {
    HostInvocationState* invocation;
    HostTaskStatus status;
    std::optional<std::expected<HostOutcome, HostError>> result;
    // Cooperative cleanup may be requested by the caller or an execution stop;
    // the invocation context retains the original execution error separately.
    bool cancellation_requested;
};

class HostSourceCall final : public HostCall {
public:
    HostSourceCall(
        const HostEnvironment& environment,
        HostCallContext& host,
        HostLoadedProgram& program,
        FunctionID function,
        std::vector<ExecutionValue> arguments
    ) noexcept;

    ~HostSourceCall() noexcept override;
    auto advance() noexcept -> HostTaskStatus override;
    auto cancel() noexcept -> void override;
    auto take_result() noexcept -> std::expected<HostOutcome, HostError> override;

private:
    const HostEnvironment& environment;
    HostCallContext& host;
    HostLoadedProgram& program;
    FunctionID function;

    std::function<std::expected<
        ExecutionHostResult,
        ExecutionCallFailure>(ExecutionCallSite, std::span<ExecutionValue>, ProgramOriginID)>
        host_call;

    std::function<void(ExecutionOutputStream, std::string_view)> output;
    std::unique_ptr<InterpreterInvocation> engine;

    auto invoke(
        ExecutionCallSite target,
        std::span<ExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure>;
};

// The Session owns one lifecycle for every implementation: context, prepared
// activation, cancellation, terminal result and cleanup.
struct HostInvocationState final {
    const HostEnvironment& environment;
    HostCallContext host;
    const HostCallContract* entry;
    std::shared_ptr<HostTaskState> completion;
    std::unique_ptr<HostCall> operation;
    bool started;

    HostInvocationState(
        const HostEnvironment& environment,
        const HostCallContract* entry,
        std::shared_ptr<HostTaskState> completion
    ) noexcept;

    ~HostInvocationState() noexcept;
    auto advance() noexcept -> void;
    auto cancel() noexcept -> void;
    auto finish(std::expected<HostOutcome, HostError> result) noexcept -> void;
};

struct HostSessionState final {
    HostEnvironment environment;
    std::vector<HostBoundEntry> entries;
    std::string module_path;
    std::list<HostLoadedProgram> programs;
    HostLoadedProgram* latest;
    std::deque<std::unique_ptr<HostInvocationState>> active;
    std::shared_ptr<HostEventQueue> events;
    std::vector<std::shared_ptr<HostEventState>> subscriptions;
    bool prefer_event;
    bool closed;
    bool dispatching;

    auto publish(
        std::shared_ptr<const HostProgramState> program,
        std::vector<std::pair<CallableID, std::size_t>> providers,
        std::span<const std::pair<std::size_t, FunctionID>> replacements
    ) noexcept -> void;

    auto collect_programs() noexcept -> void;

    auto admit(
        HostEntryImplementation implementation,
        const HostCallContract* entry,
        std::span<const HostValue> arguments,
        std::shared_ptr<HostTaskState> completion
    ) noexcept -> std::expected<std::shared_ptr<HostTaskState>, HostError>;

    auto submit(
        HostLoadedProgram& program,
        FunctionID function,
        std::span<const HostValue> arguments,
        std::shared_ptr<HostTaskState> completion = {}
    ) noexcept -> std::expected<std::shared_ptr<HostTaskState>, HostError>;

    auto lookup(std::string_view name) const noexcept -> std::expected<void, HostError>;

    auto submit(
        std::string_view name,
        std::span<const HostValue> arguments,
        std::shared_ptr<HostTaskState> completion = {}
    ) noexcept -> std::expected<std::shared_ptr<HostTaskState>, HostError>;

    auto dispatch(HostQueuedEvent event) noexcept -> void;
    auto pump(std::size_t quantum) noexcept -> HostSessionStatus;
};

} // extern "C++"
