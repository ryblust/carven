module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/runtime.hpp>
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
    if (!success.has_value() || std::move(success).take_value() != 42) {
        return false;
    }

    auto failure = ConstexprWide(ConstexprNarrow::failure(ParseFailure {.offset = 7}));
    if (!failure.holds_failure<ParseFailure>() || failure.failure<ParseFailure>().offset != 7) {
        return false;
    }

    auto destination = ConstexprWide::failure(NetworkFailure {.status = 503});
    destination = std::move(failure);
    return destination.holds_failure<ParseFailure>()
        && std::move(destination).take_failure<ParseFailure>().offset == 7;
}

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
static_assert(std::is_nothrow_move_assignable_v<Reconstructing>);
static_assert(outcome_constant_evaluation());

auto move_assign(Reconstructing& destination, Reconstructing&& source) noexcept -> void {
    destination = std::move(source);
}

} // namespace

TEST_CASE("Runtime Outcome: value and void successes stay flat") {
    using ValueOutcome = carven::runtime::Outcome<int, int>;
    auto value = ValueOutcome::success(42);
    CHECK(value.has_value());
    CHECK_EQ(std::move(value).take_value(), 42);

    using VoidOutcome = carven::runtime::Outcome<void, ParseFailure>;
    auto empty = VoidOutcome::success();
    CHECK(empty.has_value());
    std::move(empty).take_value();
}

TEST_CASE("Runtime Outcome: success and failure may have the same source type") {
    using SameTypeOutcome = carven::runtime::Outcome<int, int>;
    auto failed = SameTypeOutcome::failure<int>(7);
    CHECK_FALSE(failed.has_value());
    CHECK(failed.holds_failure<int>());
    CHECK_EQ(failed.failure<int>(), 7);
    CHECK_EQ(std::move(failed).take_failure<int>(), 7);
}

TEST_CASE("Runtime Outcome: widening preserves success and failure states") {
    auto narrow = Narrow::failure<ParseFailure>(ParseFailure {.offset = 5});
    auto widened = Wide(std::move(narrow));
    CHECK(widened.holds_failure<ParseFailure>());
    CHECK_EQ(std::move(widened).take_failure<ParseFailure>().offset, 5);

    auto success = Narrow::success(std::string("ready"));
    auto widened_success = Wide(std::move(success));
    REQUIRE(widened_success.has_value());
    CHECK_EQ(std::move(widened_success).take_value(), "ready");

    const auto exact = Wide::failure<NetworkFailure>(NetworkFailure {.status = 503});
    CHECK(exact.holds_failure<NetworkFailure>());
    CHECK_EQ(exact.failure<NetworkFailure>().status, 503);

    auto void_success = VoidNarrow::success();
    auto widened_void_success = VoidWide(std::move(void_success));
    REQUIRE(widened_void_success.has_value());
    std::move(widened_void_success).take_value();

    auto void_failure = VoidNarrow::failure<ParseFailure>(ParseFailure {.offset = 9});
    auto widened_void_failure = VoidWide(std::move(void_failure));
    CHECK(widened_void_failure.holds_failure<ParseFailure>());
    CHECK_EQ(std::move(widened_void_failure).take_failure<ParseFailure>().offset, 9);
}

TEST_CASE("Runtime Outcome: move assignment reconstructs the active alternative") {
    auto destination = Reconstructing::success(MoveConstructOnlyResult {1});
    auto failed = Reconstructing::failure<MoveConstructOnlyFailure>(MoveConstructOnlyFailure {7});
    destination = std::move(failed);
    REQUIRE(destination.holds_failure<MoveConstructOnlyFailure>());
    CHECK_EQ(destination.failure<MoveConstructOnlyFailure>().code, 7);

    auto succeeded = Reconstructing::success(MoveConstructOnlyResult {42});
    destination = std::move(succeeded);
    REQUIRE(destination.has_value());
    CHECK_EQ(std::move(destination).take_value().value, 42);

    auto self = Reconstructing::failure<MoveConstructOnlyFailure>(MoveConstructOnlyFailure {9});
    move_assign(self, std::move(self));
    REQUIRE(self.holds_failure<MoveConstructOnlyFailure>());
    CHECK_EQ(self.failure<MoveConstructOnlyFailure>().code, 9);
}
