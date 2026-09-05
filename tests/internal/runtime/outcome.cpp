module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/outcome.hpp>
#include <concepts>
#include <string>
#include <type_traits>
#include <utility>

module carven:test.internal.runtime.outcome;

namespace {

struct ParseFailure final {
    int offset;
};

struct NetworkFailure final {
    int status;
};

struct StorageFailure final {
    int device;
};

struct AuthorizationFailure final {
    int subject;
};

struct MoveConstructOnlyResult final {
    int value;

    explicit MoveConstructOnlyResult(int source) noexcept
        : value(source) {}

    MoveConstructOnlyResult(const MoveConstructOnlyResult&) = delete;
    MoveConstructOnlyResult(MoveConstructOnlyResult&&) = default;
    auto operator=(const MoveConstructOnlyResult&) -> MoveConstructOnlyResult& = delete;
    auto operator=(MoveConstructOnlyResult&&) -> MoveConstructOnlyResult& = delete;
};

struct MoveConstructOnlyFailure final {
    int code;

    explicit MoveConstructOnlyFailure(int source) noexcept
        : code(source) {}

    MoveConstructOnlyFailure(const MoveConstructOnlyFailure&) = delete;
    MoveConstructOnlyFailure(MoveConstructOnlyFailure&&) = default;
    auto operator=(const MoveConstructOnlyFailure&) -> MoveConstructOnlyFailure& = delete;
    auto operator=(MoveConstructOnlyFailure&&) -> MoveConstructOnlyFailure& = delete;
};

using Narrow = carven::runtime::Outcome<std::string, ParseFailure>;
using Wide = carven::runtime::Outcome<std::string, ParseFailure, NetworkFailure>;
using VoidNarrow = carven::runtime::Outcome<void, ParseFailure>;
using VoidWide = carven::runtime::Outcome<void, NetworkFailure, ParseFailure>;
using WrongResult = carven::runtime::Outcome<int, ParseFailure, NetworkFailure>;
using Incompatible = carven::runtime::Outcome<std::string, ParseFailure, StorageFailure>;
using NonCoveringSource = carven::runtime::Outcome<std::string, ParseFailure, NetworkFailure>;
using NonCoveringDestination =
    carven::runtime::Outcome<std::string, ParseFailure, StorageFailure, AuthorizationFailure>;
using Reconstructing = carven::runtime::Outcome<MoveConstructOnlyResult, MoveConstructOnlyFailure>;

constexpr auto outcome_constant_evaluation() noexcept -> bool {
    using ConstexprNarrow = carven::runtime::Outcome<int, ParseFailure>;
    using ConstexprWide = carven::runtime::Outcome<int, ParseFailure, NetworkFailure>;

    auto success = ConstexprWide(ConstexprNarrow::success(42));
    const auto* success_value = success.success_if();
    if (success_value == nullptr || success_value->value != 42) {
        return false;
    }

    auto failure = ConstexprWide(ConstexprNarrow::failure(ParseFailure {.offset = 7}));
    const auto* failure_value = failure.failure_if<ParseFailure>();
    if (failure_value == nullptr || failure_value->offset != 7) {
        return false;
    }

    auto destination = ConstexprWide(std::move(failure));
    const auto* destination_failure = destination.failure_if<ParseFailure>();
    return destination_failure != nullptr && destination_failure->offset == 7;
}

static_assert(carven::runtime::OutcomeTraits<Narrow>::is_outcome);
static_assert(std::same_as<carven::runtime::OutcomeTraits<Narrow>::Result, std::string>);
static_assert(std::same_as<carven::runtime::OutcomeTraits<VoidNarrow>::Result, void>);
static_assert(!carven::runtime::OutcomeTraits<int>::is_outcome);
static_assert(!carven::runtime::OutcomeTraits<const Narrow>::is_outcome);
static_assert(!carven::runtime::OutcomeTraits<Narrow&>::is_outcome);
static_assert(carven::runtime::OutcomeWidening<Narrow, Wide>);
static_assert(carven::runtime::OutcomeWidening<VoidNarrow, VoidWide>);
static_assert(!carven::runtime::OutcomeWidening<Narrow, Narrow>);
static_assert(!carven::runtime::OutcomeWidening<Wide, Narrow>);
static_assert(!carven::runtime::OutcomeWidening<Narrow, WrongResult>);
static_assert(!carven::runtime::OutcomeWidening<NonCoveringSource, NonCoveringDestination>);
static_assert(!carven::runtime::OutcomeWidening<int, Wide>);
static_assert(!carven::runtime::OutcomeWidening<Narrow&, Wide>);

static_assert(std::constructible_from<Wide, Narrow&&>);
static_assert(std::convertible_to<Narrow&&, Wide>);
static_assert(noexcept(Wide(std::declval<Narrow&&>())));
static_assert(std::constructible_from<Wide, Wide&&>);
static_assert(!std::constructible_from<Narrow, Wide&&>);
static_assert(!std::constructible_from<Wide, Narrow&>);
static_assert(!std::constructible_from<Wide, const Narrow&&>);
static_assert(!std::constructible_from<WrongResult, Narrow&&>);
static_assert(!std::constructible_from<Incompatible, Wide&&>);
static_assert(!std::constructible_from<NonCoveringDestination, NonCoveringSource&&>);
static_assert(std::constructible_from<VoidWide, VoidNarrow&&>);
static_assert(!std::is_move_assignable_v<MoveConstructOnlyResult>);
static_assert(!std::is_move_assignable_v<MoveConstructOnlyFailure>);
static_assert(std::is_nothrow_move_constructible_v<Reconstructing>);
static_assert(outcome_constant_evaluation());

} // namespace

TEST_CASE("Runtime Outcome: value and void successes stay flat") {
    using ValueOutcome = carven::runtime::Outcome<int, int>;
    auto value = ValueOutcome::success(42);
    auto* value_success = value.success_if();
    REQUIRE(value_success != nullptr);
    CHECK_EQ(value_success->value, 42);

    using VoidOutcome = carven::runtime::Outcome<void, ParseFailure>;
    auto empty = VoidOutcome::success();
    CHECK(empty.success_if() != nullptr);
}

TEST_CASE("Runtime Outcome: success and failure may have the same source type") {
    using SameTypeOutcome = carven::runtime::Outcome<int, int>;
    auto failed = SameTypeOutcome::failure<int>(7);
    CHECK(failed.success_if() == nullptr);
    auto* failure = failed.failure_if<int>();
    REQUIRE(failure != nullptr);
    CHECK_EQ(*failure, 7);
    CHECK_EQ(*failure, 7);
}

TEST_CASE("Runtime Outcome: widening preserves success and failure states") {
    auto narrow = Narrow::failure<ParseFailure>(ParseFailure {.offset = 5});
    auto widened = Wide(std::move(narrow));
    auto* widened_failure = widened.failure_if<ParseFailure>();
    REQUIRE(widened_failure != nullptr);
    CHECK_EQ(std::move(*widened_failure).offset, 5);

    auto success = Narrow::success(std::string("ready"));
    auto widened_success = Wide(std::move(success));
    auto* success_value = widened_success.success_if();
    REQUIRE(success_value != nullptr);
    CHECK_EQ(success_value->value, "ready");

    const auto exact = Wide::failure<NetworkFailure>(NetworkFailure {.status = 503});
    const auto* exact_failure = exact.failure_if<NetworkFailure>();
    REQUIRE(exact_failure != nullptr);
    CHECK_EQ(exact_failure->status, 503);

    auto void_success = VoidNarrow::success();
    auto widened_void_success = VoidWide(std::move(void_success));
    REQUIRE(widened_void_success.success_if() != nullptr);

    auto void_failure = VoidNarrow::failure<ParseFailure>(ParseFailure {.offset = 9});
    auto widened_void_failure = VoidWide(std::move(void_failure));
    auto* void_failure_value = widened_void_failure.failure_if<ParseFailure>();
    REQUIRE(void_failure_value != nullptr);
    CHECK_EQ(std::move(*void_failure_value).offset, 9);
}

TEST_CASE("Runtime Outcome: admission requires only the performed construction") {
    struct Observed final {
        int* copies;
        int* moves;
        int value;
        Observed(int& copy_count, int& move_count, int source) noexcept
            : copies(&copy_count),
              moves(&move_count),
              value(source) {}
        Observed(const Observed& source)
            : copies(source.copies),
              moves(source.moves),
              value(source.value) {
            ++*copies;
        }
        Observed(Observed&& source)
            : copies(source.copies),
              moves(source.moves),
              value(source.value) {
            ++*moves;
        }
        auto operator=(const Observed&) -> Observed& = delete;
        auto operator=(Observed&&) -> Observed& = delete;
    };
    auto copies = 0;
    auto moves = 0;
    const auto source = Observed(copies, moves, 42);
    using Value = carven::runtime::Outcome<Observed, Observed>;
    using Wider = carven::runtime::Outcome<Observed, Observed, ParseFailure>;
    static_assert(!std::is_nothrow_move_constructible_v<Observed>);
    static_assert(std::is_nothrow_move_constructible_v<Value>);
    static_assert(!std::is_move_assignable_v<Value>);
    auto success = Value::success(source);
    CHECK_EQ(copies, 1);
    CHECK_EQ(moves, 0);
    auto moved = Value(std::move(success));
    CHECK_EQ(moves, 1);
    auto widened = Wider(std::move(moved));
    CHECK_EQ(moves, 2);
    REQUIRE(widened.success_if() != nullptr);
    CHECK_EQ(widened.success_if()->value.value, 42);
    const auto failure = Value::failure(source);
    CHECK_EQ(copies, 2);
    REQUIRE(failure.failure_if<Observed>() != nullptr);
    CHECK_EQ(failure.failure_if<Observed>()->value, 42);
    const auto text = std::string("copied text");
    const auto copied_text = Narrow::success(text);
    REQUIRE(copied_text.success_if() != nullptr);
    CHECK_EQ(copied_text.success_if()->value, text);
}
