module;

#include <carven/hosting/api.hpp>

module carven:hosting.event.impl;

import :hosting.event;
import :hosting.session;
import std;

extern "C++" {

namespace {

auto cancel_queued(HostTaskState& completion) noexcept -> void {
    completion.result.emplace(HostOutcome(HostCancelled {}));
    completion.status = HostTaskStatus::Finished;
}

} // namespace

HostEventQueue::HostEventQueue() noexcept
    : mutex(),
      accepting(true),
      pending() {}

auto HostEventQueue::take() noexcept -> std::optional<HostQueuedEvent> {
    const auto lock = std::lock_guard(mutex);
    if (pending.empty()) {
        return std::nullopt;
    }
    auto next = std::move(pending.front());
    pending.pop_front();
    return next;
}

auto HostEventQueue::size() noexcept -> std::size_t {
    const auto lock = std::lock_guard(mutex);
    return pending.size();
}

auto HostEventQueue::close() noexcept -> void {
    const auto lock = std::lock_guard(mutex);
    accepting = false;
    for (const auto& event : pending) {
        cancel_queued(*event.completion);
    }
    pending.clear();
}

auto HostEventState::cancel() noexcept -> void {
    const auto mailbox = queue.lock();
    if (!mailbox) {
        return;
    }
    const auto lock = std::lock_guard(mailbox->mutex);
    active = false;
    std::erase_if(mailbox->pending, [this](const auto& event) noexcept {
        if (event.subscription.lock().get() != this) {
            return false;
        }
        cancel_queued(*event.completion);
        return true;
    });
}

HostEvent::HostEvent(std::weak_ptr<HostEventState> state) noexcept
    : state(std::move(state)) {}

auto HostEvent::post(std::span<const HostValue> arguments) const noexcept
    -> std::optional<HostTask> {
    const auto subscription = state.lock();
    if (!subscription) {
        return std::nullopt;
    }
    const auto mailbox = subscription->queue.lock();
    if (!mailbox) {
        return std::nullopt;
    }
    const auto lock = std::lock_guard(mailbox->mutex);
    if (!mailbox->accepting || !subscription->active || mailbox->pending.size() >= 256uz) {
        return std::nullopt;
    }
    auto completion = std::make_shared<HostTaskState>(HostTaskState {
        .invocation = nullptr,
        .status = HostTaskStatus::Runnable,
        .result = std::nullopt,
        .cancellation_requested = false
    });
    mailbox->pending.push_back(
        {.subscription = subscription,
         .arguments = {arguments.begin(), arguments.end()},
         .completion = completion}
    );
    return HostTask(std::move(completion));
}

HostSubscription::HostSubscription(std::shared_ptr<HostEventState> state) noexcept
    : state(std::move(state)) {}

HostSubscription::HostSubscription(HostSubscription&&) noexcept = default;

HostSubscription::~HostSubscription() noexcept {
    cancel();
}

auto HostSubscription::event() const noexcept -> HostEvent {
    return HostEvent(state);
}

auto HostSubscription::cancel() noexcept -> void {
    if (state) {
        state->cancel();
    }
}

} // extern "C++"
