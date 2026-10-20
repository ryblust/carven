#include "fixed.hpp"

#include <cstdio>
#include <utility>
#include <vector>

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

struct alignas(32) Block final {
    std::uint64_t word;
};

auto aligned(const void* value) noexcept -> bool {
    const volatile auto address = reinterpret_cast<std::uintptr_t>(value);
    return address % alignof(Block) == 0;
}

auto retained_block(Block value, std::uint64_t seed) noexcept -> execution::AsyncOperation<bool> {
    auto local = value;
    ++local.word;
    co_await execution::await_yield_once();
    co_return execution::Completion<bool>::success(
        aligned(&value) && aligned(&local) && value.word == seed && local.word == seed + 1
    );
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

auto make_fixed_outcome() noexcept -> carven::runtime::Outcome<Fixed> {
    return carven::runtime::Outcome<Fixed>::success_from([]() noexcept -> Fixed {
        return Fixed {};
    });
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

auto fixed_aligned_frames() noexcept -> execution::AsyncOperation<bool> {
    // Retain a batch before execution so every payload has its own live storage.
    auto pending = std::vector<execution::AsyncOperation<bool>>();
    for (auto seed = std::uint64_t {0}; seed < 128; ++seed) {
        pending.push_back(retained_block(Block {seed}, seed));
    }
    for (auto& operation : pending) {
        const auto result = co_await std::move(operation);
        const auto* success = result.success_if();
        if (success == nullptr || !success->value) {
            co_return execution::Completion<bool>::success(false);
        }
    }
    co_return execution::Completion<bool>::success(true);
}
