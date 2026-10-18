#pragma once

#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/io/cursor.hpp>
#include <carven/runtime/value/tuple.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace carven::runtime::io::detail {

using ReadResult = Tuple<Vector<std::uint8_t>, bool, std::int32_t, bool>;

// The runtime instance's input queue owns prefetch and incomplete records. Cold
// operations do not touch it. Cancellation unlinks only the request, retaining
// unread bytes.
class Queue final : public runtime::execution::EventSource {
    using Completion = runtime::execution::Completion<ReadResult>;

public:
    class Awaiter final {
        friend class Queue;
        Queue& queue;
        Request input;
        Awaiter* next = nullptr;
        std::optional<Completion> completion;
        runtime::execution::ResumeContinuation resume;

    public:
        Awaiter(Queue& owner, ReadMode kind, std::size_t maximum) noexcept
            : queue(owner),
              input {.mode = kind, .limit = maximum, .scanned = 0} {}

        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;

        ~Awaiter() noexcept {
            runtime::execution::detail::require_protocol(!resume.bound() || completion.has_value());
        }

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            resume.bind(handle);
            if (resume.cancel_requested()) {
                completion.emplace(Completion::cancelled());
                return false;
            }
            queue.insert(*this);
            return true;
        }

        auto await_resume() noexcept -> Completion { return std::move(*completion); }
    };

private:
    Awaiter* first = nullptr;
    Awaiter* last = nullptr;

    auto insert(Awaiter& request) noexcept -> void {
        auto& driver = request.resume.driver();
        if (first == nullptr) {
            driver.attach(*this);
        }
        if (last != nullptr) {
            last->next = &request;
        } else {
            first = &request;
        }
        last = &request;
    }

    auto finish(
        Awaiter& request,
        Awaiter* previous,
        Completion result,
        runtime::execution::Driver& driver
    ) noexcept -> void {
        if (previous != nullptr) {
            previous->next = request.next;
        } else {
            first = request.next;
        }
        if (last == &request) {
            last = previous;
        }
        request.next = nullptr;
        request.completion.emplace(std::move(result));
        if (first == nullptr) {
            driver.detach(*this);
        }
        request.resume.enqueue(driver);
    }

    auto ready(Awaiter& request) noexcept -> std::optional<Completion> {
        auto& cursor = Cursor::process();
        auto response = cursor.ready(request.input);
        if (!response) {
            return std::nullopt;
        }
        if (const auto* error = std::get_if<ReadError>(&*response)) {
            return Completion::success_from([&]() noexcept -> ReadResult {
                return {
                    .head = {},
                    .tail = {
                        .head = false,
                        .tail = {.head = error->code, .tail = {.head = error->limited, .tail = {}}}
                    }
                };
            });
        }
        const auto packet = std::get<ByteView>(*response);
        auto result = Completion::success_from([&]() noexcept {
            auto bytes = Vector<std::uint8_t>::from_slice(Slice<std::uint8_t>(packet.bytes));
            return ReadResult {
                .head = transfer(bytes),
                .tail =
                    {.head = packet.end, .tail = {.head = 0, .tail = {.head = false, .tail = {}}}}
            };
        });
        cursor.consume(packet.bytes.size());
        return result;
    }

    auto poll(runtime::execution::Driver& driver) noexcept -> void override {
        auto* previous = static_cast<Awaiter*>(nullptr);
        auto* request = first;
        while (request != nullptr) {
            auto* next = request->next;
            if (request->resume.cancel_requested()) {
                finish(*request, previous, Completion::cancelled(), driver);
            } else {
                previous = request;
            }
            request = next;
        }
        if (first == nullptr) {
            return;
        }
        auto result = ready(*first);
        if (!result) {
            Cursor::process().fill();
            if (auto filled = ready(*first)) {
                result.emplace(std::move(*filled));
            }
        }
        if (result) {
            finish(*first, nullptr, std::move(*result), driver);
        }
    }

    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override {
        if (first == nullptr) {
            return false;
        }
        Cursor::process().wait(deadline);
        return true;
    }

public:
    static auto process() noexcept -> Queue& {
        static auto queue = Queue();
        return queue;
    }

    auto read(ReadMode mode, std::size_t limit) noexcept -> Awaiter {
        return Awaiter(*this, mode, limit);
    }
};

} // namespace carven::runtime::io::detail
