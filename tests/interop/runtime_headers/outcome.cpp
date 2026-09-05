#include <carven/runtime/outcome.hpp>
#include <utility>
#include <variant>

namespace {

struct HeaderFailure final {};

struct MoveOnlyFailure final {
    int code;

    constexpr explicit MoveOnlyFailure(int value) noexcept
        : code(value) {}

    MoveOnlyFailure(const MoveOnlyFailure&) = delete;
    constexpr MoveOnlyFailure(MoveOnlyFailure&& source) noexcept
        : code(std::exchange(source.code, -1)) {}
    auto operator=(const MoveOnlyFailure&) -> MoveOnlyFailure& = delete;
    auto operator=(MoveOnlyFailure&&) -> MoveOnlyFailure& = delete;
};

struct OtherFailure final {
    int code;
};

using NarrowOutcome = carven::runtime::Outcome<void, MoveOnlyFailure>;
using WideOutcome = carven::runtime::Outcome<void, MoveOnlyFailure, OtherFailure>;
using NarrowCarrier = std::variant<MoveOnlyFailure>;
using WideCarrier = std::variant<MoveOnlyFailure, OtherFailure>;

// This is the runtime factory used when lowering a ThrowTerminator at a callable exit.
constexpr auto throw_failure_factory_and_widening_move_payload() noexcept -> bool {
    auto payload = MoveOnlyFailure {17};
    auto thrown = NarrowOutcome::failure(std::move(payload));
    auto widened = WideOutcome(std::move(thrown));
    const auto* failure = widened.failure_if<MoveOnlyFailure>();
    // NOLINTNEXTLINE(bugprone-use-after-move): verifies the move constructor poisons its source.
    return payload.code == -1 && failure != nullptr && failure->code == 17;
}

constexpr auto widen_carrier(NarrowCarrier source) noexcept -> WideCarrier {
    return std::visit(
        [](auto&& failure) constexpr noexcept -> WideCarrier {
            return WideCarrier(std::forward<decltype(failure)>(failure));
        },
        std::move(source)
    );
}

constexpr auto catch_selected_or_residual_moves_once(bool selected) noexcept -> int {
    auto incoming = NarrowCarrier(std::in_place_type<MoveOnlyFailure>, 29);
    auto* projection = std::get_if<MoveOnlyFailure>(&incoming);
    if (projection != nullptr && selected) {
        auto accepted = NarrowCarrier(std::move(*projection));
        return std::get<MoveOnlyFailure>(accepted).code;
    }
    auto residual = WideCarrier(std::move(*projection));
    return std::get<MoveOnlyFailure>(residual).code;
}

constexpr auto rejected_guard_forwards_accepted_carrier() noexcept -> int {
    auto incoming = NarrowCarrier(std::in_place_type<MoveOnlyFailure>, 41);
    auto* selected = std::get_if<MoveOnlyFailure>(&incoming);
    auto accepted = NarrowCarrier(std::move(*selected));
    const auto guard = false;
    if (guard) {
        return std::get<MoveOnlyFailure>(accepted).code;
    }
    auto residual = widen_carrier(std::move(accepted));
    return std::get<MoveOnlyFailure>(residual).code;
}

static_assert(throw_failure_factory_and_widening_move_payload());
static_assert(
    std::get<MoveOnlyFailure>(widen_carrier(NarrowCarrier(MoveOnlyFailure {23}))).code == 23
);
static_assert(catch_selected_or_residual_moves_once(true) == 29);
static_assert(catch_selected_or_residual_moves_once(false) == 29);
static_assert(rejected_guard_forwards_accepted_carrier() == 41);

} // namespace

auto outcome_header_contract() noexcept -> int {
    auto outcome = carven::runtime::Outcome<int, HeaderFailure>::success(42);
    const auto* const success = outcome.success_if();
    return success == nullptr ? 0 : success->value;
}
