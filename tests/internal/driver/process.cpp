module carven:test.internal.driver.process;

import :driver.process;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Process: missing executable and NUL arguments report launch errors",
        [] static noexcept {
            ct::expect(!(run_process({}).has_value()));
            ct::expect(!(run_process({"carven-test-missing-executable-5e5d3fd2"}).has_value()));
            ct::expect(!(run_process({"unused", std::string("a\0b", 3uz)}).has_value()));
        }
    );

    ct::test(
        "Process: temporary run directories are distinct and owned by each request",
        [] static noexcept {
            const auto first = create_run_directory();
            if (!ct::expect(first.has_value())) {
                return;
            }
            const auto second = create_run_directory();
            if (!ct::expect(second.has_value())) {
                return;
            }
            ct::expect(*first != *second);
            ct::expect(std::filesystem::is_directory(*first));
            ct::expect(std::filesystem::is_directory(*second));
            auto error = std::error_code();
            ct::expect(std::filesystem::remove(*first, error));
            ct::expect(!(error));
            ct::expect(std::filesystem::remove(*second, error));
            ct::expect(!(error));
        }
    );
});

} // namespace
