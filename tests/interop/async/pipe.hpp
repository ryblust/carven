#pragma once

#if defined(__unix__) || defined(__APPLE__)

#include <carven/runtime/async/async.hpp>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <exception>
#include <optional>
#include <poll.h>
#include <span>
#include <unistd.h>
#include <utility>
#include <vector>

namespace async_interop {

struct PipeError final {
    int code;
};

// Single-thread POSIX readiness fixture; it owns no fd or borrowed buffer.
class PipeQueue final {
public:
    using Completion = carven::runtime::async::Completion<std::size_t, PipeError>;

    class Awaiter final {
        friend class PipeQueue;
        enum class Direction { Read, Write };

        PipeQueue& queue;
        int fd;
        const std::byte* data;
        std::size_t size;
        Direction direction;
        Awaiter* next = nullptr;
        std::optional<Completion> completion;
        carven::runtime::async::ResumeContinuation resume;

        Awaiter(
            PipeQueue& provider,
            int descriptor,
            std::span<const std::byte> buffer,
            Direction operation
        ) noexcept
            : queue(provider),
              fd(descriptor),
              data(buffer.data()),
              size(buffer.size()),
              direction(operation) {
            assert(!buffer.empty());
        }

        auto events() const noexcept -> short {
            return static_cast<short>(direction == Direction::Read ? POLLIN : POLLOUT);
        }

        auto attempt() const noexcept -> std::optional<Completion> {
            for (;;) {
                // Only read_some's mutable span can construct a Read request.
                const auto count = direction == Direction::Read
                    ? ::read(fd, const_cast<std::byte*>(data), size)
                    : ::write(fd, data, size);
                if (count >= 0) {
                    return Completion::success(static_cast<std::size_t>(count));
                }
                const auto error = errno;
                if (error == EINTR) {
                    continue;
                }
                if (error == EAGAIN || error == EWOULDBLOCK) {
                    return std::nullopt;
                }
                return Completion::failure(PipeError {.code = error});
            }
        }

    public:
        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;
        auto operator=(const Awaiter&) -> Awaiter& = delete;
        auto operator=(Awaiter&&) -> Awaiter& = delete;

        ~Awaiter() noexcept { assert(!resume.bound() || completion.has_value()); }

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            resume.bind(handle);
            if (resume.cancellation_requested()) {
                completion.emplace(Completion::cancelled());
                return false;
            }
            auto ready = attempt();
            if (ready) {
                completion.emplace(std::move(*ready));
                return false;
            }
            queue.register_request(*this);
            return true;
        }

        auto await_resume() noexcept -> Completion {
            assert(completion.has_value());
            return std::move(*completion);
        }
    };

private:
    Awaiter* first = nullptr;
    Awaiter* last = nullptr;
    std::size_t registrations = 0;
    std::size_t completions = 0;
    std::size_t waits = 0;

    auto register_request(Awaiter& request) noexcept -> void {
        if (last != nullptr) {
            last->next = &request;
        } else {
            first = &request;
        }
        last = &request;
        ++registrations;
    }

    auto snapshot() const noexcept -> std::vector<::pollfd> {
        auto descriptors = std::vector<::pollfd>();
        for (auto* request = first; request != nullptr; request = request->next) {
            descriptors.push_back(
                ::pollfd {.fd = request->fd, .events = request->events(), .revents = 0}
            );
        }
        return descriptors;
    }

    static auto poll_descriptors(std::vector<::pollfd>& descriptors, int timeout) noexcept -> void {
        for (;;) {
            if (::poll(descriptors.data(), static_cast<::nfds_t>(descriptors.size()), timeout)
                >= 0) {
                return;
            }
            if (errno != EINTR) {
                std::terminate();
            }
        }
    }

public:
    PipeQueue() noexcept = default;
    PipeQueue(const PipeQueue&) = delete;
    PipeQueue(PipeQueue&&) = delete;
    auto operator=(const PipeQueue&) -> PipeQueue& = delete;
    auto operator=(PipeQueue&&) -> PipeQueue& = delete;

    ~PipeQueue() noexcept { assert(first == nullptr); }

    // fd must be nonblocking and remain open without reuse; the nonempty buffer
    // stays valid and stable until terminal delivery or lexical close completes.
    // A read result of zero is EOF. Writes keep the process's SIGPIPE policy.
    auto read_some(int fd, std::span<std::byte> buffer) noexcept -> Awaiter {
        return Awaiter(*this, fd, buffer, Awaiter::Direction::Read);
    }

    auto write_some(int fd, std::span<const std::byte> buffer) noexcept -> Awaiter {
        return Awaiter(*this, fd, buffer, Awaiter::Direction::Write);
    }

    auto poll(carven::runtime::async::Driver& driver) noexcept -> void {
        if (first == nullptr) {
            return;
        }
        auto descriptors = snapshot();
        poll_descriptors(descriptors, 0);
        auto index = std::size_t(0);
        auto** link = &first;
        auto* previous = static_cast<Awaiter*>(nullptr);
        while (*link != nullptr) {
            auto* request = *link;
            auto ready = [&]() noexcept -> std::optional<Completion> {
                if (request->resume.cancellation_requested()) {
                    return Completion::cancelled();
                }
                if (descriptors[index].revents != 0) {
                    // Readiness, HUP and errors authorize a syscall; only its
                    // result selects bytes, EOF or errno. EAGAIN stays registered.
                    return request->attempt();
                }
                return std::nullopt;
            }();
            ++index;
            if (!ready) {
                previous = request;
                link = &request->next;
                continue;
            }
            *link = request->next;
            if (last == request) {
                last = previous;
            }
            request->next = nullptr;
            request->completion.emplace(std::move(*ready));
            ++completions;
            request->resume.enqueue(driver);
        }
    }

    auto wait() noexcept -> bool {
        if (first == nullptr) {
            return false;
        }
        auto descriptors = snapshot();
        ++waits;
        poll_descriptors(descriptors, -1);
        return true;
    }

    auto pending_count() const noexcept -> std::size_t {
        auto count = std::size_t(0);
        for (auto* request = first; request != nullptr; request = request->next) {
            ++count;
        }
        return count;
    }

    auto registration_count() const noexcept -> std::size_t { return registrations; }

    auto completion_count() const noexcept -> std::size_t { return completions; }

    auto wait_count() const noexcept -> std::size_t { return waits; }
};

} // namespace async_interop

#endif
