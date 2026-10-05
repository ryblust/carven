module carven:test.internal.driver.process;

import :driver.process;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Process: missing executable and NUL arguments report launch errors"_test = [] static noexcept {
        expect(!(run_process({}).has_value()));
        expect(!(run_process({"carven-test-missing-executable-5e5d3fd2"}).has_value()));
        expect(!(run_process({"unused", std::string("a\0b", 3uz)}).has_value()));
    };

    "Process: temporary run directories are distinct and owned by each request"_test =
        [] static noexcept {
            const auto first = create_run_directory();
            if (!expect(first.has_value())) {
                return;
            }
            const auto second = create_run_directory();
            if (!expect(second.has_value())) {
                return;
            }
            expect(*first != *second);
            expect(std::filesystem::is_directory(*first));
            expect(std::filesystem::is_directory(*second));
            auto error = std::error_code();
            expect(std::filesystem::remove(*first, error));
            expect(!(error));
            expect(std::filesystem::remove(*second, error));
            expect(!(error));
        };
});

} // namespace
