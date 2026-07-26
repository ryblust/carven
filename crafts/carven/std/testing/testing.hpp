#pragma once

#include <cstdio>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace carven::testing {

namespace detail {

template<typename Result>
class TestControl final {
public:
    static auto success(Result value) noexcept(std::is_nothrow_move_constructible_v<Result>)
        -> TestControl {
        return TestControl(std::move(value));
    }

    static auto exit() noexcept -> TestControl { return TestControl(std::nullopt); }
    auto has_value() const noexcept -> bool { return result.has_value(); }
    auto value() && noexcept(std::is_nothrow_move_constructible_v<Result>) -> Result {
        return std::move(*result);
    }

private:
    explicit TestControl(Result value) noexcept(std::is_nothrow_move_constructible_v<Result>)
        : result(std::move(value)) {}
    explicit TestControl(std::nullopt_t) noexcept
        : result(std::nullopt) {}

    std::optional<Result> result;
};

template<>
class TestControl<void> final {
public:
    static auto success() noexcept -> TestControl { return TestControl(true); }
    static auto exit() noexcept -> TestControl { return TestControl(false); }
    auto has_value() const noexcept -> bool { return normal; }
    auto value() const noexcept -> void {}

private:
    explicit TestControl(bool value) noexcept
        : normal(value) {}
    bool normal;
};

} // namespace detail

struct TestFailure final {
    std::string_view module_name;
    std::string_view case_name;
    const char* file;
    int line;
    std::string_view operation;
    std::optional<std::string_view> condition;
    std::optional<std::string_view> message;
};

using TestReporter = void (*)(const TestFailure&) noexcept;
using TestFunction = void (*)() noexcept;

struct Registrar final {
    Registrar(
        std::string_view module_name,
        std::string_view case_name,
        TestFunction function
    ) noexcept;

    std::string_view module_name;
    std::string_view case_name;
    TestFunction function;
    Registrar* next = nullptr;
};

namespace detail {

inline auto registry() noexcept -> Registrar*& {
    static auto* head = static_cast<Registrar*>(nullptr);
    return head;
}

inline auto current_case() noexcept -> const Registrar*& {
    static auto* value = static_cast<const Registrar*>(nullptr);
    return value;
}

inline auto failure_count() noexcept -> int& {
    static auto value = 0;
    return value;
}

inline auto write(std::string_view text) noexcept -> void {
    std::fwrite(text.data(), sizeof(char), text.size(), stderr);
}

inline auto default_reporter(const TestFailure& failure) noexcept -> void {
    write("[carven] ");
    write(failure.module_name);
    write("::");
    write(failure.case_name);
    std::fprintf(stderr, "\n%s:%d: ", failure.file, failure.line);
    write(failure.operation);
    write(" failed\n");
    if (failure.condition.has_value()) {
        write("condition: ");
        write(*failure.condition);
        write("\n");
    }
    if (failure.message.has_value()) {
        write("message: ");
        write(*failure.message);
        write("\n");
    }
}

inline auto reporter() noexcept -> TestReporter& {
    static auto value = &default_reporter;
    return value;
}

inline auto report(
    const char* file,
    int line,
    std::string_view operation,
    std::optional<std::string_view> condition,
    std::optional<std::string_view> message
) noexcept -> void {
    ++failure_count();
    const auto* test = current_case();
    reporter()({
        .module_name = test == nullptr ? std::string_view {} : test->module_name,
        .case_name = test == nullptr ? std::string_view {} : test->case_name,
        .file = file,
        .line = line,
        .operation = operation,
        .condition = condition,
        .message = message,
    });
}

inline auto less(const Registrar& left, const Registrar& right) noexcept -> bool {
    return left.module_name < right.module_name
        || (left.module_name == right.module_name && left.case_name < right.case_name);
}

inline auto insert(Registrar* registration) noexcept -> void {
    auto** position = &registry();
    while (*position != nullptr && !less(*registration, **position)) {
        position = &(*position)->next;
    }
    registration->next = *position;
    *position = registration;
}

} // namespace detail

inline Registrar::Registrar(
    std::string_view module_name,
    std::string_view case_name,
    TestFunction function
) noexcept
    : module_name(module_name),
      case_name(case_name),
      function(function) {
    detail::insert(this);
}

inline auto set_reporter(TestReporter reporter) noexcept -> void {
    detail::reporter() = reporter == nullptr ? &detail::default_reporter : reporter;
}

inline auto check(bool condition, const char* file, int line, std::string_view source) noexcept
    -> void {
    if (!condition) {
        detail::report(file, line, "check", source, std::nullopt);
    }
}

inline auto check(
    bool condition,
    const char* file,
    int line,
    std::string_view source,
    std::string_view message
) noexcept -> void {
    if (!condition) {
        detail::report(file, line, "check", source, message);
    }
}

inline auto require(bool condition, const char* file, int line, std::string_view source) noexcept
    -> void {
    if (!condition) {
        detail::report(file, line, "require", source, std::nullopt);
    }
}

inline auto require(
    bool condition,
    const char* file,
    int line,
    std::string_view source,
    std::string_view message
) noexcept -> void {
    if (!condition) {
        detail::report(file, line, "require", source, message);
    }
}

inline auto fail(const char* file, int line) noexcept -> void {
    detail::report(file, line, "fail", std::nullopt, std::nullopt);
}

inline auto fail(const char* file, int line, std::string_view message) noexcept -> void {
    detail::report(file, line, "fail", std::nullopt, message);
}

inline auto run() noexcept -> int {
    detail::failure_count() = 0;
    for (const auto* test = detail::registry(); test != nullptr; test = test->next) {
        detail::current_case() = test;
        test->function();
    }
    detail::current_case() = nullptr;
    return detail::failure_count() == 0 ? 0 : 1;
}

} // namespace carven::testing
