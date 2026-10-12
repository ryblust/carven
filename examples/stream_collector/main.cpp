#include "network.hpp"

#include <carven/api/examples/stream_collector/workflow.hpp>

#include <csignal>
#include <iostream>
#include <string_view>

auto main(int argc, const char* const* argv) -> int {
#if defined(_WIN32)
    auto sockets = WSADATA {};
    if (::WSAStartup(MAKEWORD(2, 2), &sockets) != 0) {
        return 1;
    }
#elif !defined(__APPLE__)
    std::signal(SIGPIPE, SIG_IGN);
#endif
    auto records_only = false;
    auto from_stdin = false;
    for (auto index = 1; index < argc; ++index) {
        const auto argument = std::string_view(argv[index]);
        if (argument == "--records") {
            records_only = true;
        }
        if (argument == "--stdin") {
            from_stdin = true;
        }
    }
    {
        auto network = collector::Network();
        const auto first = collector::Connection();
        const auto second = collector::Connection();
        const auto stalled = collector::Connection();
        namespace api = carven::api::examples::stream_collector::workflow;
        auto payload = std::string(
            "INFO 12 collector connected\nWARN 37 mixer token=demo clipped=2\nERROR 42 upstream timed out\n"
        );
        if (from_stdin) {
            const auto input = collector::async::drive_root(api::read_input());
            const auto* value = input.success_if();
            if (value == nullptr) {
                std::cerr << "standard input failed\n";
                return 1;
            }
            payload.assign(value->value.begin(), value->value.end());
        }
        const auto left = collector::Context {
            .network = network,
            .connection = first,
            .payload = std::move(payload)
        };
        const auto right = collector::Context {
            .network = network,
            .connection = second,
            .payload = "INFO 9 incomplete"
        };
        const auto idle =
            collector::Context {.network = network, .connection = stalled, .payload = ""};
        const auto result =
            collector::async::drive_root(api::collect(left, right, idle, records_only), &network);
        const auto* success = result.success_if();
        if (success == nullptr) {
            std::cerr << "collection failed\n";
            return 1;
        }
        std::cout << success->value.as_str();
    }
#if defined(_WIN32)
    ::WSACleanup();
#endif
}
