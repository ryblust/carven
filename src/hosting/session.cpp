module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/operation.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.session.impl;

import :hosting.event;
import :hosting.operation;
import :hosting.program;
import :hosting.session;
import :hosting.value;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :support.invariant;
import std;

extern "C++" {

HostCall::~HostCall() noexcept = default;

namespace {

auto session_error(HostErrorKind kind, std::string message) noexcept -> std::unexpected<HostError> {
    return std::unexpected(hosting_error(kind, std::move(message)));
}

auto entry_name(const HostBoundEntry& entry) noexcept -> std::string_view {
    return entry.contract.name;
}

auto resolve_providers(
    const HostProgramState& source,
    std::span<const HostProvider> registrations
) noexcept -> std::expected<std::vector<std::pair<CallableID, std::size_t>>, HostError> {
    auto resolved = std::vector<std::pair<CallableID, std::size_t>>();
    const auto& program = source.program;
    for (const auto entry : program.declarations().callables()) {
        const auto* imported = std::get_if<CppImportImplementation>(&entry.value.implementation);
        if (imported == nullptr) {
            continue;
        }
        const auto function = program.declarations().function_for_callable(entry.id);
        if (!function) {
            invariant_violation("native declaration has no source function");
        }
        const auto& declaration = program.declarations().function(*function);
        const auto& module_decl = program.declarations().module_decl(declaration.module_id);
        const auto target = std::format(
            "{}.{}",
            program.provenance().module_record(module_decl.provenance_module).path.value(),
            program.provenance().spelling(declaration.name)
        );
        const auto found = std::ranges::find(
            registrations,
            target,
            [](const auto& provider) static noexcept -> const std::string& {
                return provider.contract.name;
            }
        );
        if (found == registrations.end()) {
            // Loaded Crafts may contain native capabilities this host does not expose.
            // Reached calls still fail at the invocation boundary if unregistered.
            continue;
        }
        const auto& signature = program.callable_signatures().signature(entry.value.signature);
        if (!hosting_signature_supported(program, signature, true, true)
            || !hosting_contract_matches(program, entry.value.signature, found->contract)) {
            return session_error(
                HostErrorKind::Signature,
                "host binding does not match the native declaration: " + target
            );
        }
        resolved.emplace_back(entry.id, static_cast<std::size_t>(found - registrations.begin()));
    }
    return resolved;
}

auto make_session_state(std::string_view module_path, HostSessionOptions options) noexcept
    -> std::expected<std::unique_ptr<HostSessionState>, HostError> {
    if (!CanonicalModulePath::from_value(module_path)) {
        return session_error(HostErrorKind::Input, "invalid source module path");
    }
    auto registrations = std::vector<HostProvider>();
    for (const auto& registration : options.objects) {
        const auto& binding = HostObjectAccess::registration(registration);
        if (binding.contract.name.empty() || !binding.object.alive()) {
            return session_error(
                HostErrorKind::Arguments,
                "object binding requires a live object and name"
            );
        }
        registrations.push_back(
            {.context = nullptr,
             .call = binding.call,
             .cancel = nullptr,
             .object = binding.object,
             .object_type = binding.type,
             .contract = binding.contract}
        );
    }
    auto native = std::vector<carven::runtime::hosting::detail::Binding>(registrations.size());
    for (const auto& binding : options.services) {
        const auto& service = HostServiceAccess::registration(binding);
        if (service.contract.name.empty()
            || (!service.contract.cpp_operation
                && ((!service.contract.source_signature
                     || service.contract.source_signature->empty())
                    || service.native.target == nullptr))) {
            return session_error(
                HostErrorKind::Arguments,
                "host service requires a contract and binding"
            );
        }
        if (!service.contract.cpp_operation
            && std::ranges::any_of(
                service.contract.parameters,
                [](const auto& parameter) static noexcept {
                    return parameter.access == HostCallAccess::Write;
                }
            )) {
            return session_error(
                HostErrorKind::Signature,
                "host service Write parameters are not supported"
            );
        }
        registrations.push_back(
            {.context = service.context,
             .call = service.call,
             .cancel = service.cancel,
             .object = std::nullopt,
             .object_type = nullptr,
             .contract = service.contract}
        );
        native.push_back(service.native);
    }
    const auto valid_contract = [](const HostCallContract& contract) static noexcept {
        if (contract.name.empty()
            || (contract.source_signature && contract.source_signature->empty())
            || contract.result.describe == nullptr
            || !std::ranges::all_of(contract.parameters, [](const auto& parameter) static noexcept {
                   return parameter.type.describe != nullptr;
               })) {
            return false;
        }
        for (auto index = 0uz; index < contract.failures.size(); ++index) {
            const auto& failure = contract.failures[index];
            if (failure.module_path.empty() || failure.name.empty()) {
                return false;
            }
            for (auto previous = 0uz; previous < index; ++previous) {
                if (contract.failures[previous].module_path == failure.module_path
                    && contract.failures[previous].name == failure.name) {
                    return false;
                }
            }
        }
        return true;
    };
    for (const auto& provider : registrations) {
        if (!valid_contract(provider.contract)) {
            return session_error(
                HostErrorKind::Signature,
                "host binding requires a valid call contract and registered types"
            );
        }
    }
    for (auto index = 0uz; index < registrations.size(); ++index) {
        for (auto previous = 0uz; previous < index; ++previous) {
            const auto& left = registrations[previous].contract;
            const auto& right = registrations[index].contract;
            const auto same_native_signature = left.cpp_operation
                && right.cpp_operation
                && left.cpp_operation == right.cpp_operation
                && left.result.describe().native_type == right.result.describe().native_type
                && std::ranges::equal(left.parameters,
                                      right.parameters,
                                      [](const auto& a, const auto& b) static noexcept {
                                          return a.access == b.access
                                              && a.type.describe().native_type
                                              == b.type.describe().native_type;
                                      });
            if (left.name == right.name
                && ((!left.cpp_operation || !right.cpp_operation) || same_native_signature)) {
                return session_error(
                    HostErrorKind::Arguments,
                    "duplicate session binding: " + registrations[index].contract.name
                );
            }
        }
    }
    auto native_types = std::vector<HostNativeType>(
        options.calls.native_types.begin(),
        options.calls.native_types.end()
    );
    const auto collect_native = [&](const HostCallContract& contract) noexcept -> bool {
        for (const auto& type : contract.native_types) {
            const auto found = std::ranges::find(native_types, type.name, &HostNativeType::name);
            if (found == native_types.end()) {
                native_types.push_back(type);
            } else if (found->type != type.type) {
                return false;
            } else if (found->encode == nullptr || found->decode == nullptr) {
                *found = type;
            }
        }
        return true;
    };
    for (const auto& provider : registrations) {
        if (!collect_native(provider.contract)) {
            return session_error(
                HostErrorKind::Signature,
                "native source name has conflicting carrier types"
            );
        }
    }
    for (const auto& entry : options.entries) {
        if (!collect_native(HostEntryAccess::registration(entry).contract)) {
            return session_error(
                HostErrorKind::Signature,
                "native source name has conflicting carrier types"
            );
        }
    }
    for (auto index = 0uz; index < native_types.size(); ++index) {
        const auto& type = native_types[index];
        if (type.name.empty() || type.type == nullptr) {
            return session_error(
                HostErrorKind::Arguments,
                "native type requires a name and identity"
            );
        }
        for (auto previous = 0uz; previous < index; ++previous) {
            if (native_types[previous].name == type.name) {
                return session_error(
                    HostErrorKind::Arguments,
                    "duplicate native type: " + type.name
                );
            }
        }
    }
    auto entries = std::vector<HostBoundEntry>();
    for (const auto& binding : options.entries) {
        const auto& registration = HostEntryAccess::registration(binding);
        const auto& entry = registration.contract;
        if (!valid_contract(entry)
            || std::ranges::any_of(entry.parameters, [](const auto& parameter) static noexcept {
                   return parameter.access == HostCallAccess::Write;
               })) {
            return session_error(
                HostErrorKind::Signature,
                "compiled entry requires a name and registered signature"
            );
        }
        if (std::ranges::find(entries, entry.name, entry_name) != entries.end()) {
            return session_error(
                HostErrorKind::Arguments,
                "duplicate compiled entry: " + std::string(entry.name)
            );
        }
        entries.push_back({.contract = entry, .implementation = registration.prepare});
    }
    return std::make_unique<HostSessionState>(HostSessionState {
        .environment =
            {.providers = std::move(registrations),
             .native = std::move(native),
             .output = options.calls.output,
             .host_effects =
                 options.calls.allow_host_effects ? HostEffects::Allow : HostEffects::Deny,
             .limits = options.calls.limits,
             .native_types = std::move(native_types)},
        .entries = std::move(entries),
        .module_path = std::string(module_path),
        .programs = {},
        .latest = nullptr,
        .active = {},
        .events = std::make_shared<HostEventQueue>(),
        .subscriptions = {},
        .prefer_event = true,
        .closed = false,
        .dispatching = false
    });
}

auto resolve_entries(
    const HostProgramState& program,
    std::span<const HostBoundEntry> entries
) noexcept -> std::expected<std::vector<std::pair<std::size_t, FunctionID>>, HostError> {
    auto replacements = std::vector<std::pair<std::size_t, FunctionID>>();
    for (auto index = 0uz; index < entries.size(); ++index) {
        const auto& entry = entries[index].contract;
        auto function = program.function(entry.name);
        if (!function) {
            if (function.error().kind == HostErrorKind::Lookup) {
                continue;
            }
            return std::unexpected(std::move(function.error()));
        }
        const auto& source = program.program;
        const auto signature = source.declarations()
                                   .callable(source.declarations().function(*function).callable)
                                   .signature;
        if (!hosting_contract_matches(source, signature, entry)) {
            return session_error(
                HostErrorKind::Signature,
                "replacement function does not match the hosted entry: " + entry.name
            );
        }
        replacements.emplace_back(index, *function);
    }
    return replacements;
}

} // namespace

HostSourceCall::HostSourceCall(
    const HostEnvironment& environment,
    HostCallContext& host,
    HostLoadedProgram& program,
    FunctionID function,
    std::vector<ExecutionValue> arguments
) noexcept
    : environment(environment),
      host(host),
      program(program),
      function(function),
      host_call([this](
                    ExecutionCallSite target,
                    std::span<ExecutionValue> values,
                    ProgramOriginID origin
                ) noexcept { return invoke(target, values, origin); }),
      output([this](ExecutionOutputStream stream, std::string_view bytes) noexcept {
          if (this->environment.output.write != nullptr) {
              this->environment.output.write(
                  this->environment.output.context,
                  stream == ExecutionOutputStream::Error,
                  bytes
              );
          }
      }),
      engine() {
    const auto limits = environment.limits;
    engine = std::make_unique<InterpreterInvocation>(
        program.owner->program,
        function,
        std::move(arguments),
        output,
        InterpreterOptions {
            .limits =
                {.steps = limits.steps,
                 .byte_work = limits.byte_work,
                 .aggregate_work = limits.aggregate_work},
            .trace = {},
            .report = {},
            .host_call = host_call
        },
        environment.host_effects
    );
    ++program.active;
}

HostSourceCall::~HostSourceCall() noexcept {
    engine.reset();
    --program.active;
}

auto HostSourceCall::invoke(
    ExecutionCallSite target,
    std::span<ExecutionValue> arguments,
    ProgramOriginID origin
) noexcept -> std::expected<ExecutionHostResult, ExecutionCallFailure> {
    if (const auto* expression = std::get_if<const SemanticExpression*>(&target)) {
        return host_cpp_call(
            program.owner->program,
            environment.providers,
            **expression,
            arguments,
            environment.limits,
            origin
        );
    }
    const auto* selected = std::get_if<CallableID>(&target);
    if (selected == nullptr) {
        invariant_violation("host call has no callable or native expression");
    }
    const auto callable = *selected;
    const auto found =
        std::ranges::find(program.providers, callable, [](const auto& item) static noexcept {
            return item.first;
        });
    if (found == program.providers.end()) {
        return hosting_service_error(origin, "missing session host binding");
    }
    const auto& provider = environment.providers[found->second];
    const auto& source = program.owner->program;
    const auto& signature =
        source.callable_signatures().signature(source.declarations().callable(callable).signature);
    if (arguments.size() != signature.parameters.size()) {
        invariant_violation("session host call lost its checked arguments");
    }
    auto context = provider.context;
    if (provider.object) {
        context = carven::runtime::hosting::detail::ObjectAccess::resolve(
            *provider.object,
            provider.object_type
        );
        if (context == nullptr) {
            return hosting_service_error(
                origin,
                provider.object->alive() ? "host method does not match the registered object type"
                                         : "host object is no longer alive"
            );
        }
    }
    return hosting_invoke(
        source,
        provider.call,
        provider.contract.result,
        context,
        &host,
        found->second,
        signature.parameters,
        signature.result,
        source.failure_sets().failure_set(signature.failures).members,
        arguments,
        environment.limits,
        environment.native_types,
        origin
    );
}

auto HostSourceCall::advance() noexcept -> HostTaskStatus {
    // A host call slice advances a bounded group of interpreter continuations.
    // Events and other calls regain control between slices; execution limits
    // still account for each source operation inside the interpreter.
    constexpr auto continuation_quantum = 256uz;
    const auto progress = engine->advance(continuation_quantum);
    if (progress == ExecutionProgress::Complete) {
        return HostTaskStatus::Finished;
    }
    return progress == ExecutionProgress::Runnable ? HostTaskStatus::Runnable
                                                   : HostTaskStatus::Waiting;
}

auto HostSourceCall::cancel() noexcept -> void {
    engine->cancel();
}

auto HostSourceCall::take_result() noexcept -> std::expected<HostOutcome, HostError> {
    return program.owner
        ->complete(function, engine->take_result(), environment.limits, environment.native_types);
}

HostInvocationState::HostInvocationState(
    const HostEnvironment& environment,
    const HostCallContract* entry,
    std::shared_ptr<HostTaskState> completion
) noexcept
    : environment(environment),
      host(environment),
      entry(entry),
      completion(std::move(completion)),
      operation(),
      started(false) {
    this->completion->invocation = this;
}

HostInvocationState::~HostInvocationState() noexcept {
    completion->invocation = nullptr;
}

auto HostInvocationState::finish(std::expected<HostOutcome, HostError> result) noexcept -> void {
    if (const auto* error = host.stopped_error()) {
        result = std::unexpected(*error);
    }
    completion->result.emplace(std::move(result));
    completion->status = HostTaskStatus::Finished;
    completion->invocation = nullptr;
    operation.reset();
    host.cancel();
}

auto HostInvocationState::advance() noexcept -> void {
    const auto scope = host.native_scope();
    if (!started) {
        started = true;
        if (completion->cancellation_requested) {
            finish(HostOutcome(HostCancelled {}));
            return;
        }
    }
    if (host.stopped_error()) {
        cancel();
    }
    const auto progress = operation->advance();
    if (progress == HostTaskStatus::Finished) {
        finish(operation->take_result());
    } else if (host.stopped_error() && !completion->cancellation_requested) {
        // Stop the whole call, including sibling requests and native children.
        // Keep the context's original error as the reason for this cleanup.
        cancel();
    } else {
        completion->status = progress;
    }
}

auto HostInvocationState::cancel() noexcept -> void {
    if (completion->cancellation_requested) {
        return;
    }
    completion->cancellation_requested = true;
    completion->status = HostTaskStatus::Runnable;
    host.cancel();
    if (operation) {
        operation->cancel();
    }
}

auto HostSessionState::lookup(std::string_view name) const noexcept
    -> std::expected<void, HostError> {
    if (std::ranges::find(entries, name, entry_name) != entries.end()) {
        return {};
    }
    if (latest) {
        auto function = latest->owner->function(name);
        if (!function) {
            return std::unexpected(std::move(function.error()));
        }
        return {};
    }
    return session_error(HostErrorKind::Lookup, "hosted entry was not found");
}

auto HostSessionState::submit(
    std::string_view name,
    std::span<const HostValue> arguments,
    std::shared_ptr<HostTaskState> completion
) noexcept -> std::expected<std::shared_ptr<HostTaskState>, HostError> {
    const auto entry = std::ranges::find(entries, name, entry_name);
    if (entry == entries.end()) {
        if (!latest) {
            return session_error(HostErrorKind::Lookup, "hosted entry was not found");
        }
        auto function = latest->owner->function(name);
        if (!function) {
            return std::unexpected(std::move(function.error()));
        }
        return submit(*latest, *function, arguments, std::move(completion));
    }
    return admit(entry->implementation, &entry->contract, arguments, std::move(completion));
}

auto HostSessionState::publish(
    std::shared_ptr<const HostProgramState> program,
    std::vector<std::pair<CallableID, std::size_t>> providers,
    std::span<const std::pair<std::size_t, FunctionID>> replacements
) noexcept -> void {
    // All resolution and contract checks have finished before changing any binding.
    programs.push_back(
        {.owner = std::move(program), .providers = std::move(providers), .active = 0uz}
    );
    latest = &programs.back();
    for (const auto& [entry, function] : replacements) {
        entries[entry].implementation =
            HostSourceImplementation {.program = latest, .function = function};
    }
    collect_programs();
}

auto HostSessionState::collect_programs() noexcept -> void {
    std::erase_if(programs, [this](const auto& program) noexcept {
        if (&program == latest || program.active != 0uz) {
            return false;
        }
        return std::ranges::none_of(entries, [&](const auto& entry) noexcept {
            const auto* source = std::get_if<HostSourceImplementation>(&entry.implementation);
            return source && source->program == &program;
        });
    });
}

auto HostSessionState::dispatch(HostQueuedEvent event) noexcept -> void {
    const auto subscription = event.subscription.lock();
    if (!subscription
        || !subscription->active
        || !subscription->owner.alive()
        || event.completion->cancellation_requested) {
        event.completion->result.emplace(HostOutcome(HostCancelled {}));
        event.completion->status = HostTaskStatus::Finished;
        return;
    }
    auto admitted = submit(subscription->entry, event.arguments, event.completion);
    if (!admitted) {
        event.completion->result.emplace(std::unexpected(std::move(admitted.error())));
        event.completion->status = HostTaskStatus::Finished;
    }
}

auto HostSessionState::pump(std::size_t quantum) noexcept -> HostSessionStatus {
    if (dispatching) {
        invariant_violation("hosting session controls reentered from a callback");
    }
    dispatching = true;
    auto waiting = 0uz;
    while (quantum != 0uz) {
        if (prefer_event || active.empty()) {
            auto event = events->take();
            if (event) {
                dispatch(std::move(*event));
                --quantum;
                prefer_event = false;
                waiting = 0uz;
                continue;
            }
        }
        if (active.empty()) {
            break;
        }
        auto invocation = std::move(active.front());
        active.pop_front();
        invocation->advance();
        --quantum;
        prefer_event = true;
        if (invocation->completion->status == HostTaskStatus::Finished) {
            invocation.reset();
            collect_programs();
            waiting = 0uz;
            continue;
        }
        waiting = invocation->completion->status == HostTaskStatus::Waiting ? waiting + 1uz : 0uz;
        active.push_back(std::move(invocation));
        if (waiting >= active.size() && events->size() == 0uz) {
            break;
        }
    }
    std::erase_if(subscriptions, [](const auto& subscription) static noexcept {
        return !subscription->active;
    });
    dispatching = false;
    if (events->size() != 0uz) {
        return HostSessionStatus::Runnable;
    }
    if (active.empty()) {
        return HostSessionStatus::Idle;
    }
    return std::ranges::any_of(
               active,
               [](const auto& task) static noexcept {
                   return task->completion->status == HostTaskStatus::Runnable;
               }
           )
        ? HostSessionStatus::Runnable
        : HostSessionStatus::Waiting;
}

auto HostSessionState::submit(
    HostLoadedProgram& program,
    FunctionID function,
    std::span<const HostValue> arguments,
    std::shared_ptr<HostTaskState> completion
) noexcept -> std::expected<std::shared_ptr<HostTaskState>, HostError> {
    const auto& source = program.owner->program;
    const auto name = source.provenance().spelling(source.declarations().function(function).name);
    const auto entry = std::ranges::find(entries, name, entry_name);
    return admit(
        HostSourceImplementation {.program = &program, .function = function},
        entry == entries.end() ? nullptr : &entry->contract,
        arguments,
        std::move(completion)
    );
}

auto HostSessionState::admit(
    HostEntryImplementation implementation,
    const HostCallContract* entry,
    std::span<const HostValue> arguments,
    std::shared_ptr<HostTaskState> completion
) noexcept -> std::expected<std::shared_ptr<HostTaskState>, HostError> {
    auto inputs = std::vector<ExecutionValue>();
    const auto* source = std::get_if<HostSourceImplementation>(&implementation);
    if (source) {
        const auto& program = source->program->owner->program;
        const auto& signature = program.callable_signatures().signature(
            program.declarations()
                .callable(program.declarations().function(source->function).callable)
                .signature
        );
        if (arguments.size() != signature.parameters.size()) {
            return session_error(
                HostErrorKind::Arguments,
                "argument count does not match the function"
            );
        }
        auto budget = environment.limits;
        for (auto index = 0uz; index < arguments.size(); ++index) {
            auto converted = hosting_import(
                program,
                arguments[index],
                signature.parameters[index].type,
                budget,
                environment.native_types
            );
            if (!converted) {
                return std::unexpected(std::move(converted.error()));
            }
            inputs.push_back(std::move(*converted));
        }
    }
    if (!completion) {
        completion = std::make_shared<HostTaskState>(HostTaskState {
            .invocation = nullptr,
            .status = HostTaskStatus::Runnable,
            .result = std::nullopt,
            .cancellation_requested = false
        });
    }
    auto invocation = std::make_unique<HostInvocationState>(environment, entry, completion);
    if (source) {
        invocation->operation = std::make_unique<HostSourceCall>(
            environment,
            invocation->host,
            *source->program,
            source->function,
            std::move(inputs)
        );
    } else {
        const auto* prepare = std::get_if<HostEntryPrepare>(&implementation);
        if (prepare == nullptr || entry == nullptr) {
            invariant_violation("compiled entry lost its typed preparation");
        }
        auto prepared = (*prepare)(arguments, *entry, environment.limits);
        if (!prepared) {
            return std::unexpected(std::move(prepared.error()));
        }
        invocation->operation = std::move(*prepared);
    }
    active.push_back(std::move(invocation));
    return completion;
}

auto host_session_options() noexcept -> HostSessionOptions {
    return {.calls = host_call_options(), .objects = {}, .entries = {}, .services = {}};
}

HostSession::HostSession(std::unique_ptr<HostSessionState> state) noexcept
    : state(std::move(state)) {}

HostSession::HostSession(HostSession&&) noexcept = default;

HostSession::~HostSession() noexcept {
    close();
}

auto HostSession::create(std::string_view module_path, HostSessionOptions options) noexcept
    -> std::expected<HostSession, HostError> {
    auto state = make_session_state(module_path, options);
    if (!state) {
        return std::unexpected(std::move(state.error()));
    }
    return HostSession(std::move(*state));
}

auto HostSession::create(const HostProgram& program, HostSessionOptions options) noexcept
    -> std::expected<HostSession, HostError> {
    if (!program.state) {
        return session_error(HostErrorKind::Lookup, "program handle was moved from");
    }
    auto state = make_session_state(program.state->module_path, options);
    if (!state) {
        return std::unexpected(std::move(state.error()));
    }
    auto& session = **state;
    auto resolved = resolve_providers(*program.state, session.environment.providers);
    if (!resolved) {
        return std::unexpected(std::move(resolved.error()));
    }
    auto checked = resolve_entries(*program.state, session.entries);
    if (!checked) {
        return std::unexpected(std::move(checked.error()));
    }
    session.publish(program.state, std::move(*resolved), *checked);
    return HostSession(std::move(*state));
}

auto HostSession::load(
    std::string_view module_path,
    std::string_view text,
    HostSessionOptions options
) noexcept -> std::expected<HostSession, HostError> {
    auto program = HostProgram::load(module_path, text, options.calls.output);
    if (!program) {
        return std::unexpected(std::move(program.error()));
    }
    return create(*program, options);
}

auto HostSession::reload(std::string_view text) noexcept -> std::expected<void, HostError> {
    if (!state || state->closed) {
        return session_error(HostErrorKind::Input, "hosting session is closed");
    }
    if (state->dispatching) {
        return session_error(HostErrorKind::Input, "hosting session controls cannot be reentered");
    }
    // Static output can call user code too. Keep lifecycle reentry excluded during load.
    state->dispatching = true;
    auto program = HostProgram::load(state->module_path, text, state->environment.output);
    if (!program) {
        state->dispatching = false;
        return std::unexpected(std::move(program.error()));
    }
    auto resolved = resolve_providers(*program->state, state->environment.providers);
    if (!resolved) {
        state->dispatching = false;
        return std::unexpected(std::move(resolved.error()));
    }
    auto checked = resolve_entries(*program->state, state->entries);
    if (!checked) {
        state->dispatching = false;
        return std::unexpected(std::move(checked.error()));
    }
    state->publish(std::move(program->state), std::move(*resolved), *checked);
    state->dispatching = false;
    return {};
}

auto HostSession::diagnostics() const noexcept -> std::string_view {
    return state && state->latest ? std::string_view(state->latest->owner->diagnostics)
                                  : std::string_view();
}

auto HostSession::submit(
    const HostFunction& function,
    std::span<const HostValue> arguments
) noexcept -> std::expected<HostTask, HostError> {
    if (!state || state->closed) {
        return session_error(HostErrorKind::Input, "hosting session is closed");
    }
    if (state->dispatching) {
        return session_error(HostErrorKind::Input, "hosting session controls cannot be reentered");
    }
    if (!state->latest || !function.state || function.state->owner != state->latest->owner) {
        return session_error(
            HostErrorKind::Arguments,
            "function does not belong to the session's latest program; submit by name after reload"
        );
    }
    auto completion = state->submit(*state->latest, function.state->function, arguments);
    if (!completion) {
        return std::unexpected(std::move(completion.error()));
    }
    return HostTask(std::move(*completion));
}

auto HostSession::submit(std::string_view name, std::span<const HostValue> arguments) noexcept
    -> std::expected<HostTask, HostError> {
    if (!state || state->closed) {
        return session_error(HostErrorKind::Input, "hosting session is closed");
    }
    if (state->dispatching) {
        return session_error(HostErrorKind::Input, "hosting session controls cannot be reentered");
    }
    auto completion = state->submit(name, arguments);
    if (!completion) {
        return std::unexpected(std::move(completion.error()));
    }
    return HostTask(std::move(*completion));
}

auto HostSession::subscribe(HostObjectRef owner, std::string_view entry) noexcept
    -> std::expected<HostSubscription, HostError> {
    if (!state || state->closed || state->dispatching) {
        return session_error(HostErrorKind::Input, "session is closed or dispatching");
    }
    if (!owner.alive()) {
        return session_error(HostErrorKind::Arguments, "event owner is no longer alive");
    }
    auto checked = state->lookup(entry);
    if (!checked) {
        return std::unexpected(std::move(checked.error()));
    }
    auto subscription = std::make_shared<HostEventState>(HostEventState {
        .queue = state->events,
        .owner = std::move(owner),
        .entry = std::string(entry),
        .active = true
    });
    std::erase_if(state->subscriptions, [](const auto& previous) static noexcept {
        return !previous->active;
    });
    if (!carven::runtime::hosting::detail::ObjectAccess::observe(
            subscription->owner,
            subscription,
            [](const std::shared_ptr<void>& target) static noexcept {
                std::static_pointer_cast<HostEventState>(target)->cancel();
            }
        )) {
        return session_error(HostErrorKind::Arguments, "event owner is no longer alive");
    }
    state->subscriptions.push_back(subscription);
    return HostSubscription(std::move(subscription));
}

auto HostSession::poll(std::size_t quantum) noexcept -> HostSessionStatus {
    return !state || state->closed ? HostSessionStatus::Closed : state->pump(quantum);
}

auto HostSession::active_count() const noexcept -> std::size_t {
    return state ? state->active.size() + state->events->size() : 0uz;
}

auto HostSession::close() noexcept -> void {
    if (!state || state->closed) {
        return;
    }
    if (state->dispatching) {
        invariant_violation("hosting session closed from a callback");
    }
    state->closed = true;
    state->events->close();
    for (const auto& subscription : state->subscriptions) {
        subscription->cancel();
    }
    state->subscriptions.clear();
    for (auto& invocation : state->active) {
        invocation->cancel();
    }
    while (!state->active.empty()) {
        state->pump(256uz);
    }
    state->latest = nullptr;
    state->entries.clear();
    state->programs.clear();
    state->environment.providers.clear();
    state->environment.output = {.context = nullptr, .write = nullptr};
}

HostTask::HostTask(std::shared_ptr<HostTaskState> state) noexcept
    : state(std::move(state)) {}

auto HostTask::status() const noexcept -> HostTaskStatus {
    return state ? state->status : HostTaskStatus::Finished;
}

auto HostTask::cancel() const noexcept -> void {
    if (state && !state->result) {
        if (state->invocation) {
            state->invocation->cancel();
        } else {
            state->cancellation_requested = true;
        }
    }
}

auto HostTask::result() const noexcept -> const std::expected<HostOutcome, HostError>* {
    return state && state->result ? std::addressof(*state->result) : nullptr;
}

} // extern "C++"
