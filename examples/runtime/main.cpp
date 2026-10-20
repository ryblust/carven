#include <carven/api/examples/runtime/preview.hpp>
#include <carven/hosting/api.hpp>
#include <carven/hosting/entry.hpp>
#include <carven/hosting/request.hpp>
#include <carven/hosting/service.hpp>
#include <carven/providers/examples/runtime/preview.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace compiled = carven::api::examples::runtime::preview;
namespace query = carven::providers::examples::runtime::preview::cv_escaped_functions::query;
using Query = std::remove_cvref_t<decltype(query::service)>;

// A real application hands these requests to its I/O provider. This demo replies
// explicitly so the new preview can finish before the old one, without timing races.
struct Host final {
    std::vector<HostRequest<Query::Response>> requests;
};

// NOLINTNEXTLINE(performance-unnecessary-value-param): Async Start receives owned arguments by its exact callback contract.
auto start(void* context, query::Parameter0 text, HostRequest<Query::Response> request) noexcept
    -> void {
    std::println("query: {}", text.as_str());
    static_cast<Host*>(context)->requests.push_back(std::move(request));
}

auto cancel(void*, std::uint64_t) noexcept -> void {
    std::println("cancelled pending query");
}

struct Revision final {
    std::size_t number;
    HostTask task;
};

struct Editor final {
    std::string view;
    std::size_t revision = 0;
    std::vector<Revision> pending;

    auto edit(const HostEvent& event, std::string text) noexcept -> bool {
        const auto arguments = std::array {HostValue(std::move(text))};
        auto task = event.post(arguments);
        if (!task) {
            return false;
        }
        pending.push_back({.number = ++revision, .task = std::move(*task)});
        return true;
    }

    auto collect() noexcept -> bool {
        for (auto item = pending.begin(); item != pending.end();) {
            const auto* result = item->task.result();
            if (!result) {
                ++item;
                continue;
            }
            if (!*result) {
                std::println(std::cerr, "{}", result->error().message);
                return false;
            }
            if (std::holds_alternative<HostCancelled>(**result)) {
                item = pending.erase(item);
                continue;
            }
            const auto* value = std::get_if<HostValue>(&**result);
            const auto* text = value ? std::get_if<std::string>(value) : nullptr;
            if (!text) {
                std::println(std::cerr, "preview did not return text");
                return false;
            }
            std::println("revision {}: {}", item->number, *text);
            // Finishing an older call is valid. Publishing it is the host's choice.
            if (item->number == revision) {
                view = *text;
            }
            item = pending.erase(item);
        }
        return true;
    }
};

auto pump(HostSession& session) noexcept -> HostSessionStatus {
    // Each poll is a bounded event-loop slice. Waiting returns control to the host.
    for (auto turn = 0; turn < 256; ++turn) {
        const auto status = session.poll(32);
        if (status != HostSessionStatus::Runnable) {
            return status;
        }
    }
    return HostSessionStatus::Runnable;
}

auto failed(std::string_view message) noexcept -> int {
    std::println(std::cerr, "{}", message);
    return 1;
}

} // namespace

auto main() -> int {
    auto input = std::ifstream("examples/runtime/preview_updated.cv");
    if (!input) {
        return failed("run from the repository root to find preview_updated.cv");
    }
    const auto updated = std::string(std::istreambuf_iterator<char>(input), {});

    // Provider state outlives the session and its cancellation callbacks.
    auto host = Host {};
    auto editor = Editor {};
    const auto services = std::array {host_async_service<start, cancel>(query::service, &host)};
    const auto entries = std::array {
        host_entry<compiled::preview>(compiled::cv_escaped_functions::preview::service),
    };
    auto options = host_session_options();
    options.services = services;
    options.entries = entries;
    auto session = HostSession::create(query::source_module, options);
    if (!session) {
        return failed(session.error().message);
    }
    const auto owner = HostObject(editor);
    auto subscription = session->subscribe(owner.reference(), "preview");
    if (!subscription) {
        return failed(subscription.error().message);
    }
    const auto event = subscription->event();

    // Start the compiled implementation and leave its provider request pending.
    if (!editor.edit(event, "draft")
        || pump(*session) != HostSessionStatus::Waiting
        || host.requests.size() != 1) {
        return failed("compiled preview did not reach its query");
    }

    // The same session, provider, editor, and event endpoint survive replacement.
    const auto reload = session->reload(updated);
    if (!reload) {
        return failed(reload.error().message);
    }
    if (!editor.edit(event, "revised draft")
        || pump(*session) != HostSessionStatus::Waiting
        || host.requests.size() != 2) {
        return failed("updated preview did not reach its query");
    }

    // New source finishes first. complete() delivers data; poll() resumes execution.
    if (!host.requests[1].complete(query::Result::from_utf8("revised response"))
        || pump(*session) != HostSessionStatus::Waiting
        || !editor.collect()) {
        return failed("updated preview did not complete");
    }
    std::println("view: {}", editor.view);

    // An admitted call keeps its compiled implementation even after reload.
    if (!host.requests[0].complete(query::Result::from_utf8("original response"))
        || pump(*session) != HostSessionStatus::Idle
        || !editor.collect()) {
        return failed("original preview did not complete");
    }
    std::println("view after older reply: {}", editor.view);

    // Closing the editor revokes new events and cancels work already admitted.
    if (!editor.edit(event, "closing draft") || pump(*session) != HostSessionStatus::Waiting) {
        return failed("closing preview did not reach its query");
    }
    subscription->cancel();
    for (const auto& item : editor.pending) {
        item.task.cancel();
    }
    if (pump(*session) != HostSessionStatus::Idle || !editor.collect()) {
        return failed("preview cancellation did not finish");
    }
    session->close();
    std::println("editor closed");
    return 0;
}
