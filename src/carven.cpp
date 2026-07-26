module carven:main;

import :driver.cli;

extern "C++" auto main(int argc, const char* const* argv) noexcept -> int {
    return carven_main(argc, argv);
}
