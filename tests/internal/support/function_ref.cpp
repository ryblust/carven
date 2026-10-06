module carven:test.internal.support.function_ref;

import :support.function_ref;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

using IntCallback = FunctionRef<int(int) noexcept>;

auto increment(int value) noexcept -> int {
    return value + 1;
}

auto decrement(int value) noexcept -> int {
    return value - 1;
}

auto function_view() noexcept -> IntCallback {
    const auto pointer = &increment;
    return IntCallback(pointer);
}

using CapturingCallback =
    decltype([state = 0](int value) mutable noexcept { return state += value; });
using ConstCallback = decltype([state = 1](int value) noexcept { return state + value; });

struct CopyOnlyArgument final {
    int value;
    explicit CopyOnlyArgument(int value) noexcept;
    CopyOnlyArgument(const CopyOnlyArgument&) = default;
    CopyOnlyArgument(CopyOnlyArgument&&) = delete;
};

CopyOnlyArgument::CopyOnlyArgument(int value) noexcept
    : value(value) {}

auto read_copy_only(const CopyOnlyArgument& value) noexcept -> int {
    return value.value;
}

struct MemberTarget final {
    int value;
    auto add(int amount) noexcept -> int;
};

auto MemberTarget::add(int amount) noexcept -> int {
    return value += amount;
}

using MemberCallback = FunctionRef<int(MemberTarget&, int) noexcept>;
using DataCallback = FunctionRef<int&(MemberTarget&) noexcept>;
using ValueProducer = decltype([] static noexcept { return 1; });
using ReferenceProducer = decltype([](int& value) static noexcept -> int& { return value; });

// Borrowed targets must be invocable lvalues with their original constness.
static_assert(!std::constructible_from<IntCallback, const CapturingCallback&>);
static_assert(!std::constructible_from<IntCallback, CapturingCallback>);
static_assert(!std::constructible_from<IntCallback, const ConstCallback>);
static_assert(!std::constructible_from<IntCallback, decltype([] static noexcept {})>);
static_assert(!std::constructible_from<MemberCallback, decltype(&MemberTarget::add)>);
static_assert(!std::constructible_from<DataCallback, decltype(&MemberTarget::value)>);

// Result adaptation must not create a temporary behind a returned reference.
static_assert(!std::constructible_from<FunctionRef<const int&() noexcept>, ValueProducer&>);
static_assert(
    !std::constructible_from<FunctionRef<const double&(int&) noexcept>, ReferenceProducer>
);

// Invocation has a non-escaping exception boundary.
static_assert(noexcept(std::declval<const IntCallback&>()(0)));

const TestSuite suite([] static noexcept {
    "FunctionRef: function targets do not borrow source objects"_test = [] static noexcept {
        auto pointer = &increment;
        const auto view = IntCallback(pointer);
        pointer = &decrement;
        expect_equal(view(4), 5);
        expect_equal(function_view()(4), 5);
        const auto temporary = IntCallback([](int value) static noexcept { return value * 2; });
        expect_equal(temporary(4), 8);
    };

    "FunctionRef: copied views share mutable and noncopyable targets"_test = [] static noexcept {
        auto target = [state = std::make_unique<int>(0)](int value) mutable noexcept {
            return *state += value;
        };
        const auto first = IntCallback(target);
        const auto second = first;
        expect_equal(first(2), 2);
        expect_equal(second(3), 5);
        expect_equal(IntCallback(target)(4), 9);
        const auto constant = [offset = 10](int value) noexcept {
            return offset + value;
        };
        const auto readonly = IntCallback(constant);
        expect_equal(readonly(2), 12);
    };

    "FunctionRef: invocation preserves references and transfers owned arguments"_test =
        [] static noexcept {
            const auto target =
                [](int& left, int&& right, std::unique_ptr<int> owned) static noexcept -> int& {
                left += right + *owned;
                return left;
            };
            const auto view = FunctionRef<int&(int&, int&&, std::unique_ptr<int>) noexcept>(target);
            auto value = 1;
            auto owned = std::make_unique<int>(3);
            auto& result = view(value, 2, std::move(owned));
            expect(std::addressof(result) == std::addressof(value));
            expect_equal(value, 6);
            expect(owned == nullptr);
            const auto readonly = FunctionRef<const int&(int&) noexcept>(ReferenceProducer());
            expect(std::addressof(readonly(value)) == std::addressof(value));

            const auto copied = CopyOnlyArgument(7);
            const auto function = FunctionRef<int(CopyOnlyArgument) noexcept>(&read_copy_only);
            expect_equal(function(copied), 7);
            const auto read = [](const CopyOnlyArgument& value) static noexcept {
                return value.value;
            };
            const auto object = FunctionRef<int(CopyOnlyArgument) noexcept>(read);
            expect_equal(object(copied), 7);
        };

    "FunctionRef: invocation converts results or discards them for void"_test = [] static noexcept {
        const auto widened = FunctionRef<long(int) noexcept>(&increment);
        expect_equal(widened(2), 3l);
        auto calls = 0;
        const auto count = [&]() noexcept {
            return ++calls;
        };
        const auto discarded = FunctionRef<void() noexcept>(count);
        discarded();
        expect_equal(calls, 1);
    };

    "FunctionRef: invocation supports member functions and member references"_test =
        [] static noexcept {
            auto target = MemberTarget {.value = 1};
            const auto method = &MemberTarget::add;
            const auto invoke = MemberCallback(method);
            expect_equal(invoke(target, 2), 3);
            const auto member = &MemberTarget::value;
            const auto read = DataCallback(member);
            auto& value = read(target);
            expect(std::addressof(value) == std::addressof(target.value));
            value = 8;
            expect_equal(target.value, 8);
        };

    "FunctionRef: empty optional recipients reject invocation"_test = [] static noexcept {
        auto empty = IntCallback();
        expect(!empty);
        const auto null_pointer = static_cast<int (*)(int) noexcept>(nullptr);
        expect(!IntCallback(null_pointer));
        empty = &increment;
        expect(static_cast<bool>(empty));
        expect_equal(empty(1), 2);
        empty = {};
        expect(!empty);
        expect(expect_termination("empty callback", [&]() noexcept {
            static_cast<void>(empty(0));
        }));
    };
});

} // namespace
