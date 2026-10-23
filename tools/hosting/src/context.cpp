module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/request.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.context.impl;

import :hosting.environment;
import :hosting.value;
import :semantic.evaluation.limits;
import std;

extern "C++" {

struct HostCallContextState final {
    const HostEnvironment& environment;
    std::vector<std::weak_ptr<HostRequestState>> requests;
    bool cancelled;
    std::optional<HostError> stopped_error;
};

HostCallContext::HostCallContext(const HostEnvironment& environment) noexcept
    : state(
          std::make_unique<HostCallContextState>(HostCallContextState {
              .environment = environment,
              .requests = {},
              .cancelled = false,
              .stopped_error = std::nullopt
          })
      ) {}

HostCallContext::~HostCallContext() noexcept {
    HostContextAccess::cancel(*this);
}

auto HostCallContext::limits() const noexcept -> HostLimits {
    return state->environment.limits;
}

auto HostContextAccess::make(const HostEnvironment& environment) noexcept -> HostCallContext {
    return HostCallContext(environment);
}

auto HostContextAccess::attach(
    HostCallContext& context,
    std::size_t index,
    const std::shared_ptr<HostRequestState>& mailbox,
    void* arguments,
    std::expected<void, HostError> (*launch)(void*, void*) noexcept
) noexcept -> std::expected<void, HostError> {
    const auto& state = context.state;
    if (state->stopped_error) {
        return std::unexpected(*state->stopped_error);
    }
    if (state->cancelled) {
        return std::unexpected(hosting_error(HostErrorKind::Execution, "hosted call is cancelled"));
    }
    const auto& provider = state->environment.providers[index];
    if (provider.contract.execution != HostCallExecution::Async) {
        return std::unexpected(hosting_error(
            HostErrorKind::Signature,
            "host binding is synchronous: " + provider.contract.name
        ));
    }
    std::erase_if(state->requests, [](const auto& request) static noexcept {
        const auto active = request.lock();
        return !active || !active->attached();
    });
    mailbox->context = provider.context;
    state->requests.emplace_back(mailbox);
    auto started = launch(provider.context, arguments);
    if (!started) {
        mailbox->cancel();
        return std::unexpected(std::move(started.error()));
    }
    const auto lock = std::lock_guard(mailbox->mutex);
    if (mailbox->phase == HostRequestState::Phase::Pending
        || mailbox->phase == HostRequestState::Phase::Ready) {
        mailbox->cancel_hook = provider.cancel;
    }
    return {};
}

auto HostContextAccess::cancel(HostCallContext& context) noexcept -> void {
    const auto& state = context.state;
    if (state->cancelled) {
        return;
    }
    state->cancelled = true;
    for (const auto& request : state->requests) {
        if (const auto mailbox = request.lock()) {
            mailbox->cancel();
        }
    }
    state->requests.clear();
}

auto HostContextAccess::native_driver(HostCallContext& context) noexcept
    -> carven::runtime::execution::Driver& {
    return *context.state->environment.native_driver;
}

auto HostContextAccess::native_scope(HostCallContext& context) noexcept
    -> carven::runtime::hosting::detail::Scope {
    const auto& state = context.state;
    return carven::runtime::hosting::detail::Scope(
        state->environment.native,
        &context,
        [](void* context) static noexcept {
            const auto& call = *static_cast<HostCallContext*>(context)->state;
            return !call.cancelled && !call.stopped_error;
        },
        [](void* context, std::string_view message) static noexcept {
            HostContextAccess::stop(
                *static_cast<HostCallContext*>(context),
                hosting_error(HostErrorKind::Execution, std::string(message))
            );
        },
        context.limits().byte_work,
        maximum_constant_bytes,
        std::min(context.limits().aggregate_work, maximum_constant_aggregate_elements),
        maximum_constant_aggregate_depth,
        hosting_valid_text
    );
}

auto HostContextAccess::stopped_error(const HostCallContext& context) noexcept -> const HostError* {
    const auto& state = context.state;
    return state->stopped_error ? &*state->stopped_error : nullptr;
}

auto HostContextAccess::stop(HostCallContext& context, HostError error) noexcept -> void {
    const auto& state = context.state;
    if (!state->stopped_error) {
        state->stopped_error.emplace(std::move(error));
    }
}

} // extern "C++"
