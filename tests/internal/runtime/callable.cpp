module;
#include <carven/runtime/callable.hpp>
#include <concepts>
#include <utility>

module carven:test.internal.runtime.callable;

import :test.harness.framework;

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

struct NonConstEmptyCallable final {
    auto operator()(int value) noexcept -> int { return value; }
};

struct ConstructedCallable final {
    ConstructedCallable() noexcept {}

    auto operator()(int value) const noexcept -> int { return value; }
};

struct DestructedCallable final {
    ~DestructedCallable() noexcept {}

    auto operator()(int value) const noexcept -> int { return value; }
};

struct WrongResultCallable final {
    auto operator()(int value) const noexcept -> long { return value; }
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

static_assert(carven::runtime::Stateless<ConstCallable>);
static_assert(carven::runtime::Stateless<NoncapturingCallable>);
static_assert(carven::runtime::Stateless<NonConstEmptyCallable>);
static_assert(!carven::runtime::Stateless<MutableCallable>);
static_assert(!carven::runtime::Stateless<CapturingCallable>);
static_assert(!carven::runtime::Stateless<ConstructedCallable>);
static_assert(!carven::runtime::Stateless<DestructedCallable>);
static_assert(!carven::runtime::Stateless<const ConstCallable>);
static_assert(!carven::runtime::Stateless<volatile ConstCallable>);
static_assert(!carven::runtime::Stateless<ConstCallable&>);
static_assert(!carven::runtime::Stateless<void>);
static_assert(!carven::runtime::Stateless<IntFunctionPointer>);

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
using NarrowFunctionRef = carven::runtime::FunctionRef<NarrowOutcome(int) noexcept>;
using WideFunctionRef = carven::runtime::FunctionRef<WideOutcome(int) noexcept>;
using VoidOutcome = carven::runtime::Outcome<void, ParseFailure>;
using VoidOutcomeFunctionRef = carven::runtime::FunctionRef<VoidOutcome(int) noexcept>;

auto narrow_result(int value) noexcept -> NarrowOutcome {
    return value >= 0 ? NarrowOutcome::success_from([value]() noexcept { return value; })
                      : NarrowOutcome::failure(ParseFailure {.offset = -value});
}

auto plain_void(int) noexcept -> void {}

using WrongResultFunctionPointer = long (*)(int) noexcept;
using WideOutcomeFunctionPointer = WideOutcome (*)(int) noexcept;

static_assert(std::constructible_from<NarrowFunctionRef, decltype(&narrow_result)>);
static_assert(std::constructible_from<WideFunctionRef, decltype(&narrow_result)>);
static_assert(std::constructible_from<WideFunctionRef, decltype(&increment)>);
static_assert(std::constructible_from<VoidOutcomeFunctionRef, decltype(&plain_void)>);
static_assert(!std::constructible_from<WideFunctionRef, WrongResultFunctionPointer>);
static_assert(!std::constructible_from<NarrowFunctionRef, WideOutcomeFunctionPointer>);
static_assert(
    !std::constructible_from<carven::runtime::FunctionRef<void(int) noexcept>, IntFunctionPointer>
);

static_assert(!std::default_initializable<IntFunctionRef>);
static_assert(!std::constructible_from<IntFunctionRef, std::nullptr_t>);
static_assert(std::constructible_from<IntFunctionRef, IntFunctionPointer>);
static_assert(std::constructible_from<IntFunctionRef, decltype(increment)&>);
static_assert(std::constructible_from<IntFunctionRef, NoncapturingCallable>);
static_assert(std::constructible_from<IntFunctionRef, MutableCallable&>);
static_assert(std::constructible_from<IntFunctionRef, const ConstCallable&>);
static_assert(std::constructible_from<IntFunctionRef, NothrowFunctionPointerConversion>);
static_assert(!std::constructible_from<IntFunctionRef, CapturingCallable>);
static_assert(std::constructible_from<IntFunctionRef, ThrowingCallable&>);
static_assert(std::constructible_from<IntFunctionRef, ThrowingFunctionPointerConversion>);
static_assert(!std::constructible_from<IntFunctionRef, volatile MutableCallable&>);
static_assert(!std::constructible_from<MemberFunctionRef, MemberFunctionPointer&>);
static_assert(!std::convertible_to<MutableCallable&, IntFunctionRef>);
static_assert(std::copyable<IntFunctionRef>);
static_assert(noexcept(std::declval<const IntFunctionRef&>()(0)));

template<typename Callable>
concept StatelessIntTarget =
    requires (const Callable& callable) { IntFunctionRef::from_stateless(callable); };

static_assert(StatelessIntTarget<ConstCallable>);
static_assert(StatelessIntTarget<NoncapturingCallable>);
static_assert(StatelessIntTarget<ThrowingCallable>);
static_assert(!StatelessIntTarget<NonConstEmptyCallable>);
static_assert(!StatelessIntTarget<ConstructedCallable>);
static_assert(!StatelessIntTarget<DestructedCallable>);
static_assert(!StatelessIntTarget<WrongResultCallable>);
static_assert(!StatelessIntTarget<MutableCallable>);
static_assert(!StatelessIntTarget<CapturingCallable>);


const TestSuite suite([] static noexcept {
    "Runtime: FunctionRef invokes functions and noncapturing callables"_test = [] static noexcept {
        const auto function = IntFunctionRef(&increment);
        expect_equal(function(4), 5);

        const auto lambda = [](int value) static noexcept -> int {
            return value * 2;
        };
        const auto reference = IntFunctionRef(lambda);
        expect_equal(reference(6), 12);

        const auto temporary =
            IntFunctionRef([](int value) static noexcept -> int { return value - 1; });
        expect_equal(temporary(6), 5);
    };

    "Runtime: FunctionRef preserves capturing mutable callable identity when copied"_test =
        [] static noexcept {
            auto offset = 3;
            auto capture = [&offset](int value) noexcept -> int {
                ++offset;
                return value + offset;
            };
            const auto first = IntFunctionRef(capture);
            const auto second = first;
            expect_equal(first(1), 5);
            expect_equal(second(1), 6);
            expect_equal(offset, 5);

            auto mutable_callable = MutableCallable {.calls = 0};
            const auto mutable_reference = IntFunctionRef(mutable_callable);
            expect_equal(mutable_reference(10), 11);
            expect_equal(mutable_reference(10), 12);
        };

    "Runtime: FunctionRef preserves constness and exact void results"_test = [] static noexcept {
        const auto callable = ConstCallable {};
        const auto const_reference = IntFunctionRef(callable);
        expect_equal(const_reference(4), 12);

        auto calls = 0;
        auto void_callable = [&calls](int) noexcept -> void {
            ++calls;
        };
        const auto void_reference = carven::runtime::FunctionRef<void(int) noexcept>(void_callable);
        void_reference(3);
        expect_equal(calls, 1);
    };

    "Runtime: FunctionRef copies views without copying their target"_test = [] static noexcept {
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
        expect_equal(second(1), 2);
        expect_equal(copies, 0);
        expect_equal(moves, 0);
    };

    "Runtime: FunctionRef preserves reference parameter categories"_test = [] static noexcept {
        auto callable = [](int& left, int&& right) static noexcept -> int {
            left += right;
            right = 0;
            return left;
        };
        const auto reference = carven::runtime::FunctionRef<int(int&, int&&) noexcept>(callable);

        auto left = 2;
        auto right = 5;
        expect_equal(reference(left, std::move(right)), 7);
        expect_equal(left, 7);
        // NOLINTNEXTLINE(bugprone-use-after-move): the callable mutates, but does not consume, the referred object.
        expect_equal(right, 0);
    };

    "Runtime: FunctionRef widens compatible function, object, and temporary lambda results"_test =
        [] static noexcept {
            const auto function = WideFunctionRef(&narrow_result);
            auto success = function(7);
            const auto* success_value = success.success_if();
            if (!expect(success_value != nullptr)) {
                return;
            }
            expect_equal(success_value->value, 7);

            auto object = [](int value) static noexcept -> NarrowOutcome {
                return NarrowOutcome::failure(ParseFailure {.offset = value});
            };
            const auto object_reference = WideFunctionRef(object);
            auto object_failure = object_reference(11);
            const auto* object_failure_value = object_failure.failure_if<ParseFailure>();
            if (!expect(object_failure_value != nullptr)) {
                return;
            }
            expect_equal(object_failure_value->offset, 11);

            const auto temporary = WideFunctionRef([](int value) static noexcept -> NarrowOutcome {
                return NarrowOutcome::success_from([value]() noexcept { return value * 2; });
            });
            auto temporary_success = temporary(6);
            const auto* temporary_value = temporary_success.success_if();
            if (!expect(temporary_value != nullptr)) {
                return;
            }
            expect_equal(temporary_value->value, 12);
        };

    "Runtime: FunctionRef wraps plain success results for failing destinations"_test =
        [] static noexcept {
            const auto value_function = WideFunctionRef(&increment);
            auto value_success = value_function(4);
            const auto* function_value = value_success.success_if();
            if (!expect(function_value != nullptr)) {
                return;
            }
            expect_equal(function_value->value, 5);

            auto value_object = [](int value) static noexcept -> int {
                return value * 3;
            };
            const auto value_reference = WideFunctionRef(value_object);
            auto object_success = value_reference(3);
            const auto* object_value = object_success.success_if();
            if (!expect(object_value != nullptr)) {
                return;
            }
            expect_equal(object_value->value, 9);

            const auto void_function = VoidOutcomeFunctionRef(&plain_void);
            auto void_success = void_function(0);
            expect(void_success.success_if() != nullptr);
        };

    "Runtime FunctionRef: noexcept boundary admits potentially throwing targets"_test =
        [] static noexcept {
            const auto callable = ThrowingCallable();
            const auto view = IntFunctionRef(callable);
            expect_equal(view(7), 7);
            const auto converted = IntFunctionRef(ThrowingFunctionPointerConversion());
            expect_equal(converted(7), 8);
            const auto function = +[](int value) static -> int {
                return value + 2;
            };
            const auto pointer = IntFunctionRef(function);
            expect_equal(pointer(7), 9);
            const auto temporary =
                IntFunctionRef([](int value) static -> int { return value + 3; });
            expect_equal(temporary(7), 10);
        };

    "Callable views: stateless targets adapt results without borrowing source storage"_test =
        [] static noexcept {
            const auto view = IntFunctionRef::from_stateless(ConstCallable {});
            expect_equal(view(4), 12);
            const auto lambda_view =
                IntFunctionRef::from_stateless([](int value) static noexcept -> int {
                    return value + 1;
                });
            expect_equal(lambda_view(4), 5);
            const auto failing_view = WideFunctionRef::from_stateless(ConstCallable {});
            const auto result = failing_view(3);
            if (!expect(result.success_if() != nullptr)) {
                return;
            }
            expect_equal(result.success_if()->value, 9);
            const auto widening_view =
                WideFunctionRef::from_stateless([](int value) static noexcept -> NarrowOutcome {
                    return NarrowOutcome::failure(ParseFailure {.offset = value});
                });
            const auto failure = widening_view(7);
            const auto* payload = failure.failure_if<ParseFailure>();
            if (!expect(payload != nullptr)) {
                return;
            }
            expect_equal(payload->offset, 7);
            const auto void_view =
                VoidOutcomeFunctionRef::from_stateless([](int) static noexcept -> void {});
            expect(void_view(0).success_if() != nullptr);
        };

    "Runtime FunctionRef: value delivery uses the ordinary transfer policy"_test =
        [] static noexcept {
            struct CopyTrivial final {
                int* moves;

                explicit CopyTrivial(int& count) noexcept
                    : moves(&count) {}

                CopyTrivial(const CopyTrivial&) = default;

                CopyTrivial(CopyTrivial&& value) noexcept
                    : moves(value.moves) {
                    ++*moves;
                }
            };

            auto moves = 0;
            auto value = CopyTrivial(moves);
            const auto callable = [](CopyTrivial) static noexcept -> int {
                return 42;
            };
            using View = carven::runtime::FunctionRef<int(CopyTrivial) noexcept>;
            const auto function = View(+callable);
            const auto object = View(callable);
            const auto stateless = View::from_stateless(callable);
            expect(function(carven::runtime::transfer(value)) == 42);
            expect(object(carven::runtime::transfer(value)) == 42);
            expect(stateless(carven::runtime::transfer(value)) == 42);
            expect(moves == 0);
        };

    "Runtime FunctionRef: admission checks delivered arguments and result construction"_test =
        [] static noexcept {
            struct CopyOnly final {
                CopyOnly() = default;
                CopyOnly(const CopyOnly&) = default;
                CopyOnly(CopyOnly&&) = delete;
            };

            const auto callable = [](CopyOnly) static noexcept -> int {
                return 42;
            };
            using View = carven::runtime::FunctionRef<int(CopyOnly) noexcept>;
            static_assert(std::constructible_from<View, decltype(+callable)>);
            static_assert(std::constructible_from<View, decltype(callable)&>);
            auto value = CopyOnly {};
            expect(View(+callable)(carven::runtime::transfer(value)) == 42);
            expect(View(callable)(carven::runtime::transfer(value)) == 42);
            expect(View::from_stateless(callable)(carven::runtime::transfer(value)) == 42);
            const auto rvalue_only = [](CopyOnly&&) static noexcept -> int {
                return 0;
            };
            static_assert(!std::constructible_from<View, decltype(rvalue_only)&>);

            struct Fixed final {
                Fixed() = default;
                Fixed(const Fixed&) = delete;
                Fixed(Fixed&&) = delete;
            };

            using Narrow = carven::runtime::Outcome<Fixed, ParseFailure>;
            using Wide = carven::runtime::Outcome<Fixed, ParseFailure, NetworkFailure>;
            using WideView = carven::runtime::FunctionRef<Wide() noexcept>;
            const auto plain = []() static noexcept -> Fixed {
                return {};
            };
            const auto narrow = []() static noexcept -> Narrow {
                return Narrow::success_from([]() static noexcept -> Fixed { return {}; });
            };
            static_assert(!std::constructible_from<Wide, Narrow&&>);
            static_assert(!std::constructible_from<WideView, decltype(+narrow)>);
            static_assert(!std::constructible_from<WideView, decltype(narrow)&>);
            const auto direct = carven::runtime::FunctionRef<Narrow() noexcept>(+narrow)();
            expect(direct.success_if() != nullptr);
            const auto wrapped = WideView(+plain)();
            expect(wrapped.success_if() != nullptr);
        };
});

} // namespace
