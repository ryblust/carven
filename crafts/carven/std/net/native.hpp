#pragma once

#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

namespace carven::standard::net {

namespace async = runtime::execution;

#if defined(_WIN32)
using Socket = SOCKET;
inline constexpr auto invalid_socket = INVALID_SOCKET;
#else
using Socket = int;
inline constexpr auto invalid_socket = -1;
#endif

auto socket_error() noexcept -> int;
auto close_socket(Socket socket) noexcept -> void;

// Codes are independent of platform errno values.
enum class ErrorKind : std::uint8_t { None, System, Closed, Conflict };

struct Error final {
    ErrorKind kind;
    std::int32_t code;
};

class Connection;

class Network final : public async::EventSource {
public:
    using Completion = async::Completion<std::size_t, Error>;

    class Awaiter final {
        friend class Network;
        friend class Connection;
        const Connection& connection;
        std::span<const std::byte> buffer;
        bool writing;
        Awaiter* next = nullptr;
        std::optional<Completion> completion;
        async::ResumeContinuation continuation;

        auto attempt() const noexcept -> std::optional<Completion>;
        auto suspend() noexcept -> bool;
        auto finish(Completion result) noexcept -> void;
        Awaiter(
            const Connection& connection,
            std::span<const std::byte> buffer,
            bool writing
        ) noexcept;

    public:
        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;
        auto operator=(const Awaiter&) -> Awaiter& = delete;
        auto operator=(Awaiter&&) -> Awaiter& = delete;
        ~Awaiter() noexcept;
        auto await_ready() const noexcept -> bool;

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            continuation.bind(handle);
            return suspend();
        }

        auto await_resume() noexcept -> Completion;
    };

    Network() noexcept;
    Network(const Network&) = delete;
    auto operator=(const Network&) -> Network& = delete;
    ~Network() noexcept override;
    auto startup_error() const noexcept -> int;
    auto poll(async::Driver& driver) noexcept -> void override;
    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override;

private:
    Awaiter* first = nullptr;
    int initialization_error = 0;
    auto wait_ready(std::optional<std::int64_t> deadline) noexcept -> void;
};

// Host-owned, address-stable resource. Finish all operations before destruction.
class Connection final {
    friend class Network;
    friend class Network::Awaiter;
    Network& network;
    mutable Socket socket;
    mutable bool reading = false;
    mutable bool writing = false;
    int initialization_error = 0;

public:
    Connection(Network& network, Socket owned_socket) noexcept;
    Connection(const Connection&) = delete;
    Connection(Connection&&) = delete;
    auto operator=(const Connection&) -> Connection& = delete;
    auto operator=(Connection&&) -> Connection& = delete;
    ~Connection() noexcept;
    auto startup_error() const noexcept -> int;
    auto native_socket() const noexcept -> Socket;
    auto close() const noexcept -> Error;
    auto shutdown_send() const noexcept -> Error;
    auto read(std::span<std::byte> buffer) const noexcept -> Network::Awaiter;
    auto write(std::span<const std::byte> buffer) const noexcept -> Network::Awaiter;
};

struct View final {
    const Connection* connection;
};

struct ReadResult final {
    runtime::Vector<std::uint8_t> bytes;
    std::uint8_t error;
    std::int32_t code;
    bool end;
    auto take_bytes() noexcept -> runtime::Vector<std::uint8_t>;
};

struct WriteResult final {
    runtime::Vector<std::uint8_t> bytes;
    std::size_t count;
    std::uint8_t error;
    std::int32_t code;
    auto take_bytes() noexcept -> runtime::Vector<std::uint8_t>;
};

auto read(const View& view, std::size_t limit) noexcept -> async::AsyncOperation<ReadResult>;
auto write(const View& view, runtime::Vector<std::uint8_t> bytes, std::size_t offset) noexcept
    -> async::AsyncOperation<WriteResult>;

} // namespace carven::standard::net
