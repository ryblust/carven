module carven:test.harness.framework;

import :support.quote;
import std;

using TestFunction = void (*)() noexcept;

class TestSuite final {
public:
    explicit TestSuite(TestFunction declare) noexcept;
};

class TestBody final {
public:
    // The literal assignment protocol needs implicit conversion to capture the caller's location.
    template<typename Function>
        requires std::convertible_to<Function, TestFunction>
    TestBody(
        Function body,
        std::source_location location = std::source_location::current()
    ) noexcept
        : body(body),
          location(location) {}

private:
    friend class TestRegistration;

    TestFunction body;
    std::source_location location;
};

class TestRegistration final {
public:
    // The name is borrowed until assignment copies it into runner-owned storage.
    explicit constexpr TestRegistration(std::string_view name) noexcept;
    auto operator=(TestBody body) const noexcept -> void;

private:
    const std::string_view name;
};

constexpr TestRegistration::TestRegistration(std::string_view name) noexcept
    : name(name) {}

constexpr auto operator""_test(const char* name, std::size_t length) noexcept -> TestRegistration {
    return TestRegistration(std::string_view(name, length));
}

auto run_tests(int argc, const char* const* argv) noexcept -> int;
auto current_test_name() noexcept -> std::string_view;

auto record_test_assertion(bool passed, std::source_location location) noexcept -> bool;
auto test_failure_output() noexcept -> std::ostream&;

class TestScenarioContext final {
public:
    explicit TestScenarioContext(std::string_view name) noexcept;
    ~TestScenarioContext() noexcept;
    TestScenarioContext(const TestScenarioContext&) = delete;
    auto operator=(const TestScenarioContext&) -> TestScenarioContext& = delete;
};

class TestAssertion final {
public:
    TestAssertion(bool condition, bool fatal, std::source_location location) noexcept;

    template<typename Describe>
    TestAssertion(
        bool condition,
        bool fatal,
        std::source_location location,
        Describe describe
    ) noexcept
        : TestAssertion(condition, fatal, location) {
        if (!passed) {
            describe(test_failure_output());
        }
    }

    ~TestAssertion() noexcept;
    TestAssertion(const TestAssertion&) = delete;
    auto operator=(const TestAssertion&) -> TestAssertion& = delete;

    explicit operator bool() const noexcept;

    template<typename... Messages>
    auto note(const Messages&... messages) noexcept -> TestAssertion& {
        if (!passed) {
            test_failure_output() << "\n  note:";
            (write_note(messages), ...);
        }
        return *this;
    }

private:
    template<typename Message>
    static auto write_note(const Message& message) noexcept -> void {
        test_failure_output() << ' ';
        if constexpr (std::invocable<const Message&>) {
            static_assert(std::is_nothrow_invocable_v<const Message&>);
            test_failure_output() << std::invoke(message);
        } else {
            test_failure_output() << message;
        }
    }

    bool passed;
    bool fatal;
};

template<typename Condition>
    requires requires (const Condition& condition) { static_cast<bool>(condition); }
auto expect(
    const Condition& condition,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return TestAssertion(static_cast<bool>(condition), false, location);
}

template<typename Condition>
    requires requires (const Condition& condition) { static_cast<bool>(condition); }
auto require(
    const Condition& condition,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return TestAssertion(static_cast<bool>(condition), true, location);
}

template<typename Value>
inline constexpr bool is_test_text = std::convertible_to<const Value&, std::string_view>;

template<typename Value>
    requires is_test_text<Value>
auto test_text_view(const Value& value) noexcept -> std::optional<std::string_view> {
    if constexpr (std::is_pointer_v<Value>) {
        if (value == nullptr) {
            return std::nullopt;
        }
    }
    return std::string_view(value);
}

template<typename Value>
auto write_test_value(std::ostream& output, const Value& value) noexcept -> void {
    if constexpr (is_test_text<Value>) {
        const auto text = test_text_view(value);
        output << (text ? quote_text(*text) : "<null>");
    } else if constexpr (std::integral<Value>
                         && !std::same_as<Value, bool>
                         && !std::same_as<Value, char>) {
        output << +value;
    } else if constexpr (std::is_enum_v<Value> && std::formattable<Value, char>) {
        output << std::format("{}", value);
    } else if constexpr (std::is_enum_v<Value>) {
        output << +std::to_underlying(value);
    } else if constexpr (requires { output << value; }) {
        output << value;
    } else if constexpr (std::formattable<Value, char>) {
        output << std::format("{}", value);
    } else {
        static_assert(sizeof(Value) == 0, "assertion value has no printable representation");
    }
}

auto write_test_text_difference(
    std::ostream& output,
    std::string_view actual,
    std::string_view expected
) noexcept -> void;

template<typename Left, typename Right, typename Compare>
auto compare_test_values(
    const Left& actual,
    const Right& expected,
    Compare operation,
    std::string_view relation,
    bool fatal,
    std::source_location location
) noexcept -> TestAssertion {
    const auto passed = [&] noexcept {
        if constexpr (is_test_text<Left> && is_test_text<Right>) {
            return operation(test_text_view(actual), test_text_view(expected));
        } else {
            return static_cast<bool>(operation(actual, expected));
        }
    }();
    return TestAssertion(passed, fatal, location, [&](std::ostream& output) noexcept {
        if constexpr (is_test_text<Left> && is_test_text<Right>) {
            const auto actual_text = test_text_view(actual);
            const auto expected_text = test_text_view(expected);
            if (relation == "==" && actual_text && expected_text) {
                write_test_text_difference(output, *actual_text, *expected_text);
                return;
            }
        }
        output << "\n  actual:   ";
        write_test_value(output, actual);
        output << "\n  expected: ";
        write_test_value(output, expected);
        if (relation != "==") {
            output << "\n  relation: actual " << relation << " expected";
        }
    });
}

template<typename Left, typename Right>
auto expect_equal(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::equal_to<> {}, "==", false, location);
}

template<typename Left, typename Right>
auto expect_not_equal(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::not_equal_to<> {}, "!=", false, location);
}

template<typename Left, typename Right>
auto expect_less(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::less<> {}, "<", false, location);
}

template<typename Left, typename Right>
auto expect_less_equal(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::less_equal<> {}, "<=", false, location);
}

template<typename Left, typename Right>
auto expect_greater(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::greater<> {}, ">", false, location);
}

template<typename Left, typename Right>
auto expect_greater_equal(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::greater_equal<> {}, ">=", false, location);
}

template<typename Left, typename Right>
auto require_equal(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    return compare_test_values(actual, expected, std::equal_to<> {}, "==", true, location);
}

template<typename Left, typename Right>
    requires std::ranges::forward_range<const Left>
    && std::ranges::forward_range<const Right>
    && std::ranges::sized_range<const Left>
    && std::ranges::sized_range<const Right>
    && requires (const Left& left, const Right& right) { std::ranges::equal(left, right); }
auto expect_range_equal(
    const Left& actual,
    const Right& expected,
    std::source_location location = std::source_location::current()
) noexcept -> TestAssertion {
    const auto actual_size = std::ranges::size(actual);
    const auto expected_size = std::ranges::size(expected);
    auto actual_it = std::ranges::begin(actual);
    auto expected_it = std::ranges::begin(expected);
    auto index = 0uz;
    while (actual_it != std::ranges::end(actual)
           && expected_it != std::ranges::end(expected)
           && *actual_it == *expected_it) {
        ++actual_it;
        ++expected_it;
        ++index;
    }
    const auto passed =
        actual_it == std::ranges::end(actual) && expected_it == std::ranges::end(expected);
    return TestAssertion(passed, false, location, [&](std::ostream& output) noexcept {
        output << "\n  lengths: actual " << actual_size << ", expected " << expected_size
               << "\n  first difference at index " << index;
        if (actual_it != std::ranges::end(actual)) {
            output << "\n  actual element:   ";
            write_test_value(output, *actual_it);
        }
        if (expected_it != std::ranges::end(expected)) {
            output << "\n  expected element: ";
            write_test_value(output, *expected_it);
        }
    });
}

template<typename Function>
    requires std::is_nothrow_invocable_v<Function&>
auto scenario(std::string_view name, Function body) noexcept -> void {
    const auto context = TestScenarioContext(name);
    static_cast<void>(std::invoke(body));
}

template<typename Range, typename Projection, typename Callback>
    requires std::ranges::forward_range<const Range>
    && std::is_nothrow_invocable_v<Projection&, std::ranges::range_reference_t<const Range>>
    && std::is_nothrow_invocable_v<Callback&, std::ranges::range_reference_t<const Range>>
    && std::convertible_to<
                 std::invoke_result_t<Projection&, std::ranges::range_reference_t<const Range>>,
                 std::string_view>
auto each(
    const Range& cases,
    Projection projection,
    Callback callback,
    std::source_location location = std::source_location::current()
) noexcept -> void {
    if (std::ranges::begin(cases) == std::ranges::end(cases)) {
        expect(false, location).note("empty case table");
        return;
    }
    for (const auto& item : cases) {
        const auto& name = std::invoke(projection, item);
        scenario(std::string_view(name), [&] noexcept { std::invoke(callback, item); });
    }
}
