module carven:test.harness.main;

import :test.harness.framework;

extern "C++" auto main(int argc, const char* const* argv) noexcept -> int {
    return carven::testing::run(argc, argv);
}
