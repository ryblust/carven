#include <carven/std/net/native.hpp>

#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/execution/clock.hpp>

#include <algorithm>
#include <cassert>
#include <climits>
#include <memory>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
#error "The TCP adapter requires per-send or per-socket SIGPIPE suppression"
#endif
#endif

namespace carven::standard::net {

namespace {

auto interrupted(int code) noexcept -> bool {
#if defined(_WIN32)
    return code == WSAEINTR;
#else
    return code == EINTR;
#endif
}

auto would_block(int code) noexcept -> bool {
#if defined(_WIN32)
    return code == WSAEWOULDBLOCK;
#else
    return code == EAGAIN || code == EWOULDBLOCK;
#endif
}

} // namespace

auto socket_error() noexcept -> int {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

auto close_socket(Socket socket) noexcept -> void {
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

Network::Network() noexcept {
#if defined(_WIN32)
    auto data = WSADATA {};
    initialization_error = ::WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

Network::~Network() noexcept {
    assert(first == nullptr);
#if defined(_WIN32)
    if (initialization_error == 0) {
        ::WSACleanup();
    }
#endif
}

auto Network::startup_error() const noexcept -> int {
    return initialization_error;
}

Connection::Connection(Network& network, Socket owned_socket) noexcept
    : network(network),
      socket(owned_socket) {
    assert(socket != invalid_socket);
#if defined(_WIN32)
    auto enabled = u_long(1);
    if (::ioctlsocket(socket, FIONBIO, &enabled) != 0) {
        initialization_error = socket_error();
    }
#else
    const auto flags = ::fcntl(socket, F_GETFL);
    if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
        initialization_error = socket_error();
    }
#if defined(SO_NOSIGPIPE)
    const auto enabled = 1;
    if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        initialization_error = socket_error();
    }
#endif
#endif
    if (initialization_error != 0) {
        close_socket(std::exchange(socket, invalid_socket));
    }
}

Connection::~Connection() noexcept {
    assert(!reading && !writing);
    static_cast<void>(close());
}

auto Connection::startup_error() const noexcept -> int {
    return initialization_error;
}

auto Connection::native_socket() const noexcept -> Socket {
    return socket;
}

auto Connection::close() const noexcept -> Error {
    if (reading || writing) {
        return {.kind = ErrorKind::Conflict, .code = 0};
    }
    if (socket != invalid_socket) {
        close_socket(std::exchange(socket, invalid_socket));
    }
    return {.kind = ErrorKind::None, .code = 0};
}

auto Connection::shutdown_send() const noexcept -> Error {
    if (socket == invalid_socket) {
        return {.kind = ErrorKind::Closed, .code = 0};
    }
    if (writing) {
        return {.kind = ErrorKind::Conflict, .code = 0};
    }
#if defined(_WIN32)
    const auto result = ::shutdown(socket, SD_SEND);
#else
    const auto result = ::shutdown(socket, SHUT_WR);
#endif
    return result == 0 ? Error {.kind = ErrorKind::None, .code = 0}
                       : Error {.kind = ErrorKind::System, .code = socket_error()};
}

auto Connection::read(std::span<std::byte> buffer) const noexcept -> Network::Awaiter {
    assert(!buffer.empty());
    return Network::Awaiter(*this, buffer, false);
}

auto Connection::write(std::span<const std::byte> buffer) const noexcept -> Network::Awaiter {
    return Network::Awaiter(*this, buffer, true);
}

Network::Awaiter::Awaiter(
    const Connection& connection,
    std::span<const std::byte> buffer,
    bool writing
) noexcept
    : connection(connection),
      buffer(buffer),
      writing(writing) {}

Network::Awaiter::~Awaiter() noexcept {
    assert(!continuation.bound() || completion.has_value());
}

auto Network::Awaiter::await_ready() const noexcept -> bool {
    return false;
}

auto Network::Awaiter::attempt() const noexcept -> std::optional<Completion> {
    if (buffer.empty()) {
        return Completion::success(0);
    }
    const auto size = static_cast<int>(std::min(buffer.size(), static_cast<std::size_t>(INT_MAX)));
    for (;;) {
#if defined(MSG_NOSIGNAL)
        constexpr auto send_flags = MSG_NOSIGNAL;
#else
        constexpr auto send_flags = 0;
#endif
        const auto count = writing
            ? ::send(
                  connection.socket,
                  reinterpret_cast<const char*>(buffer.data()),
                  size,
                  send_flags
              )
            : ::recv(
                  connection.socket,
                  reinterpret_cast<char*>(const_cast<std::byte*>(buffer.data())),
                  size,
                  0
              );
        if (count >= 0) {
            return Completion::success(static_cast<std::size_t>(count));
        }
        const auto code = socket_error();
        if (interrupted(code)) {
            continue;
        }
        if (would_block(code)) {
            return std::nullopt;
        }
        return Completion::failure(Error {.kind = ErrorKind::System, .code = code});
    }
}

auto Network::Awaiter::finish(Completion result) noexcept -> void {
    (writing ? connection.writing : connection.reading) = false;
    completion.emplace(std::move(result));
}

auto Network::Awaiter::suspend() noexcept -> bool {
    if (continuation.cancel_requested()) {
        completion.emplace(Completion::cancelled());
        return false;
    }
    if (connection.socket == invalid_socket) {
        completion.emplace(Completion::failure(Error {.kind = ErrorKind::Closed, .code = 0}));
        return false;
    }
    auto& busy = writing ? connection.writing : connection.reading;
    if (busy) {
        completion.emplace(Completion::failure(Error {.kind = ErrorKind::Conflict, .code = 0}));
        return false;
    }
    busy = true;
    auto ready = attempt();
    if (ready) {
        finish(std::move(*ready));
        return false;
    }
    next = connection.network.first;
    connection.network.first = this;
    return true;
}

auto Network::Awaiter::await_resume() noexcept -> Completion {
    assert(completion.has_value());
    return std::move(*completion);
}

auto Network::poll(async::Driver& driver) noexcept -> void {
    auto** link = &first;
    while (*link != nullptr) {
        auto* request = *link;
        auto ready = request->continuation.cancel_requested()
            ? std::optional(Completion::cancelled())
            : request->completion ? std::optional(std::move(*request->completion))
                                  : request->attempt();
        if (!ready) {
            link = &request->next;
            continue;
        }
        *link = request->next;
        request->next = nullptr;
        request->finish(std::move(*ready));
        request->continuation.enqueue(driver);
    }
}

auto Network::wait_ready(std::optional<std::int64_t> deadline) noexcept -> void {
    // Poll readiness only; completion is delivered by poll on the driver thread.
#if defined(_WIN32)
    auto descriptors = std::vector<WSAPOLLFD>();
#else
    auto descriptors = std::vector<pollfd>();
#endif
    for (const auto* request = first; request != nullptr; request = request->next) {
        descriptors.push_back(
            {.fd = request->connection.socket,
             .events = static_cast<short>(request->writing ? POLLOUT : POLLIN),
             .revents = 0}
        );
    }
    for (;;) {
        const auto timeout = async::detail::steady_poll_timeout(deadline);
#if defined(_WIN32)
        const auto result =
            ::WSAPoll(descriptors.data(), static_cast<ULONG>(descriptors.size()), timeout);
#else
        const auto result = ::poll(descriptors.data(), descriptors.size(), timeout);
#endif
        if (result >= 0) {
            return;
        }
        if (!interrupted(socket_error())) {
            const auto code = socket_error();
            for (auto* request = first; request != nullptr; request = request->next) {
                request->completion.emplace(
                    Completion::failure(Error {.kind = ErrorKind::System, .code = code})
                );
            }
            return;
        }
    }
}

auto Network::wait(std::optional<std::int64_t> deadline) noexcept -> bool {
    if (first == nullptr) {
        return false;
    }
    wait_ready(deadline);
    return true;
}

auto WriteResult::take_bytes() noexcept -> runtime::Vector<std::uint8_t> {
    return std::move(bytes);
}

auto ReadResult::take_bytes() noexcept -> runtime::Vector<std::uint8_t> {
    return std::move(bytes);
}

auto read(const View& view, std::size_t limit) noexcept -> async::AsyncOperation<ReadResult> {
    assert(limit > 0);
    using Completion = async::Completion<ReadResult>;
    if (view.connection == nullptr) {
        co_return Completion::success_from([&]() noexcept {
            return ReadResult {.bytes = {}, .error = 2, .code = 0, .end = false};
        });
    }
    // The caller's limit is an upper bound, not a mandatory allocation size.
    const auto capacity = std::min(limit, std::size_t(64 * 1024));
    const auto buffer = std::make_unique_for_overwrite<std::uint8_t[]>(capacity);
    auto result =
        co_await view.connection->read(std::as_writable_bytes(std::span(buffer.get(), capacity)));
    if (result.is_cancelled()) {
        co_return Completion::cancelled();
    }
    if (const auto* error = result.failure_if<Error>()) {
        co_return Completion::success_from([&]() noexcept {
            return ReadResult {
                .bytes = {},
                .error = static_cast<std::uint8_t>(error->kind),
                .code = error->code,
                .end = false
            };
        });
    }
    const auto count = result.success_if()->value;
    auto bytes = runtime::Vector<std::uint8_t>::from_slice(
        runtime::Slice<std::uint8_t>(std::span<const std::uint8_t>(buffer.get(), count))
    );
    co_return Completion::success_from([&]() noexcept {
        return ReadResult {.bytes = std::move(bytes), .error = 0, .code = 0, .end = count == 0};
    });
}

auto write(const View& view, runtime::Vector<std::uint8_t> bytes, std::size_t offset) noexcept
    -> async::AsyncOperation<WriteResult> {
    using Completion = async::Completion<WriteResult>;
    if (view.connection == nullptr) {
        co_return Completion::success_from([&]() noexcept {
            return WriteResult {.bytes = std::move(bytes), .count = 0, .error = 2, .code = 0};
        });
    }
    auto result = co_await view.connection->write(
        std::as_bytes(std::span(bytes.begin(), bytes.size()).subspan(offset))
    );
    if (result.is_cancelled()) {
        co_return Completion::cancelled();
    }
    if (const auto* error = result.failure_if<Error>()) {
        co_return Completion::success_from([&]() noexcept {
            return WriteResult {
                .bytes = std::move(bytes),
                .count = 0,
                .error = static_cast<std::uint8_t>(error->kind),
                .code = error->code
            };
        });
    }
    co_return Completion::success_from([&]() noexcept {
        return WriteResult {
            .bytes = std::move(bytes),
            .count = result.success_if()->value,
            .error = 0,
            .code = 0
        };
    });
}

} // namespace carven::standard::net
