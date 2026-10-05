module carven:test.harness.main;

import :test.harness.framework;

extern "C++" auto main(int argc, const char* const* argv) noexcept -> int {
    return run_tests(argc, argv);
}
