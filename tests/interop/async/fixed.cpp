#include "fixed.hpp"

#include <cstdio>
#include <utility>

namespace {

struct Counters final {
    std::int32_t created;
    std::int32_t destroyed;
    std::int32_t live;
    std::int32_t trace;
    const Fixed* active;
};

Counters counters {
    .created = 0,
    .destroyed = 0,
    .live = 0,
    .trace = 0,
    .active = nullptr,
};

namespace execution = carven::runtime::execution;

struct CompletionObservation final {
    bool published;
    bool destroyed;
    bool requested;
};

CompletionObservation completion_observation {
    .published = false,
    .destroyed = false,
    .requested = false,
};

class CancelOnDestroy final {
    CompletionObservation* observed;
    execution::TaskContext* parent = nullptr;

public:
    explicit CancelOnDestroy(CompletionObservation& observation) noexcept
        : observed(&observation) {}

    CancelOnDestroy(const CancelOnDestroy&) = delete;

    CancelOnDestroy(CancelOnDestroy&& source) noexcept
        : observed(std::exchange(source.observed, nullptr)),
          parent(std::exchange(source.parent, nullptr)) {}

    auto operator=(const CancelOnDestroy&) -> CancelOnDestroy& = delete;
    auto operator=(CancelOnDestroy&&) -> CancelOnDestroy& = delete;

    ~CancelOnDestroy() noexcept {
        if (parent == nullptr) {
            return;
        }
        observed->destroyed = true;
        parent->request_cancel();
        observed->requested = parent->cancel_requested();
    }

    auto publish(execution::TaskContext& task) noexcept -> void {
        parent = task.parent;
        observed->published = true;
    }
};

// Coroutine parameters remain alive through final suspension. Observation must
// destroy this parameter before timeout commits its outward success.
auto completion_winner(CancelOnDestroy retained) noexcept
    -> execution::AsyncOperation<std::int32_t> {
    retained.publish(*(co_await execution::current_activation()).task);
    co_return execution::Completion<std::int32_t>::success(7);
}

} // namespace

Fixed::Fixed() noexcept
    : construction_address(this) {
    ++counters.created;
    ++counters.live;
    counters.trace = counters.trace * 10 + 1;
    counters.active = this;
}

Fixed::~Fixed() noexcept {
    ++counters.destroyed;
    --counters.live;
    counters.trace = counters.trace * 10 + 2;
    if (counters.active == this) {
        counters.active = nullptr;
    }
}

auto Fixed::at_construction_address() const noexcept -> bool {
    return construction_address == this;
}

auto make_fixed() noexcept -> Fixed {
    return Fixed {};
}

auto fixed_created() noexcept -> std::int32_t {
    return counters.created;
}

auto fixed_destroyed() noexcept -> std::int32_t {
    return counters.destroyed;
}

auto fixed_live() noexcept -> std::int32_t {
    return counters.live;
}

auto fixed_trace() noexcept -> std::int32_t {
    return counters.trace;
}

auto fixed_identity_stable() noexcept -> std::int32_t {
    return counters.active != nullptr && counters.active->at_construction_address() ? 1 : 0;
}

auto fixed_report() noexcept -> void {
    std::printf(
        "%d %d %d %d %d\n",
        static_cast<int>(fixed_created()),
        static_cast<int>(fixed_destroyed()),
        static_cast<int>(fixed_live()),
        static_cast<int>(fixed_trace()),
        static_cast<int>(fixed_identity_stable())
    );
}

auto fixed_cancel_on_destroy() noexcept -> execution::AsyncOperation<std::int32_t> {
    completion_observation = {.published = false, .destroyed = false, .requested = false};
    return completion_winner(CancelOnDestroy(completion_observation));
}

auto fixed_completion_cancel_observed() noexcept -> bool {
    return completion_observation.published
        && completion_observation.destroyed
        && completion_observation.requested;
}
