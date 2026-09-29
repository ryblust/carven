module carven:test.harness.framework.impl;

import :support.quote;
import :test.harness.framework;
import std;

namespace carven::testing {
namespace {

struct TestCase final {
    std::string name;
    TestFunction body;
    std::source_location location;
};

enum class Phase : std::uint8_t { Declaration, Collection, Execution, Complete };

struct RunnerState final {
    std::vector<TestFunction> suites;
    std::vector<TestCase> tests;
    std::vector<std::string> contexts;
    std::string_view current_test_name;
    std::size_t assertions_passed;
    std::size_t assertions_failed;
    Phase phase;
    std::size_t harness_errors;
};

auto runner_state() noexcept -> RunnerState& {
    static auto instance = RunnerState {
        .suites = {},
        .tests = {},
        .contexts = {},
        .current_test_name = {},
        .assertions_passed = 0uz,
        .assertions_failed = 0uz,
        .phase = Phase::Declaration,
        .harness_errors = 0uz,
    };
    return instance;
}

struct RunOptions final {
    std::string_view include_pattern;
    std::string_view exclude_pattern;
    std::string_view test_name;
    bool list_tests;
    bool show_help;
};

auto matches_pattern(std::string_view text, std::string_view pattern) noexcept -> bool {
    auto text_index = 0uz;
    auto pattern_index = 0uz;
    auto star_index = std::string_view::npos;
    auto retry_text_index = 0uz;
    while (text_index < text.size()) {
        if (pattern_index < pattern.size()
            && (pattern[pattern_index] == '?' || pattern[pattern_index] == text[text_index])) {
            ++text_index;
            ++pattern_index;
        } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            retry_text_index = text_index;
        } else if (star_index != std::string_view::npos) {
            pattern_index = star_index + 1uz;
            text_index = ++retry_text_index;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

auto is_selected(std::string_view name, const RunOptions& options) noexcept -> bool {
    return (options.test_name.empty() || name == options.test_name)
        && (options.include_pattern.empty() || matches_pattern(name, options.include_pattern))
        && (options.exclude_pattern.empty() || !matches_pattern(name, options.exclude_pattern));
}

auto parse_options(int argc, const char* const* argv) noexcept -> std::optional<RunOptions> {
    auto options = RunOptions {
        .include_pattern = {},
        .exclude_pattern = {},
        .test_name = {},
        .list_tests = false,
        .show_help = false,
    };
    for (auto index = 1; index < argc; ++index) {
        const auto argument = std::string_view(argv[index]);
        if (argument == "--list-tests" || argument == "--help") {
            auto& flag = argument == "--list-tests" ? options.list_tests : options.show_help;
            if (flag) {
                std::cerr << "Invalid test option: " << argument << '\n';
                return std::nullopt;
            }
            flag = true;
            continue;
        }
        auto* destination = argument == "--filter" ? &options.include_pattern
            : argument == "--exclude"              ? &options.exclude_pattern
            : argument == "--test"                 ? &options.test_name
                                                   : nullptr;
        if (destination == nullptr || !destination->empty()) {
            std::cerr << "Invalid test option: " << argument << '\n';
            return std::nullopt;
        }
        if (index + 1 == argc || std::string_view(argv[index + 1]).starts_with("--")) {
            std::cerr << "Missing value for test option: " << argument << '\n';
            return std::nullopt;
        }
        *destination = argv[++index];
        if (destination->empty()) {
            std::cerr << "Test selection must not be empty.\n";
            return std::nullopt;
        }
    }
    return options;
}

auto collect_tests() noexcept -> bool {
    auto& runner = runner_state();
    runner.phase = Phase::Collection;
    for (const auto declare : runner.suites) {
        declare();
    }
    std::ranges::sort(runner.tests, {}, &TestCase::name);
    const TestCase* previous = nullptr;
    for (const auto& entry : runner.tests) {
        if (entry.name.empty() || entry.body == nullptr) {
            std::cerr << entry.location.file_name() << ':' << entry.location.line()
                      << ": Invalid test declaration: " << entry.name << '\n';
            ++runner.harness_errors;
        } else if (previous != nullptr && entry.name == previous->name) {
            std::cerr << entry.location.file_name() << ':' << entry.location.line()
                      << ": Duplicate test name: " << quote_text(entry.name)
                      << "\n  also declared at " << previous->location.file_name() << ':'
                      << previous->location.line() << '\n';
            ++runner.harness_errors;
        }
        previous = &entry;
    }
    return runner.harness_errors == 0uz;
}

} // namespace

Suite::Suite(TestFunction declare) noexcept {
    auto& runner = runner_state();
    if (runner.phase != Phase::Declaration || declare == nullptr) {
        ++runner.harness_errors;
        std::cerr << "Invalid test suite declaration.\n";
        return;
    }
    runner.suites.push_back(declare);
}

auto test(std::string_view name, TestFunction body, std::source_location location) noexcept
    -> void {
    auto& runner = runner_state();
    if (runner.phase != Phase::Collection) {
        ++runner.harness_errors;
        std::cerr << "Test declared outside collection: " << name << '\n';
        return;
    }
    runner.tests.push_back({.name = std::string(name), .body = body, .location = location});
}

namespace detail {

auto write_text_difference(
    std::ostream& output,
    std::string_view actual,
    std::string_view expected
) noexcept -> void {
    auto offset = 0uz;
    while (
        offset < actual.size() && offset < expected.size() && actual[offset] == expected[offset]) {
        ++offset;
    }
    const auto write_excerpt = [&](std::string_view value) noexcept {
        constexpr auto limit = 120uz;
        constexpr auto context = 32uz;
        const auto start = value.size() <= limit || offset <= context ? 0uz : offset - context;
        const auto length = std::min(limit, value.size() - start);
        if (start != 0uz) {
            output << "...";
        }
        output << quote_text(value.substr(start, length));
        if (start + length < value.size()) {
            output << "...";
        }
    };
    output << "\n  actual:   ";
    write_excerpt(actual);
    output << "\n  expected: ";
    write_excerpt(expected);
    output << "\n  lengths: actual " << actual.size() << ", expected " << expected.size()
           << "\n  first difference at byte " << offset;
}

auto record_assertion(bool passed, std::source_location location) noexcept -> bool {
    auto& runner = runner_state();
    const auto outside_test = runner.current_test_name.empty();
    if (outside_test) {
        ++runner.harness_errors;
        passed = false;
    }
    if (passed) {
        ++runner.assertions_passed;
        return true;
    }
    ++runner.assertions_failed;
    std::cerr << location.file_name() << ':' << location.line() << ": ";
    if (outside_test) {
        std::cerr << "assertion outside a test";
    } else {
        std::cerr << "assertion failed in " << quote_text(runner.current_test_name);
    }
    for (const auto& context : runner.contexts) {
        std::cerr << "\n  context: " << quote_text(context);
    }
    return false;
}

auto failure_output() noexcept -> std::ostream& {
    return std::cerr;
}

ScenarioContext::ScenarioContext(std::string_view name) noexcept {
    runner_state().contexts.emplace_back(name);
}

ScenarioContext::~ScenarioContext() noexcept {
    runner_state().contexts.pop_back();
}

} // namespace detail

Assertion::Assertion(bool condition, bool fatal, std::source_location location) noexcept
    : passed(detail::record_assertion(condition, location)),
      fatal(fatal) {}

Assertion::~Assertion() noexcept {
    if (!passed) {
        std::cerr << '\n';
        std::cerr.flush();
        if (fatal) {
            std::abort();
        }
    }
}

Assertion::operator bool() const noexcept {
    return passed;
}

auto current_test_name() noexcept -> std::string_view {
    return runner_state().current_test_name;
}

auto run(int argc, const char* const* argv) noexcept -> int {
    const auto options = parse_options(argc, argv);
    if (!options.has_value()) {
        return 2;
    }
    if (options->show_help) {
        std::cout << "Usage: " << argv[0]
                  << " [--filter PATTERN] [--exclude PATTERN] [--test NAME] [--list-tests]\n"
                  << "Patterns support * and ?. An empty selection fails.\n";
        return 0;
    }
    auto& runner = runner_state();
    if (runner.phase != Phase::Declaration) {
        std::cerr << "Tests may only run once.\n";
        return 1;
    }
    if (!collect_tests()) {
        runner.phase = Phase::Complete;
        return 1;
    }
    runner.phase = Phase::Execution;
    auto selected_count = 0uz;
    auto passed_count = 0uz;
    auto failed_count = 0uz;
    for (const auto& entry : runner.tests) {
        if (!is_selected(entry.name, *options)) {
            continue;
        }
        ++selected_count;
        if (options->list_tests) {
            std::cout << entry.name << '\n';
            continue;
        }
        runner.current_test_name = entry.name;
        const auto passed_before = runner.assertions_passed;
        const auto failed_before = runner.assertions_failed;
        const auto errors_before = runner.harness_errors;
        entry.body();
        runner.current_test_name = {};
        if (runner.assertions_passed == passed_before
            && runner.assertions_failed == failed_before) {
            std::cerr << entry.location.file_name() << ':' << entry.location.line()
                      << ": Test executed no assertions: " << quote_text(entry.name) << '\n';
            ++failed_count;
        } else if (runner.assertions_failed != failed_before
                   || runner.harness_errors != errors_before) {
            ++failed_count;
        } else {
            ++passed_count;
        }
    }
    runner.phase = Phase::Complete;
    if (selected_count == 0uz) {
        std::cerr << "No tests matched the selection.\n";
        if (!options->test_name.empty()) {
            std::cerr << "  test: " << quote_text(options->test_name) << '\n';
        }
        if (!options->include_pattern.empty()) {
            std::cerr << "  filter: " << quote_text(options->include_pattern) << '\n';
        }
        if (!options->exclude_pattern.empty()) {
            std::cerr << "  exclude: " << quote_text(options->exclude_pattern) << '\n';
        }
        return 1;
    }
    if (!options->list_tests) {
        std::cerr << "Tests: " << passed_count << " passed, " << failed_count
                  << " failed; assertions: " << runner.assertions_passed << " passed, "
                  << runner.assertions_failed << " failed\n";
    }
    return failed_count != 0uz || runner.harness_errors != 0uz ? 1 : 0;
}

} // namespace carven::testing
