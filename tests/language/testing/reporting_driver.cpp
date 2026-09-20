#include <carven/generated/carven-test-runner.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" auto cv_test_reporting_observed_flags() noexcept -> std::uint32_t;

namespace {

auto observed_output = std::string();
auto short_circuit_failures = 0;

auto report(const carven::runtime::TestFailure& failure) noexcept -> void {
    short_circuit_failures += failure.case_name
        == "Testing: dynamic short circuit explanations observe only executed operands";
    observed_output += failure.file;
    observed_output += ':';
    observed_output += std::to_string(failure.line);
    observed_output += "\noperation: ";
    observed_output += failure.operation;
    if (failure.condition.has_value()) {
        observed_output += "\ncondition: ";
        observed_output += *failure.condition;
    }
    if (failure.message.has_value()) {
        observed_output += "\nmessage: ";
        observed_output += *failure.message;
    }
    observed_output += '\n';
    observed_output += failure.explanation;
}

auto contains(const std::string& text, const std::string& expected) noexcept -> bool {
    return text.find(expected) != std::string::npos;
}

} // namespace

auto cv_test_reporting_verify() noexcept -> void {
    const auto result = carven::runtime::run_generated_tests(&report);
    const auto& text = observed_output;

    constexpr auto expected_flags = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4)
        | (1u << 6) | (1u << 8) | (1u << 10) | (1u << 12) | (1u << 17) | (1u << 19) | (1u << 21);
    if (result == 0
        || cv_test_reporting_observed_flags() != expected_flags
        || short_circuit_failures != 3) {
        std::abort();
    }
    if (!contains(text, "observed_boolean(&calls, true): <not evaluated>")
        || !contains(
            text,
            "observed_boolean(&calls, true): true\nobserved_boolean(&calls, false): false"
        )
        || !contains(
            text,
            "observed_boolean(&calls, false): false\nobserved_boolean(&calls, false): false"
        )
        || !contains(text, "comparison_next(&calls): 1\ncomparison_next(&calls): 2")
        || !contains(text, "actual: DisplayedRecord {\n    value: 12,\n    text: \"left\",\n}")
        || !contains(text, "expected: DisplayedRecord {\n    value: 15,\n    text: \"right\",\n}")
        || !contains(text, "comparison_next(&calls) == 1: <not evaluated>")
        || !contains(text, "comparison_next(&calls) == 2: false")
        || !contains(text, "calls: 1\n4: 4")
        || !contains(text, "1: 1\n2: 2")) {
        std::cerr << text;
        std::abort();
    }
    if (!contains(text, "tests/language/testing/reporting.cv:16")
        || !contains(text, "operation: check")
        || !contains(
            text,
            "condition: traced_reporting_condition(\n"
            "            0,\n"
            "            // The source contract preserves comments inside the "
            "expression span.\n"
            "            false,\n"
            "        )"
        )
        || !contains(text, "message: check message")
        || !contains(text, "tests/language/testing/reporting.cv:28")
        || !contains(text, "operation: require")
        || !contains(text, "condition: traced_reporting_condition(3, false)")
        || !contains(text, "message: require message")
        || !contains(text, "tests/language/testing/reporting.cv:33")
        || !contains(text, "operation: fail")
        || !contains(text, "message: fail message")
        || !contains(text, "message: direct temporary message with enough text for owned storage")
        || !contains(text, "message: explicit temporary message with enough text for owned storage")
        || !contains(
            text,
            "message: callable temporary message with enough text for owned storage"
        )) {
        std::cerr << text;
        std::abort();
    }
}
