#pragma once

#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/execution/time.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace collector {

namespace async = carven::runtime::execution;

#if defined(_WIN32)
using Socket = SOCKET;
inline constexpr auto invalid_socket = INVALID_SOCKET;

inline auto socket_error() noexcept -> int {
    return WSAGetLastError();
}

inline auto interrupted(int code) noexcept -> bool {
    return code == WSAEINTR;
}

inline auto would_block(int code) noexcept -> bool {
    return code == WSAEWOULDBLOCK;
}

inline auto close_socket(Socket socket) noexcept -> void {
    ::closesocket(socket);
}
#else
using Socket = int;
inline constexpr auto invalid_socket = -1;

inline auto socket_error() noexcept -> int {
    return errno;
}

inline auto interrupted(int code) noexcept -> bool {
    return code == EINTR;
}

inline auto would_block(int code) noexcept -> bool {
    return code == EAGAIN || code == EWOULDBLOCK;
}

inline auto close_socket(Socket socket) noexcept -> void {
    ::close(socket);
}
#endif

struct SocketError final {
    int code;
};

// One event loop owns readiness registrations. Socket and buffer owners stay
// alive until completion, including timeout-driven cancellation and closure.
class Network final : public async::EventSource {
public:
    using Completion = async::Completion<std::size_t, SocketError>;

    class Awaiter final {
        friend class Network;
        Network& network;
        Socket socket;
        const std::byte* data;
        std::size_t size;
        bool writing;
        Awaiter* next = nullptr;
        std::optional<Completion> completion;
        async::ResumeContinuation continuation;

        Awaiter(Network& owner, Socket fd, std::span<const std::byte> buffer, bool write) noexcept
            : network(owner),
              socket(fd),
              data(buffer.data()),
              size(buffer.size()),
              writing(write) {
            assert(!buffer.empty());
        }

        auto attempt() const noexcept -> std::optional<Completion> {
            for (;;) {
                const auto count = writing
                    ? ::send(socket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0)
                    : ::recv(
                          socket,
                          reinterpret_cast<char*>(const_cast<std::byte*>(data)),
                          static_cast<int>(size),
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
                return Completion::failure(SocketError {.code = code});
            }
        }

    public:
        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;
        auto operator=(const Awaiter&) -> Awaiter& = delete;
        auto operator=(Awaiter&&) -> Awaiter& = delete;

        ~Awaiter() noexcept { assert(!continuation.bound() || completion.has_value()); }

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            continuation.bind(handle);
            if (continuation.cancel_requested()) {
                completion.emplace(Completion::cancelled());
                return false;
            }
            auto ready = attempt();
            if (ready) {
                completion.emplace(std::move(*ready));
                return false;
            }
            next = network.first;
            network.first = this;
            return true;
        }

        auto await_resume() noexcept -> Completion {
            assert(completion.has_value());
            return std::move(*completion);
        }
    };

private:
    Awaiter* first = nullptr;

    auto select_ready(fd_set& reads, fd_set& writes, std::optional<std::int64_t> deadline) noexcept
        -> void {
        for (;;) {
            FD_ZERO(&reads);
            FD_ZERO(&writes);
            auto maximum = Socket(0);
            for (auto* request = first; request != nullptr; request = request->next) {
                FD_SET(request->socket, request->writing ? &writes : &reads);
                if (request->socket > maximum) {
                    maximum = request->socket;
                }
            }
            auto timeout = timeval {};
            if (deadline) {
                const auto now = async::now();
                const auto remaining = *deadline > now ? *deadline - now : 0;
                const auto microseconds = remaining / 1000 + (remaining % 1000 != 0);
                timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(microseconds / 1000000);
                timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(microseconds % 1000000);
            }
            if (first == nullptr) {
                if (deadline && *deadline > async::now()) {
                    std::this_thread::sleep_for(std::chrono::nanoseconds(*deadline - async::now()));
                }
                return;
            }
            const auto ready = ::select(
                static_cast<int>(maximum + 1),
                &reads,
                &writes,
                nullptr,
                deadline ? &timeout : nullptr
            );
            if (ready >= 0) {
                return;
            }
            if (!interrupted(socket_error())) {
                std::terminate();
            }
        }
    }

public:
    Network() noexcept = default;
    Network(const Network&) = delete;
    auto operator=(const Network&) -> Network& = delete;

    ~Network() noexcept override { assert(first == nullptr); }

    auto read(Socket socket, std::span<std::byte> buffer) noexcept -> Awaiter {
        return Awaiter(*this, socket, buffer, false);
    }

    auto write(Socket socket, std::span<const std::byte> buffer) noexcept -> Awaiter {
        return Awaiter(*this, socket, buffer, true);
    }

    auto poll(async::Driver& driver) noexcept -> void override {
        auto reads = fd_set {};
        auto writes = fd_set {};
        select_ready(reads, writes, async::now());
        auto** link = &first;
        while (*link != nullptr) {
            auto* request = *link;
            auto ready = [&]() noexcept -> std::optional<Completion> {
                if (request->continuation.cancel_requested()) {
                    return Completion::cancelled();
                }
                if (FD_ISSET(request->socket, request->writing ? &writes : &reads)) {
                    return request->attempt();
                }
                return std::nullopt;
            }();
            if (!ready) {
                link = &request->next;
                continue;
            }
            *link = request->next;
            request->next = nullptr;
            request->completion.emplace(std::move(*ready));
            request->continuation.enqueue(driver);
        }
    }

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override {
        if (first == nullptr) {
            return false;
        }
        auto reads = fd_set {};
        auto writes = fd_set {};
        select_ready(reads, writes, deadline);
        return true;
    }
};

class Connection final {
    Socket reader = invalid_socket;
    Socket writer = invalid_socket;

public:
    Connection();
    Connection(const Connection&) = delete;
    auto operator=(const Connection&) -> Connection& = delete;

    ~Connection() noexcept {
        if (reader != invalid_socket) {
            close_socket(reader);
        }
        if (writer != invalid_socket) {
            close_socket(writer);
        }
    }

    auto receiving_socket() const noexcept -> Socket { return reader; }

    auto sending_socket() const noexcept -> Socket { return writer; }
};

struct Context final {
    Network& network;
    const Connection& connection;
    std::string payload;
};

} // namespace collector
