#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace carven::testing {

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
    std::string_view operation;
    std::optional<std::string_view> condition;
    std::optional<std::string_view> message;
};

using TestReporter = void (*)(const TestFailure&) noexcept;

namespace detail {

inline auto write(std::string_view text) noexcept -> void {
    std::fwrite(text.data(), sizeof(char), text.size(), stderr);
}

inline auto default_reporter(const TestFailure& failure) noexcept -> void {
    write("[carven] ");
    write(failure.module_name);
    write("::");
    write(failure.case_name);
    write("\n");
    write(failure.file);
    std::fprintf(stderr, ":%" PRIu32 ": ", failure.line);
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

} // namespace detail

class TestContext final {
public:
    explicit TestContext(TestReporter source_reporter = nullptr) noexcept
        : reporter(source_reporter == nullptr ? &detail::default_reporter : source_reporter) {}

    auto begin_case(std::string_view module_name, std::string_view case_name) noexcept -> void {
        if (active.has_value()) {
            detail::testing_contract_error();
        }
        active = ActiveTestCase {.module_name = module_name, .case_name = case_name};
    }

    auto end_case() noexcept -> void {
        if (!active.has_value()) {
            detail::testing_contract_error();
        }
        active.reset();
    }

    auto report_failure(
        std::string_view file,
        std::uint32_t line,
        std::string_view operation,
        std::optional<std::string_view> condition,
        std::optional<std::string_view> message
    ) noexcept -> void {
        if (!active.has_value()) {
            detail::testing_contract_error();
        }
        failed = true;
        reporter({
            .module_name = active->module_name,
            .case_name = active->case_name,
            .file = file,
            .line = line,
            .operation = operation,
            .condition = condition,
            .message = message,
        });
    }

    auto result() const noexcept -> int {
        if (active.has_value()) {
            detail::testing_contract_error();
        }
        return failed ? 1 : 0;
    }

private:
    struct ActiveTestCase final {
        std::string_view module_name;
        std::string_view case_name;
    };

    TestReporter reporter;
    bool failed = false;
    std::optional<ActiveTestCase> active;
};

} // namespace carven::testing
