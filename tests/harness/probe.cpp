module carven:test.harness.probe;

import :test.harness.framework;
import std;

namespace {

static_assert(std::is_assignable_v<decltype(""_test), decltype([] static noexcept {})>);
static_assert(!std::is_assignable_v<decltype(""_test), decltype([] static {})>);
static_assert(!std::is_assignable_v<decltype(""_test), decltype([value = 1] noexcept {
                                        static_cast<void>(value);
                                    })>);
static_assert(!std::is_assignable_v<decltype(""_test), TestRegistration>);
static_assert(!std::is_assignable_v<decltype(""_test), TestRegistration&>);
static_assert(!std::is_assignable_v<decltype(""_test), const TestRegistration&>);

auto mode = std::string_view();
auto declaration_site = std::source_location();
auto duplicate_site = std::source_location();
auto assertion_site = std::source_location();
auto body_calls = 0;

const TestSuite suite([] static noexcept {
    if (mode == "duplicate") {
        declaration_site = std::source_location::current();
        "Framework probe: duplicate"_test = [] static noexcept {
            ++body_calls;
            expect(true);
        };
        duplicate_site = std::source_location::current();
        "Framework probe: duplicate"_test = [] static noexcept {
            ++body_calls;
            expect(true);
        };
        return;
    }
    if (mode == "no-assertions") {
        declaration_site = std::source_location::current();
        "Framework probe: no assertions"_test = [] static noexcept {
        };
        return;
    }
    if (mode == "failure") {
        "Framework probe: assertion location"_test = [] static noexcept {
            assertion_site = std::source_location::current();
            expect_equal(1, 2).note("failure probe");
        };
        return;
    }
    "Framework probe: second"_test = [] static noexcept {
        ++body_calls;
        expect_equal(current_test_name(), "Framework probe: second");
    };
    "Framework probe: first"_test = [] static noexcept {
        ++body_calls;
        expect_equal(current_test_name(), "Framework probe: first");
    };
});

auto reported_site(std::source_location previous) noexcept -> std::string {
    return std::format("{}:{}", previous.file_name(), previous.line() + 1u);
}

} // namespace

extern "C++" auto main(int argc, const char* const* argv) noexcept -> int {
    if (argc != 2) {
        std::cerr << "Expected one framework probe mode.\n";
        return 1;
    }
    mode = argv[1];
    if (body_calls != 0) {
        std::cerr << "Case bodies ran before collection.\n";
        return 1;
    }
    auto arguments = std::vector<const char*> {argv[0]};
    if (mode == "list") {
        arguments.push_back("--list-tests");
    } else if (mode == "selection") {
        arguments.insert(arguments.end(), {"--test", "Framework probe: first"});
    } else if (mode == "empty-selection") {
        arguments.insert(arguments.end(), {"--filter", "absent:*"});
    } else if (mode != "passing"
               && mode != "duplicate"
               && mode != "no-assertions"
               && mode != "failure") {
        std::cerr << "Unknown framework probe mode: " << mode << '\n';
        return 1;
    }
    auto output = std::ostringstream();
    auto errors = std::ostringstream();
    auto* previous_output = std::cout.rdbuf(output.rdbuf());
    auto* previous_errors = std::cerr.rdbuf(errors.rdbuf());
    const auto result = run_tests(static_cast<int>(arguments.size()), arguments.data());
    std::cout.rdbuf(previous_output);
    std::cerr.rdbuf(previous_errors);
    const auto text = errors.str();
    const auto passed = [&] noexcept {
        if (mode == "passing" || mode == "selection") {
            const auto expected_calls = mode == "passing" ? 2 : 1;
            return result == 0
                && body_calls == expected_calls
                && text.contains(std::format("Tests: {} passed, 0 failed", expected_calls));
        }
        if (mode == "list") {
            return result == 0
                && body_calls == 0
                && text.empty()
                && output.str() == "Framework probe: first\nFramework probe: second\n";
        }
        if (mode == "empty-selection") {
            return result == 1
                && body_calls == 0
                && text.contains("No tests matched the selection.");
        }
        if (mode == "duplicate") {
            return result == 1
                && body_calls == 0
                && text.contains("Duplicate test name:")
                && text.contains(reported_site(declaration_site))
                && text.contains(reported_site(duplicate_site));
        }
        if (mode == "no-assertions") {
            return result == 1
                && text.contains("Test executed no assertions:")
                && text.contains(reported_site(declaration_site));
        }
        return result == 1
            && text.contains("assertion failed")
            && text.contains(reported_site(assertion_site))
            && text.contains("failure probe");
    }();
    if (!passed) {
        std::cerr << "Framework probe failed: " << mode << " (runner status " << result << ")\n"
                  << output.str() << text;
        return 1;
    }
    return 0;
}
