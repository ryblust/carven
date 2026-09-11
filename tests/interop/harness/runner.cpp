#include <carven/api/tests/interop/discarded/operations.hpp>
#include <carven/api/tests/interop/unicode_contract/export_argument.hpp>

#include <carven/generated/carven-test-runner.hpp>

#include <csignal>
#include <cstdlib>
#include <string_view>

namespace {
auto terminated(int signal) noexcept -> void {
    std::_Exit(signal == SIGABRT ? 73 : 74);
}
} // namespace

// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) noexcept -> int {
    if (argc == 1) {
        return carven::runtime::run_generated_tests();
    }
    if (argc != 2) {
        return 1;
    }
    std::signal(SIGABRT, terminated);
    namespace api = carven::api::tests::interop::discarded::operations;
    const auto operation = std::string_view(argv[1]);
    if (operation == "divide") {
        api::divide(0);
    } else if (operation == "remainder") {
        api::remainder(0);
    } else if (operation == "shift") {
        api::shift(-1);
    } else if (operation == "width") {
        api::shift(32);
    } else if (operation == "index") {
        api::index(1);
    } else if (operation == "slice-index") {
        api::slice_index(1);
    } else if (operation == "slice-negative") {
        api::slice_index(-1);
    } else if (operation == "slice-range") {
        api::slice_range(0, 2);
    } else if (operation == "slice-reversed") {
        api::slice_range(1, 0);
    } else if (operation == "unicode") {
        api::unicode();
    } else if (operation == "unicode-export") {
        carven::api::tests::interop::unicode_contract::export_argument::accept_unicode_scalar(
            static_cast<char32_t>(0xd800)
        );
    }
    return 1;
}
