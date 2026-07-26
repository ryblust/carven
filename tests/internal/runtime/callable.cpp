module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/runtime.hpp>
#include <concepts>
#include <utility>

module carven:test.internal.runtime.callable;

namespace {

auto increment(int value) noexcept -> int {
    return value + 1;
}

using IntFunctionPointer = int (*)(int) noexcept;

using IntFunctionRef = carven::runtime::FunctionRef<int(int) noexcept>;

struct MutableCallable final {
    int calls;

    auto operator()(int value) noexcept -> int {
        ++calls;
        return value + calls;
    }
};

struct ConstCallable final {
    auto operator()(int value) const noexcept -> int { return value * 3; }
};

struct ThrowingCallable final {
    auto operator()(int value) const -> int { return value; }
};

struct ThrowingFunctionPointerConversion final {
    operator IntFunctionPointer() const noexcept(false) { return &increment; }
};

struct NothrowFunctionPointerConversion final {
    operator IntFunctionPointer() const noexcept { return &increment; }
};

struct MemberCallable final {
    auto invoke(int value) noexcept -> int { return value; }
};

using CapturingCallable =
    decltype([offset = 1](int value) noexcept -> int { return value + offset; });

using NoncapturingCallable = decltype([](int value) static noexcept -> int { return value; });

using MemberFunctionRef = carven::runtime::FunctionRef<int(MemberCallable&, int) noexcept>;

using MemberFunctionPointer = int (MemberCallable::*)(int) noexcept;

struct ParseFailure final {
    int offset;
};

struct NetworkFailure final {
    int status;
};

using NarrowOutcome = carven::runtime::Outcome<int, ParseFailure>;
using WideOutcome = carven::runtime::Outcome<int, ParseFailure, NetworkFailure>;
using WideFunctionRef = carven::runtime::FunctionRef<WideOutcome(int) noexcept>;

auto narrow_result(int value) noexcept -> NarrowOutcome {
    return value >= 0 ? NarrowOutcome::success(value)
                      : NarrowOutcome::failure(ParseFailure {.offset = -value});
}

static_assert(std::constructible_from<WideFunctionRef, decltype(&narrow_result)>);

static_assert(!std::default_initializable<IntFunctionRef>);
static_assert(!std::constructible_from<IntFunctionRef, std::nullptr_t>);
static_assert(std::constructible_from<IntFunctionRef, IntFunctionPointer>);
static_assert(std::constructible_from<IntFunctionRef, decltype(increment)&>);
static_assert(std::constructible_from<IntFunctionRef, NoncapturingCallable>);
static_assert(std::constructible_from<IntFunctionRef, MutableCallable&>);
static_assert(std::constructible_from<IntFunctionRef, const ConstCallable&>);
static_assert(std::constructible_from<IntFunctionRef, NothrowFunctionPointerConversion>);
static_assert(!std::constructible_from<IntFunctionRef, CapturingCallable>);
static_assert(!std::constructible_from<IntFunctionRef, ThrowingCallable&>);
static_assert(!std::constructible_from<IntFunctionRef, ThrowingFunctionPointerConversion>);
static_assert(!std::constructible_from<IntFunctionRef, volatile MutableCallable&>);
static_assert(!std::constructible_from<MemberFunctionRef, MemberFunctionPointer&>);
static_assert(!std::convertible_to<MutableCallable&, IntFunctionRef>);
static_assert(std::copyable<IntFunctionRef>);
static_assert(noexcept(std::declval<const IntFunctionRef&>()(0)));

} // namespace

TEST_CASE("Runtime: FunctionRef invokes functions and noncapturing callables") {
    const auto function = IntFunctionRef(&increment);
    CHECK_EQ(function(4), 5);

    const auto lambda = [](int value) static noexcept -> int {
        return value * 2;
    };
    const auto reference = IntFunctionRef(lambda);
    CHECK_EQ(reference(6), 12);

    const auto temporary =
        IntFunctionRef([](int value) static noexcept -> int { return value - 1; });
    CHECK_EQ(temporary(6), 5);
}

TEST_CASE("Runtime: FunctionRef preserves capturing mutable callable identity when copied") {
    auto offset = 3;
    auto capture = [&offset](int value) noexcept -> int {
        ++offset;
        return value + offset;
    };
    const auto first = IntFunctionRef(capture);
    const auto second = first;
    CHECK_EQ(first(1), 5);
    CHECK_EQ(second(1), 6);
    CHECK_EQ(offset, 5);

    auto mutable_callable = MutableCallable {.calls = 0};
    const auto mutable_reference = IntFunctionRef(mutable_callable);
    CHECK_EQ(mutable_reference(10), 11);
    CHECK_EQ(mutable_reference(10), 12);
}

TEST_CASE("Runtime: FunctionRef preserves constness and discards object return values") {
    const auto callable = ConstCallable {};
    const auto const_reference = IntFunctionRef(callable);
    CHECK_EQ(const_reference(4), 12);

    auto calls = 0;
    auto returning_callable = [&calls](int value) noexcept -> int {
        ++calls;
        return value * 2;
    };
    const auto void_reference =
        carven::runtime::FunctionRef<void(int) noexcept>(returning_callable);
    void_reference(3);
    CHECK_EQ(calls, 1);
}

TEST_CASE("Runtime: FunctionRef copies views without copying their target") {
    auto copies = 0;
    auto moves = 0;
    struct TrackedCallable final {
        int* copies;
        int* moves;

        TrackedCallable(int& copy_count, int& move_count) noexcept
            : copies(&copy_count),
              moves(&move_count) {}
        TrackedCallable(const TrackedCallable& other) noexcept
            : copies(other.copies),
              moves(other.moves) {
            ++*copies;
        }
        TrackedCallable(TrackedCallable&& other) noexcept
            : copies(other.copies),
              moves(other.moves) {
            ++*moves;
        }

        auto operator()(int value) noexcept -> int { return value + 1; }
    };

    auto target = TrackedCallable(copies, moves);
    const auto first = IntFunctionRef(target);
    auto second = first;
    second = first;
    CHECK_EQ(second(1), 2);
    CHECK_EQ(copies, 0);
    CHECK_EQ(moves, 0);
}

TEST_CASE("Runtime: FunctionRef preserves reference parameter categories") {
    auto callable = [](int& left, int&& right) static noexcept -> int {
        left += right;
        right = 0;
        return left;
    };
    const auto reference = carven::runtime::FunctionRef<int(int&, int&&) noexcept>(callable);

    auto left = 2;
    auto right = 5;
    CHECK_EQ(reference(left, std::move(right)), 7);
    CHECK_EQ(left, 7);
    // NOLINTNEXTLINE(bugprone-use-after-move): the callable mutates, but does not consume, the referred object.
    CHECK_EQ(right, 0);
}

TEST_CASE("Runtime: FunctionRef widens compatible function, object, and temporary lambda results") {
    const auto function = WideFunctionRef(&narrow_result);
    auto success = function(7);
    REQUIRE(success.has_value());
    CHECK_EQ(std::move(success).take_value(), 7);

    auto object = [](int value) noexcept -> NarrowOutcome {
        return NarrowOutcome::failure(ParseFailure {.offset = value});
    };
    const auto object_reference = WideFunctionRef(object);
    auto object_failure = object_reference(11);
    REQUIRE(object_failure.holds_failure<ParseFailure>());
    CHECK_EQ(std::move(object_failure).take_failure<ParseFailure>().offset, 11);

    const auto temporary = WideFunctionRef([](int value) static noexcept -> NarrowOutcome {
        return NarrowOutcome::success(value * 2);
    });
    auto temporary_success = temporary(6);
    REQUIRE(temporary_success.has_value());
    CHECK_EQ(std::move(temporary_success).take_value(), 12);
}
