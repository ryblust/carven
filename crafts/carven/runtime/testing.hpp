#pragma once

#include "outcome.hpp"
#include "report.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string_view>
#include <type_traits>

namespace carven::runtime {

struct TestStopped final {};

// Native calls retain declared failures; a test stop cannot cross this boundary.
template<typename Result, typename... Failures>
auto native_test_result(Outcome<Result, TestStopped, Failures...>&& outcome) noexcept
    -> std::conditional_t<sizeof...(Failures) == 0, Result, Outcome<Result, Failures...>> {
    using NativeResult =
        std::conditional_t<sizeof...(Failures) == 0, Result, Outcome<Result, Failures...>>;
    if (auto* success = outcome.success_if()) {
        if constexpr (sizeof...(Failures) == 0) {
            if constexpr (!std::is_void_v<Result>) {
                return transfer(success->value);
            } else {
                return;
            }
        } else if constexpr (std::is_void_v<Result>) {
            return NativeResult::success();
        } else {
            return NativeResult::success_from([&]() noexcept -> Result {
                return transfer(success->value);
            });
        }
    }
    if constexpr (sizeof...(Failures) > 0) {
        const auto propagate = [&]<typename First, typename... Rest>(
                                   const auto& self,
                                   std::type_identity<First>,
                                   std::type_identity<Rest>... rest
                               ) noexcept -> NativeResult {
            if (auto* failure = outcome.template failure_if<First>()) {
                return NativeResult::failure(transfer(*failure));
            }
            if constexpr (sizeof...(Rest) > 0) {
                return self(self, rest...);
            } else {
                std::terminate();
            }
        };
        return propagate(propagate, std::type_identity<Failures> {}...);
    } else {
        std::terminate();
    }
}

namespace detail {

[[noreturn]] inline auto testing_contract_error() noexcept -> void {
    std::fputs("carven testing contract error\n", stderr);
    std::abort();
}

} // namespace detail

struct TestFailure final {
    std::string_view module_name;
    std::string_view case_name;
    std::string_view file;
    std::uint32_t line;
    std::uint32_t column;
    std::string_view operation;
    std::optional<std::string_view> condition;
    std::optional<std::string_view> message;
    std::string_view explanation;
};

using TestReporter = void (*)(const TestFailure&) noexcept;

namespace detail {

inline auto default_reporter(const TestFailure& failure) noexcept -> void {
    write_failure(
        failure.file,
        failure.line,
        failure.column,
        failure.operation,
        failure.condition,
        failure.message,
        failure.explanation
    );
}

} // namespace detail

class TestContext;

namespace detail {

inline thread_local TestContext* active_test_context = nullptr;
} // namespace detail

class TestContext final {
public:
    explicit TestContext(TestReporter source_reporter = nullptr) noexcept
        : reporter(source_reporter == nullptr ? &detail::default_reporter : source_reporter) {}

    auto begin_case(std::string_view module_name, std::string_view case_name) noexcept -> void {
        if (active.has_value()) {
            detail::testing_contract_error();
        }
        case_failed = false;
        previous_context = detail::active_test_context;
        detail::active_test_context = this;
        active = TestReportContext {.module_name = module_name, .case_name = case_name};
        active_test_report = &*active;
    }

    auto end_case() noexcept -> void {
        if (!active.has_value()) {
            detail::testing_contract_error();
        }
        detail::active_test_context = previous_context;
        active_test_report = previous_context != nullptr ? &*previous_context->active : nullptr;
        previous_context = nullptr;
        ++total;
        failures += case_failed ? 1u : 0u;
        active.reset();
    }

    auto report_failure(
        std::string_view file,
        std::uint32_t line,
        std::uint32_t column,
        std::string_view operation,
        std::optional<std::string_view> condition,
        std::optional<std::string_view> message,
        std::string_view explanation
    ) noexcept -> void {
        if (!active.has_value()) {
            detail::testing_contract_error();
        }
        case_failed = true;
        reporter({
            .module_name = active->module_name,
            .case_name = active->case_name,
            .file = file,
            .line = line,
            .column = column,
            .operation = operation,
            .condition = condition,
            .message = message,
            .explanation = explanation,
        });
    }

    auto result() const noexcept -> int {
        if (active.has_value()) {
            detail::testing_contract_error();
        }
        return failures != 0 ? 1 : 0;
    }

    auto finish() const noexcept -> int {
        const auto status = result();
        if (reporter == &detail::default_reporter) {
            std::fprintf(
                stderr,
                "carven: tests: %zu passed; %zu failed\n",
                total - failures,
                failures
            );
        }
        return status;
    }

private:
    TestContext* previous_context = nullptr;
    TestReporter reporter;
    bool case_failed = false;
    std::size_t total = 0;
    std::size_t failures = 0;
    std::optional<TestReportContext> active;
};

inline auto current_test() noexcept -> TestContext& {
    if (detail::active_test_context == nullptr) {
        detail::testing_contract_error();
    }
    return *detail::active_test_context;
}

} // namespace carven::runtime
