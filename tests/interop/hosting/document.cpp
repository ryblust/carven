#include <carven/hosting/api.hpp>
#include <carven/hosting/call.hpp>
#include <carven/hosting/entry.hpp>
#include <carven/hosting/method.hpp>
#include <carven/hosting/request.hpp>
#include <carven/hosting/service.hpp>
#include <carven/providers/tests/interop/hosting/document.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace services = carven::providers::tests::interop::hosting::document::cv_escaped_functions;
using Fetch = std::remove_cvref_t<decltype(services::fetch::service)>;

class Document final {
public:
    Document() noexcept
        : registration(*this) {}

    Document(const Document&) = delete;
    auto operator=(const Document&) -> Document& = delete;

    auto read() const noexcept -> std::string { return text; }

    auto publish(const std::string& value) noexcept -> void { view = value; }

    auto reference() const noexcept -> HostObjectRef { return registration.reference(); }

    auto listen(HostEvent event) noexcept -> void { changed = std::move(event); }

    auto edit(std::string value) noexcept -> std::optional<HostTask> {
        text = std::move(value);
        ++revision;
        const auto arguments = std::array {HostValue(revision)};
        return changed ? changed->post(arguments) : std::nullopt;
    }

    auto rendered() const noexcept -> std::string_view { return view; }

private:
    std::string text;
    std::string view;
    std::int32_t revision = 0;
    std::optional<HostEvent> changed;
    // First destroyed, before native fields. The object address stays stable.
    HostObject registration;
};

struct Fetcher final {
    std::vector<HostRequest<Fetch::Response>> requests;
};

auto fetch(
    void* context,
    // NOLINTNEXTLINE(performance-unnecessary-value-param): Async Start receives owned arguments by its exact callback contract.
    services::fetch::Parameter0,
    HostRequest<Fetch::Response> request
) noexcept -> void {
    // This service outlives both the document and the session's cancellation hooks.
    static_cast<Fetcher*>(context)->requests.push_back(std::move(request));
}

auto pump(HostSession& session) noexcept -> HostSessionStatus {
    auto status = HostSessionStatus::Runnable;
    for (auto turn = 0; turn < 10000 && status == HostSessionStatus::Runnable; ++turn) {
        status = session.poll(64);
    }
    return status;
}

auto succeeded(const HostTask& task) noexcept -> bool {
    const auto* result = task.result();
    return result && *result && std::holds_alternative<HostValue>(**result);
}

auto cancelled(const HostTask& task) noexcept -> bool {
    const auto* result = task.result();
    return result && *result && std::holds_alternative<HostCancelled>(**result);
}

using CounterRef = carven::runtime::hosting::ObjectRef<std::int32_t>;

auto snapshot(const CounterRef& counter, std::string text) noexcept -> std::string {
    return std::format("{}: {}", *counter.get(), text);
}

auto event_snapshot() noexcept -> bool {
    auto counter = std::int32_t {1};
    const auto owner = HostObject(counter);
    const auto native_types = std::array {host_native_type<CounterRef>("snapshot::CounterRef")};
    const auto entries = std::array {host_entry<&snapshot>("snapshot", native_types)};
    auto options = host_session_options();
    options.entries = entries;
    auto session = HostSession::create("snapshot", options);
    if (!session) {
        return false;
    }
    auto subscription = session->subscribe(owner.reference(), "snapshot");
    if (!subscription) {
        return false;
    }
    auto arguments = std::array {
        HostValue(owner.reference<std::int32_t>()),
        HostValue(HostObjectRef::own(std::string("queued")))
    };
    auto task = subscription->event().post(arguments);
    if (!task) {
        return false;
    }
    // Carrier values are detached at post; reference carriers keep their referent.
    const auto* text = std::get_if<HostObjectRef>(&arguments[1]);
    if (text == nullptr || text->get<std::string>() == nullptr) {
        return false;
    }
    *text->get<std::string>() = "edited";
    counter = 7;
    if (pump(*session) != HostSessionStatus::Idle) {
        return false;
    }
    const auto* result = task->result();
    const auto* value = result && *result ? std::get_if<HostValue>(&**result) : nullptr;
    const auto* observed = value ? std::get_if<std::string>(value) : nullptr;
    return observed && *observed == "7: queued";
}

auto source(std::string_view path) noexcept -> std::string {
    auto input = std::ifstream(std::string(path));
    return {std::istreambuf_iterator<char>(input), {}};
}

} // namespace

auto run_document() noexcept -> bool {
    if (!event_snapshot()) {
        return false;
    }
    const auto original = source("tests/interop/hosting/document.cv");
    const auto updated = source("tests/interop/hosting/document_updated.cv");
    auto fetcher = Fetcher {.requests = {}};
    auto document = std::make_unique<Document>();
    const auto reference = document->reference();
    const auto methods = std::array {
        host_method<&Document::read>(decltype(services::read::service)::name, reference),
        host_method<&Document::publish>(decltype(services::publish::service)::name, reference),
    };
    const auto async =
        std::array {host_async_service<fetch, nullptr>(services::fetch::service, &fetcher)};
    auto options = host_session_options();
    options.objects = methods;
    options.services = async;
    auto session = HostSession::load(services::fetch::source_module, original, options);
    if (!session) {
        std::println(std::cerr, "{}", session.error().message);
        return false;
    }
    auto subscription = session->subscribe(reference, "changed");
    if (!subscription || !reference.same_object(document->reference())) {
        return false;
    }
    auto event = subscription->event();
    document->listen(event);
    auto first = document->edit("A");
    if (!first
        || !document->rendered().empty()
        || session->poll(0) != HostSessionStatus::Runnable
        || pump(*session) != HostSessionStatus::Idle
        || !succeeded(*first)
        || document->rendered() != "old 1: A") {
        return false;
    }
    // Start old code, then queue an event before replacing the source.
    const auto old = session->submit("refresh", {});
    if (!old || pump(*session) != HostSessionStatus::Waiting || fetcher.requests.size() != 1) {
        return false;
    }
    auto queued = document->edit("B");
    // Reject an incompatible native declaration before publishing or running it.
    auto incompatible = updated;
    const auto declaration = std::string("fn publish(text: String)");
    const auto offset = incompatible.find(declaration);
    if (offset == std::string::npos) {
        return false;
    }
    incompatible.insert(offset + declaration.size(), " -> String");
    const auto rejected = session->reload(incompatible);
    if (rejected || rejected.error().kind != HostErrorKind::Signature) {
        return false;
    }
    const auto reload = session->reload(updated);
    if (!queued
        || !reload
        || pump(*session) != HostSessionStatus::Waiting
        || !succeeded(*queued)
        || document->rendered() != "updated 2: B"
        || !fetcher.requests[0].complete(services::fetch::Result::from_utf8("A response"))
        || pump(*session) != HostSessionStatus::Idle
        || !succeeded(*old)
        || document->rendered() != "old fetch: A response") {
        return false;
    }
    // Revocation removes already queued notifications and rejects future posts.
    auto revoked = document->edit("C");
    subscription->cancel();
    if (!revoked || !cancelled(*revoked) || event.post({}) || session->active_count() != 0) {
        return false;
    }
    auto next = session->subscribe(reference, "changed");
    if (!next) {
        return false;
    }
    event = next->event();
    document->listen(event);
    // A worker may post, but source execution still belongs to the host poll loop.
    auto threaded = std::optional<HostTask>();
    {
        const auto worker = std::jthread([event, &threaded]() noexcept {
            const auto arguments = std::array {HostValue(std::int32_t {3})};
            threaded = event.post(arguments);
        });
    }
    if (!threaded
        || threaded->result()
        || pump(*session) != HostSessionStatus::Idle
        || !succeeded(*threaded)
        || document->rendered() != "updated 3: C") {
        return false;
    }
    // Admission failures have a result, rather than silently losing notifications.
    auto invalid = event.post({});
    if (!invalid
        || pump(*session) != HostSessionStatus::Idle
        || !invalid->result()
        || *invalid->result()
        || invalid->result()->error().kind != HostErrorKind::Arguments) {
        return false;
    }
    // Closing a second session cancels queued work without retaining its code.
    auto closing = HostSession::load(services::fetch::source_module, updated, options);
    if (!closing) {
        return false;
    }
    auto closing_subscription = closing->subscribe(reference, "changed");
    if (!closing_subscription) {
        return false;
    }
    const auto closing_event = closing_subscription->event();
    const auto arguments = std::array {HostValue(std::int32_t {3})};
    auto closing_task = closing_event.post(arguments);
    closing->close();
    if (!closing_task || !cancelled(*closing_task) || closing_event.post(arguments)) {
        return false;
    }
    // Await retains owned data, not a Document*. The checked method access after
    // completion must fail safely if the host destroys the document in the meantime.
    const auto pending = session->submit("refresh", {});
    if (!pending || pump(*session) != HostSessionStatus::Waiting || fetcher.requests.size() != 2) {
        return false;
    }
    auto abandoned = document->edit("D");
    document.reset();
    if (!abandoned
        || !cancelled(*abandoned)
        || reference.alive()
        || event.post(arguments)
        || !fetcher.requests[1].complete(services::fetch::Result::from_utf8("late response"))
        || pump(*session) != HostSessionStatus::Idle) {
        return false;
    }
    const auto* result = pending->result();
    if (!result
        || *result
        || result->error().kind != HostErrorKind::Execution
        || result->error().message.find("host object is no longer alive") == std::string::npos
        || !result->error().location) {
        return false;
    }
    session->close();
    std::println("document: events -> reload -> revoke -> destroy during await; detached safely");
    return true;
}
