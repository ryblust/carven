module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/request.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.context.impl;

import :hosting.session;
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
    cancel();
}

auto HostCallContext::limits() const noexcept -> HostLimits {
    return state->environment.limits;
}

auto HostCallContext::attach(
    std::size_t index,
    const std::shared_ptr<HostRequestState>& mailbox,
    void* arguments,
    std::expected<void, HostError> (*launch)(void*, void*) noexcept
) noexcept -> std::expected<void, HostError> {
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

auto HostCallContext::cancel() noexcept -> void {
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

auto HostCallContext::native_scope() noexcept -> carven::runtime::hosting::detail::Scope {
    return carven::runtime::hosting::detail::Scope(
        state->environment.native,
        this,
        [](void* context) static noexcept {
            const auto& call = *static_cast<HostCallContext*>(context)->state;
            return !call.cancelled && !call.stopped_error;
        },
        [](void* context, std::string_view message) static noexcept {
            static_cast<HostCallContext*>(context)->stop(
                hosting_error(HostErrorKind::Execution, std::string(message))
            );
        },
        limits().byte_work,
        maximum_constant_bytes,
        std::min(limits().aggregate_work, maximum_constant_aggregate_elements),
        maximum_constant_aggregate_depth,
        hosting_valid_text
    );
}

auto HostCallContext::stopped_error() const noexcept -> const HostError* {
    return state->stopped_error ? &*state->stopped_error : nullptr;
}

auto HostCallContext::stop(HostError error) noexcept -> void {
    if (!state->stopped_error) {
        state->stopped_error.emplace(std::move(error));
    }
}

} // extern "C++"
