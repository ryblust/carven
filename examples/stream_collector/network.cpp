#include "network.hpp"

#include <carven/api/examples/stream_collector/workflow.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

namespace api = carven::api::examples::stream_collector::workflow;
namespace async = carven::runtime::execution;

using IOError = decltype(api::io_error(0));

auto ensure(bool condition, const char* message) noexcept -> void {
    if (!condition) {
        std::fprintf(stderr, "%s (socket error %d)\n", message, collector::socket_error());
        std::exit(EXIT_FAILURE);
    }
}

auto nonblocking(collector::Socket socket) noexcept -> void {
#if defined(_WIN32)
    auto enabled = u_long(1);
    ensure(::ioctlsocket(socket, FIONBIO, &enabled) == 0, "set nonblocking socket");
#else
    const auto flags = ::fcntl(socket, F_GETFL);
    ensure(
        flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0,
        "set nonblocking socket"
    );
#if defined(__APPLE__)
    const auto enabled = 1;
    ensure(
        ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0,
        "disable SIGPIPE"
    );
#endif
#endif
}

} // namespace

collector::Connection::Connection() {
    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ensure(listener != invalid_socket, "create loopback listener");
    auto address = sockaddr_in {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    ensure(
        ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
        "bind loopback listener"
    );
    ensure(::listen(listener, 1) == 0, "listen on loopback");
#if defined(_WIN32)
    auto length = static_cast<int>(sizeof(address));
#else
    auto length = static_cast<socklen_t>(sizeof(address));
#endif
    ensure(
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0,
        "query loopback port"
    );
    writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ensure(writer != invalid_socket, "create publisher socket");
    ensure(
        ::connect(writer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
        "connect publisher"
    );
    reader = ::accept(listener, nullptr, nullptr);
    ensure(reader != invalid_socket, "accept publisher");
    close_socket(listener);
    nonblocking(reader);
    nonblocking(writer);
}

auto receive_chunk(const collector::Context& context) noexcept
    -> async::AsyncOperation<carven::runtime::Vector<std::uint8_t>, IOError> {
    using Result = async::Completion<carven::runtime::Vector<std::uint8_t>, IOError>;
    auto buffer = std::array<std::byte, 7> {};
    auto result = co_await context.network.read(context.connection.receiving_socket(), buffer);
    if (result.is_cancelled()) {
        co_return Result::cancelled();
    }
    if (const auto* error = result.failure_if<collector::SocketError>()) {
        co_return Result::failure(api::io_error(error->code));
    }
    const auto count = result.success_if()->value;
    auto bytes = carven::runtime::Vector<std::uint8_t>();
    for (auto index = std::size_t(0); index < count; ++index) {
        bytes.push(std::to_integer<std::uint8_t>(buffer[index]));
    }
    co_return Result::success_from([&]() noexcept { return std::move(bytes); });
}

auto send_stream(const collector::Context& context) noexcept
    -> async::AsyncOperation<void, IOError> {
    using Result = async::Completion<void, IOError>;
    const auto bytes = std::as_bytes(std::span(context.payload));
    auto offset = std::size_t(0);
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto fragment = bytes.subspan(offset, remaining < 5 ? remaining : 5);
        auto result = co_await context.network.write(context.connection.sending_socket(), fragment);
        if (result.is_cancelled()) {
            co_return Result::cancelled();
        }
        if (const auto* error = result.failure_if<collector::SocketError>()) {
            co_return Result::failure(api::io_error(error->code));
        }
        const auto count = result.success_if()->value;
        if (count == 0) {
            co_return Result::failure(api::io_error(-1));
        }
        offset += count;
    }
#if defined(_WIN32)
    const auto result = ::shutdown(context.connection.sending_socket(), SD_SEND);
#else
    const auto result = ::shutdown(context.connection.sending_socket(), SHUT_WR);
#endif
    if (result != 0) {
        co_return Result::failure(api::io_error(collector::socket_error()));
    }
    co_return Result::success();
}
