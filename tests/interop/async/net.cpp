#include <carven/std/net/native.hpp>

#include <carven/api/crafts/carven/std/net.hpp>
#include <carven/api/tests/interop/async/net.hpp>

#include <cstdio>
#include <cstdlib>
#include <optional>

#if !defined(_WIN32)
#include <netinet/in.h>
#endif

namespace {

namespace net = carven::standard::net;
namespace api = carven::api::tests::interop::async::net;
namespace async = carven::runtime::execution;

auto check(bool value, const char* message) noexcept -> void {
    if (!value) {
        std::fprintf(stderr, "network contract failed: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class Pair final {
    std::optional<net::Connection> receiving;
    std::optional<net::Connection> sending;

public:
    explicit Pair(net::Network& network) noexcept;
    auto reader() const noexcept -> const net::Connection&;
    auto writer() const noexcept -> const net::Connection&;
};

Pair::Pair(net::Network& network) noexcept {
    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(listener != net::invalid_socket, "create listener");
    auto address = sockaddr_in {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(
        ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
        "bind listener"
    );
    check(::listen(listener, 1) == 0, "listen");
#if defined(_WIN32)
    auto length = static_cast<int>(sizeof(address));
#else
    auto length = static_cast<socklen_t>(sizeof(address));
#endif
    check(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0, "port");
    const auto writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(writer != net::invalid_socket, "create writer");
    check(
        ::connect(writer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
        "connect"
    );
    const auto reader = ::accept(listener, nullptr, nullptr);
    check(reader != net::invalid_socket, "accept");
    net::close_socket(listener);
    const auto small = 4096;
    check(
        ::setsockopt(
            writer,
            SOL_SOCKET,
            SO_SNDBUF,
            reinterpret_cast<const char*>(&small),
            sizeof(small)
        ) == 0,
        "bounded send buffer"
    );
    check(
        ::setsockopt(
            reader,
            SOL_SOCKET,
            SO_RCVBUF,
            reinterpret_cast<const char*>(&small),
            sizeof(small)
        ) == 0,
        "bounded receive buffer"
    );
    receiving.emplace(network, reader);
    sending.emplace(network, writer);
    check(receiving->startup_error() == 0 && sending->startup_error() == 0, "configure endpoints");
}

auto Pair::reader() const noexcept -> const net::Connection& {
    return *receiving;
}

auto Pair::writer() const noexcept -> const net::Connection& {
    return *sending;
}

template<typename Operation>
auto succeeds(Operation operation, net::Network& network) noexcept -> bool {
    auto completion = async::drive_root(std::move(operation), &network);
    const auto* result = completion.success_if();
    return result != nullptr && result->value;
}

} // namespace

auto run_net_cases() noexcept -> void {
    auto network = net::Network();
    check(network.startup_error() == 0, "initialize");
    const auto pair = Pair(network);
    namespace net_api = carven::api::crafts::carven::std::net;
    const auto reader = net_api::borrow({&pair.reader()});
    const auto writer = net_api::borrow({&pair.writer()});
    {
        const auto unstarted = api::at_end(reader);
    }
    check(succeeds(api::conflict(reader), network), "same-direction conflict and cancel drain");
    check(succeeds(api::timed_out(reader), network), "timeout drain");
    check(
        succeeds(api::duplex(reader, writer), network),
        "simultaneous read and write on one connection"
    );
    auto payload = carven::runtime::Vector<std::uint8_t>();
    for (const auto value : {0xe4, 0xb8, 0x96, 0xe7, 0x95, 0x8c, 0x0d, 0x0a}) {
        payload.push(static_cast<std::uint8_t>(value));
    }
    check(
        succeeds(api::transfer(reader, writer, payload, 1), network),
        "fragmented bytes after cancellation"
    );
    payload.clear();
    for (auto index = 0; index < 262144; ++index) {
        payload.push(static_cast<std::uint8_t>(index % 251));
    }
    check(
        succeeds(api::transfer(reader, writer, payload, 4096), network),
        "backpressure and short writes"
    );
    check(pair.writer().shutdown_send().kind == net::ErrorKind::None, "shutdown sending");
    check(succeeds(api::at_end(reader), network), "normal EOF");
    check(succeeds(api::failed_write(writer), network), "send failure without SIGPIPE");
    check(pair.reader().close().kind == net::ErrorKind::None, "close after providers drain");
    check(pair.reader().close().kind == net::ErrorKind::None, "idempotent close");
    check(succeeds(api::closed(reader), network), "closed connection");
    {
        const auto blocked = Pair(network);
        const auto view = net_api::borrow({&blocked.writer()});
        payload.clear();
        for (auto index = 0; index < 4 * 1024 * 1024; ++index) {
            payload.push(1);
        }
        check(
            succeeds(api::cancel_write(view, std::move(payload)), network),
            "pending write conflict and cancellation drain"
        );
        check(
            blocked.writer().close().kind == net::ErrorKind::None,
            "write cancellation releases registration before close"
        );
    }
    std::puts("network: pass");
}
