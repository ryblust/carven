module;

#include <carven/hosting/api.hpp>

module carven:hosting.event;

import std;

struct HostEventQueue;

struct HostQueuedEvent final {
    std::weak_ptr<HostEventState> subscription;
    std::vector<HostValue> arguments;
    std::shared_ptr<HostTaskState> completion;
};

extern "C++" {

struct HostEventState final {
    std::weak_ptr<HostEventQueue> queue;
    HostObjectRef owner;
    std::string entry;
    // Guarded by the mailbox mutex. Lifecycle writes use the session thread.
    bool active;
    auto cancel() noexcept -> void;
};

} // extern "C++"

// Posting handles never retain the session, executable programs, or invocations.
struct HostEventQueue final {
    std::mutex mutex;
    bool accepting;
    std::deque<HostQueuedEvent> pending;
    HostEventQueue() noexcept;
    auto take() noexcept -> std::optional<HostQueuedEvent>;
    auto size() noexcept -> std::size_t;
    auto close() noexcept -> void;
};
