#include <carven/runtime/execution/execution.hpp>

#include <carven/api/tests/interop/async/ownership.hpp>

#include <cstdio>
#include <cstdlib>
#include <string_view>

auto run_net_cases() noexcept -> void;

auto run_timer_cases() noexcept -> void;
auto run_cleanup_cases() noexcept -> void;
auto run_stop_case() noexcept -> int;

#if defined(__unix__) || defined(__APPLE__)
auto run_pipe_cases() noexcept -> void;
auto run_bridge_cases() noexcept -> void;
auto run_invalid_character(std::string_view name) noexcept -> int;
#endif

// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) noexcept -> int {
    if (argc != 2) {
        return EXIT_FAILURE;
    }
    if (std::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
        return EXIT_FAILURE;
    }
    const auto operation = std::string_view(argv[1]);
    if (operation == "net") {
        run_net_cases();
    } else if (operation == "timer") {
        run_timer_cases();
    } else if (operation == "cleanup") {
        run_cleanup_cases();
    } else if (operation == "native-stop") {
        return run_stop_case();
    } else if (operation == "ownership") {
        auto result = carven::runtime::execution::drive_root(
            carven::api::tests::interop::async::ownership::run_ownership()
        );
        if (result.success_if() == nullptr) {
            return EXIT_FAILURE;
        }
#if defined(__unix__) || defined(__APPLE__)
    } else if (operation == "pipe") {
        run_pipe_cases();
    } else if (operation == "bridge") {
        run_bridge_cases();
    } else if (operation.starts_with("invalidchar")) {
        return run_invalid_character(operation);
#endif
    } else {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
