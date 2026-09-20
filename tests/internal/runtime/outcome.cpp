module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/outcome.hpp>
#include <carven/runtime/passing.hpp>
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

    auto success =
        ConstexprWide(ConstexprNarrow::success_from([]() static noexcept { return 42; }));
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
    auto value = ValueOutcome::success_from([]() static noexcept { return 42; });
    auto* value_success = value.success_if();
    REQUIRE(value_success != nullptr);
    CHECK_EQ(value_success->value, 42);

    using VoidOutcome = carven::runtime::Outcome<void, ParseFailure>;
    auto empty = VoidOutcome::success();
    CHECK(empty.success_if() != nullptr);
}

TEST_CASE("Runtime Outcome: success factories construct immovable payloads exactly once") {
    class Fixed final {
    public:
        explicit Fixed(int source) noexcept
            : value(source) {}

        Fixed(const Fixed&) = delete;
        Fixed(Fixed&&) = delete;

        auto get() const noexcept -> int { return value; }

    private:
        int value;
    };

    using Result = carven::runtime::Outcome<Fixed, ParseFailure>;
    using Wider = carven::runtime::Outcome<Fixed, ParseFailure, NetworkFailure>;
    static_assert(!std::is_move_constructible_v<Result>);
    static_assert(carven::runtime::OutcomeWidening<Result, Wider>);
    static_assert(!std::constructible_from<Wider, Result&&>);
    static_assert(!std::constructible_from<Fixed, Fixed&&>);
    auto calls = 0;
    const auto result = Result::success_from([&]() noexcept {
        ++calls;
        return Fixed(42);
    });
    REQUIRE(result.success_if() != nullptr);
    CHECK(result.success_if()->value.get() == 42);
    CHECK(calls == 1);
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

    auto success = Narrow::success_from([]() static noexcept { return std::string("ready"); });
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
    auto success = Value::success_from([&]() noexcept { return Observed(source); });
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
    const auto copied_text = Narrow::success_from([&]() noexcept { return std::string(text); });
    REQUIRE(copied_text.success_if() != nullptr);
    CHECK_EQ(copied_text.success_if()->value, text);
}

TEST_CASE("Runtime Outcome: propagation preserves value void and failure alternatives") {
    using Value = carven::runtime::Outcome<int, ParseFailure, NetworkFailure>;
    auto success = Value::success_from([]() static noexcept { return 42; });
    const auto propagated_success = std::move(success).propagate();
    REQUIRE(propagated_success.success_if() != nullptr);
    CHECK_EQ(propagated_success.success_if()->value, 42);

    auto parse = Value::failure(ParseFailure {.offset = 7});
    const auto propagated_parse = std::move(parse).propagate();
    CHECK(propagated_parse.success_if() == nullptr);
    REQUIRE(propagated_parse.failure_if<ParseFailure>() != nullptr);
    CHECK_EQ(propagated_parse.failure_if<ParseFailure>()->offset, 7);

    auto network = Value::failure(NetworkFailure {.status = 503});
    const auto propagated_network = std::move(network).propagate();
    CHECK(propagated_network.success_if() == nullptr);
    REQUIRE(propagated_network.failure_if<NetworkFailure>() != nullptr);
    CHECK_EQ(propagated_network.failure_if<NetworkFailure>()->status, 503);

    auto empty = VoidNarrow::success();
    const auto propagated_empty = std::move(empty).propagate();
    CHECK(propagated_empty.success_if() != nullptr);
    auto void_failure = VoidNarrow::failure(ParseFailure {.offset = 9});
    const auto propagated_void_failure = std::move(void_failure).propagate();
    REQUIRE(propagated_void_failure.failure_if<ParseFailure>() != nullptr);
    CHECK_EQ(propagated_void_failure.failure_if<ParseFailure>()->offset, 9);

    using SameType = carven::runtime::Outcome<int, int>;
    auto same_success = SameType::success_from([]() static noexcept { return 11; });
    auto same_failure = SameType::failure(13);
    const auto propagated_same_success = std::move(same_success).propagate();
    const auto propagated_same_failure = std::move(same_failure).propagate();
    REQUIRE(propagated_same_success.success_if() != nullptr);
    CHECK_EQ(propagated_same_success.success_if()->value, 11);
    REQUIRE(propagated_same_failure.failure_if<int>() != nullptr);
    CHECK_EQ(*propagated_same_failure.failure_if<int>(), 13);
}

TEST_CASE("Runtime Outcome: propagation and widening use the same payload transfer policy") {
    struct CopyTrivial final {
        int* moves;
        int value;

        CopyTrivial(int& move_count, int source) noexcept
            : moves(&move_count),
              value(source) {}

        CopyTrivial(const CopyTrivial&) = default;

        CopyTrivial(CopyTrivial&& source) noexcept
            : moves(source.moves),
              value(source.value) {
            ++*moves;
        }
    };

    static_assert(std::is_trivially_copy_constructible_v<CopyTrivial>);
    static_assert(std::is_trivially_destructible_v<CopyTrivial>);
    static_assert(!std::is_trivially_move_constructible_v<CopyTrivial>);
    using Value = carven::runtime::Outcome<CopyTrivial, CopyTrivial>;
    auto moves = 0;
    const auto initial = CopyTrivial(moves, 42);
    auto source_success = Value::success_from([&]() noexcept { return initial; });
    auto source_failure = Value::failure(initial);
    auto manual_success = Value::success_from([&]() noexcept { return initial; });
    auto manual_failure = Value::failure(initial);

    const auto expected_success = Value::success_from([&]() noexcept -> CopyTrivial {
        return carven::runtime::transfer(manual_success.success_if()->value);
    });
    const auto expected_failure =
        Value::failure(carven::runtime::transfer(*manual_failure.failure_if<CopyTrivial>()));
    const auto propagated_success = std::move(source_success).propagate();
    const auto propagated_failure = std::move(source_failure).propagate();

    REQUIRE(propagated_success.success_if() != nullptr);
    REQUIRE(propagated_failure.failure_if<CopyTrivial>() != nullptr);
    CHECK_EQ(
        propagated_success.success_if()->value.value,
        expected_success.success_if()->value.value
    );
    CHECK_EQ(
        propagated_failure.failure_if<CopyTrivial>()->value,
        expected_failure.failure_if<CopyTrivial>()->value
    );
    using WideValue = carven::runtime::Outcome<CopyTrivial, ParseFailure, CopyTrivial>;
    auto narrow_success = Value::success_from([&]() noexcept { return initial; });
    auto narrow_failure = Value::failure(initial);
    const auto wide_success = WideValue(std::move(narrow_success));
    const auto wide_failure = WideValue(std::move(narrow_failure));
    REQUIRE(wide_success.success_if() != nullptr);
    CHECK(wide_success.success_if()->value.value == 42);
    REQUIRE(wide_failure.failure_if<CopyTrivial>() != nullptr);
    CHECK(wide_failure.failure_if<CopyTrivial>()->value == 42);
    CHECK_EQ(moves, 0);
}

TEST_CASE("Runtime Outcome: propagation preserves payload construction identity") {
    struct Self final {
        const Self* construction_address;
        char padding[64];

        Self() noexcept
            : construction_address(this),
              padding {} {}
    };

    static_assert(std::is_trivially_copy_constructible_v<Self>);
    static_assert(std::is_trivially_move_constructible_v<Self>);
    static_assert(std::is_trivially_destructible_v<Self>);
    using Value = carven::runtime::Outcome<Self, ParseFailure>;
    auto source = Value::success_from([]() static noexcept -> Self { return {}; });
    const auto* source_address = source.success_if()->value.construction_address;
    const auto actual = std::move(source).propagate();
    REQUIRE(actual.success_if() != nullptr);
    CHECK_EQ(actual.success_if()->value.construction_address, source_address);
    CHECK_NE(actual.success_if()->value.construction_address, &actual.success_if()->value);
}

TEST_CASE("Runtime Outcome: propagation preserves payload transfer and caller cleanup order") {
    struct Observed final {
        std::string* events;
        int value;

        Observed(std::string& log, int source) noexcept
            : events(&log),
              value(source) {
            *events += 'c';
        }

        Observed(const Observed& source) noexcept
            : events(source.events),
              value(source.value) {
            *events += 'p';
        }

        Observed(Observed&& source) noexcept
            : events(source.events),
              value(source.value) {
            *events += 'm';
            source.value = -source.value;
        }

        ~Observed() { *events += value < 0 ? 's' : 'd'; }
    };

    struct Guard final {
        std::string* events;
        char mark;

        ~Guard() { *events += mark; }
    };

    using Value = carven::runtime::Outcome<Observed, Observed>;
    const auto observe = [](bool failure) static noexcept -> std::string {
        auto events = std::string();
        {
            const auto before = Guard {.events = &events, .mark = 'a'};
            auto source = failure
                ? Value::failure(Observed(events, 42))
                : Value::success_from([&]() noexcept { return Observed(events, 42); });
            const auto after = Guard {.events = &events, .mark = 'b'};
            // Ignore initial construction. Observe transfer and cleanup.
            events.clear();
            events += '[';
            {
                const auto destination = std::move(source).propagate();
                const auto* payload = failure             ? destination.failure_if<Observed>()
                    : destination.success_if() != nullptr ? &destination.success_if()->value
                                                          : nullptr;
                events += payload != nullptr && payload->value == 42 ? 'r' : '?';
            }
            events += ']';
        }
        return events;
    };

    for (const auto failure : {false, true}) {
        CAPTURE(failure);
        CHECK_EQ(observe(failure), "[mrd]bsa");
    }
}

TEST_CASE("Runtime Outcome: widening accepts trivially copied payloads with deleted moves") {
    struct CopyOnly final {
        CopyOnly() = default;
        CopyOnly(const CopyOnly&) = default;
        CopyOnly(CopyOnly&&) = delete;
    };

    using Source = carven::runtime::Outcome<CopyOnly, CopyOnly>;
    using Destination = carven::runtime::Outcome<CopyOnly, ParseFailure, CopyOnly>;
    static_assert(std::constructible_from<Destination, Source&&>);
    auto success = Source::success_from([]() static noexcept -> CopyOnly { return {}; });
    const auto value = CopyOnly {};
    auto failure = Source::failure(value);
    const auto wide_success = Destination(std::move(success));
    const auto wide_failure = Destination(std::move(failure));
    CHECK(wide_success.success_if() != nullptr);
    CHECK(wide_failure.failure_if<CopyOnly>() != nullptr);
}
