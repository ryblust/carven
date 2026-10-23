#pragma once

#include <carven/hosting/api.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

// Heterogeneous requests share cancellation, never an erased result payload.
struct HostRequestState {
    enum class Phase { Pending, Ready, Consumed, Cancelled };
    std::mutex mutex;
    std::uint64_t id;
    Phase phase;
    void* context;
    void (*cancel_hook)(void*, std::uint64_t) noexcept;
    HostRequestState(void* context) noexcept;
    virtual ~HostRequestState() noexcept = default;
    auto cancel() noexcept -> void;
    auto attached() noexcept -> bool;

private:
    virtual auto discard() noexcept -> void = 0;
};

auto hosting_request_id(const std::shared_ptr<HostRequestState>& state) noexcept -> std::uint64_t;

auto hosting_request_active(const std::shared_ptr<HostRequestState>& state) noexcept -> bool;

// Completion keeps its concrete result type. Only cancellation registration is erased.
template<typename Response>
struct HostRequestValue final : HostRequestState {
    explicit HostRequestValue(void* context) noexcept
        : HostRequestState(context),
          response(std::nullopt) {}

    auto respond(std::expected<Response, HostError> value) noexcept -> bool {
        const auto lock = std::lock_guard(mutex);
        if (phase != Phase::Pending) {
            return false;
        }
        response.emplace(std::move(value));
        phase = Phase::Ready;
        return true;
    }

    auto consume() noexcept -> std::optional<std::expected<Response, HostError>> {
        const auto lock = std::lock_guard(mutex);
        if (phase != Phase::Ready) {
            return std::nullopt;
        }
        phase = Phase::Consumed;
        context = nullptr;
        cancel_hook = nullptr;
        return std::exchange(response, std::nullopt);
    }

private:
    std::optional<std::expected<Response, HostError>> response;

    auto discard() noexcept -> void override { response.reset(); }
};

// One serialized consumer detaches the mailbox on destruction.
template<typename Response>
class HostPendingRequest final {
public:
    explicit HostPendingRequest(std::shared_ptr<HostRequestValue<Response>> state) noexcept
        : state(std::move(state)) {}

    HostPendingRequest(const HostPendingRequest&) = delete;
    HostPendingRequest(HostPendingRequest&&) noexcept = default;
    auto operator=(const HostPendingRequest&) -> HostPendingRequest& = delete;
    auto operator=(HostPendingRequest&&) -> HostPendingRequest& = delete;

    ~HostPendingRequest() noexcept { cancel(); }

    auto consume() noexcept -> std::optional<std::expected<Response, HostError>> {
        return state ? state->consume() : std::nullopt;
    }

    auto cancel() noexcept -> void {
        if (state) {
            state->cancel();
        }
    }

private:
    std::shared_ptr<HostRequestValue<Response>> state;
};

// A copyable, thread-safe endpoint. Posting never resumes execution.
template<typename Response>
class HostRequest final {
public:
    auto complete(Response response) const noexcept -> bool {
        return state && state->respond(std::move(response));
    }

    auto fail(HostError error) const noexcept -> bool {
        return state && state->respond(std::unexpected(std::move(error)));
    }

    auto id() const noexcept -> std::uint64_t { return hosting_request_id(state); }

    auto active() const noexcept -> bool { return hosting_request_active(state); }

private:
    explicit HostRequest(std::shared_ptr<HostRequestValue<Response>> state) noexcept
        : state(std::move(state)) {}

    std::shared_ptr<HostRequestValue<Response>> state;
    friend class HostCallContext;

    template<typename Contract, typename Signature>
    friend struct HostServiceAdapter;
};
