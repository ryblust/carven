#include <carven/api/tests/interop/hosting/preview.hpp>
#include <carven/hosting/api.hpp>
#include <carven/hosting/entry.hpp>
#include <carven/hosting/request.hpp>
#include <carven/hosting/service.hpp>
#include <carven/providers/tests/interop/hosting/preview.hpp>
#include "document_host.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace compiled = carven::api::tests::interop::hosting::preview;
namespace query = carven::providers::tests::interop::hosting::preview::cv_escaped_functions::query;
using Query = std::remove_cvref_t<decltype(query::service)>;

auto query_success(std::string text) noexcept -> Query::Response {
    return Query::Response::success_from([&]() noexcept {
        return query::Result::from_utf8(std::move(text));
    });
}

auto failed(std::source_location location = std::source_location::current()) noexcept -> int {
    std::println(std::cerr, "preview check failed at line {}", location.line());
    return 1;
}

struct Pending final {
    query::Parameter0 query;
    HostRequest<Query::Response> request;
};

struct Host final {
    std::vector<Pending> requests;
    std::vector<std::uint64_t> cancelled;
};

auto start(void* context, query::Parameter0 query, HostRequest<Query::Response> request) noexcept
    -> void {
    auto& host = *static_cast<Host*>(context);
    host.requests.push_back({.query = std::move(query), .request = std::move(request)});
}

auto cancel(void* context, std::uint64_t id) noexcept -> void {
    static_cast<Host*>(context)->cancelled.push_back(id);
}

// The application owns selection and publication. Execution may finish an old
// workflow normally; only the selected task can update this document's view.
class DocumentPreview final {
public:
    DocumentPreview(
        std::unique_ptr<documents::Document> document,
        HostSubscription subscription
    ) noexcept;

    ~DocumentPreview() noexcept;
    auto edit(std::string text) noexcept -> std::optional<HostTask>;
    auto collect() noexcept -> void;
    auto rendered() const noexcept -> std::string_view;
    auto close() noexcept -> void;

private:
    std::unique_ptr<documents::Document> document;
    HostSubscription subscription;
    std::list<HostTask> pending;
    std::list<HostTask>::iterator selected;
};

DocumentPreview::DocumentPreview(
    std::unique_ptr<documents::Document> document,
    HostSubscription subscription
) noexcept
    : document(std::move(document)),
      subscription(std::move(subscription)),
      pending(),
      selected(pending.end()) {}

DocumentPreview::~DocumentPreview() noexcept {
    close();
}

auto DocumentPreview::edit(std::string text) noexcept -> std::optional<HostTask> {
    if (!document) {
        return std::nullopt;
    }
    selected = pending.end();
    document->edit(std::move(text));
    const auto arguments = std::array {HostValue(document->read())};
    auto task = subscription.event().post(arguments);
    if (task) {
        pending.push_back(*task);
        selected = std::prev(pending.end());
    }
    return task;
}

auto DocumentPreview::collect() noexcept -> void {
    for (auto task = pending.begin(); task != pending.end();) {
        const auto* result = task->result();
        if (!result) {
            ++task;
            continue;
        }
        if (task == selected) {
            selected = pending.end();
            const auto* value = *result ? std::get_if<HostValue>(&**result) : nullptr;
            const auto* text = value ? std::get_if<std::string>(value) : nullptr;
            if (document && text) {
                document->publish(*text);
            }
        }
        task = pending.erase(task);
    }
}

auto DocumentPreview::rendered() const noexcept -> std::string_view {
    return document ? document->rendered() : std::string_view();
}

auto DocumentPreview::close() noexcept -> void {
    subscription.cancel();
    for (const auto& task : pending) {
        task.cancel();
    }
    selected = pending.end();
    pending.clear();
    document.reset();
}

auto pump(HostSession& session) noexcept -> HostSessionStatus {
    // One small event-loop slice at a time; no interpreter-owned blocking wait.
    // At most 8192 call slices: a stopped child group must drain without host replies.
    for (auto turn = 0; turn < 256; ++turn) {
        const auto status = session.poll(32);
        if (status != HostSessionStatus::Runnable) {
            return status;
        }
    }
    return HostSessionStatus::Runnable;
}

auto cancelled(const HostTask& task) noexcept -> bool {
    const auto* result = task.result();
    return result && *result && std::holds_alternative<HostCancelled>(**result);
}

auto text_result(const HostTask& task, std::string_view expected) noexcept -> bool {
    const auto* result = task.result();
    if (!result || !*result) {
        if (result) {
            std::println(std::cerr, "{}", result->error().message);
        }
        return false;
    }
    const auto* value = std::get_if<HostValue>(&**result);
    const auto* text = value ? std::get_if<std::string>(value) : nullptr;
    return text && *text == expected;
}

} // namespace

auto main() -> int {
    auto updated_file = std::ifstream("tests/interop/hosting/preview_updated.cv");
    if (!updated_file) {
        return failed();
    }
    const auto updated_source = std::string(std::istreambuf_iterator<char>(updated_file), {});
    auto host = Host {.requests = {}, .cancelled = {}};
    const auto services = std::array {host_async_service<start, cancel>(query::service, &host)};
    auto options = host_session_options();
    options.services = services;
    // Leave room for the two formatted requests while exercising the session's
    // nondefault host-response limit on both execution paths below.
    options.calls.limits.byte_work = 256;
    const auto entries = std::array {
        host_entry<compiled::preview>(compiled::cv_escaped_functions::preview::service),
        host_entry<compiled::preview_checked>(
            compiled::cv_escaped_functions::preview_checked::service
        ),
        host_entry<compiled::preview_child>(compiled::cv_escaped_functions::preview_child::service),
        host_entry<compiled::preview_candidates>(
            compiled::cv_escaped_functions::preview_candidates::service
        ),
    };
    options.entries = entries;
    auto retained = std::optional<HostTask>();
    {
        auto session = HostSession::create(query::source_module, options);
        if (!session) {
            std::println(std::cerr, "{}", session.error().message);
            return failed();
        }
        const auto wrong_arguments = std::array {HostValue(true)};
        const auto invalid_text = std::array {HostValue(std::string("\xff"))};
        const auto missing = session->submit("preview", {});
        const auto wrong = session->submit("preview", wrong_arguments);
        const auto invalid_argument = session->submit("preview", invalid_text);
        if (missing
            || wrong
            || invalid_argument
            || missing.error().kind != HostErrorKind::Arguments
            || wrong.error().kind != HostErrorKind::Arguments
            || invalid_argument.error().kind != HostErrorKind::Arguments
            || !host.requests.empty()) {
            return failed();
        }
        const auto unstarted_arguments = std::array {HostValue(std::string("not started"))};
        const auto unstarted = session->submit("preview", unstarted_arguments);
        if (!unstarted) {
            return failed();
        }
        unstarted->cancel();
        if (pump(*session) != HostSessionStatus::Idle
            || !cancelled(*unstarted)
            || !host.requests.empty()
            || !host.cancelled.empty()) {
            return failed();
        }
        auto document = std::make_unique<documents::Document>("old input");
        const auto owner = document->reference();
        auto subscription = session->subscribe(owner, "preview");
        if (!subscription) {
            return failed();
        }
        const auto endpoint = subscription->event();
        auto editor = DocumentPreview(std::move(document), std::move(*subscription));
        const auto old_arguments = std::array {HostValue(std::string("old input"))};
        const auto new_arguments = std::array {HostValue(std::string("new input"))};
        const auto old = editor.edit("old input");
        if (!old || pump(*session) != HostSessionStatus::Waiting || host.requests.size() != 1) {
            return failed();
        }
        const auto superseded = session->submit("preview", new_arguments);
        const auto limited_native = session->submit("preview_candidates", old_arguments);
        if (!superseded
            || !limited_native
            || pump(*session) != HostSessionStatus::Waiting
            || host.requests.size() != 4) {
            return failed();
        }
        // A failed source update leaves pending calls and current code intact.
        const auto invalid = session->reload("fn preview(");
        if (invalid
            || invalid.error().kind != HostErrorKind::Analysis
            || session->active_count() != 3) {
            return failed();
        }
        const auto wrong_mode =
            session->reload(R"(fn preview(&&text: String) -> String { return "sync"; })");
        if (wrong_mode
            || wrong_mode.error().kind != HostErrorKind::Signature
            || session->active_count() != 3) {
            return failed();
        }
        const auto reload = session->reload(updated_source);
        if (!reload) {
            std::println(std::cerr, "{}", reload.error().message);
            return failed();
        }
        const auto latest = editor.edit("new input");
        if (!latest) {
            return failed();
        }
        superseded->cancel();
        if (pump(*session) != HostSessionStatus::Waiting
            || !cancelled(*superseded)
            || host.requests.size() != 5
            || host.cancelled.size() != 1
            || host.cancelled[0] != host.requests[1].request.id()
            || host.requests[1].request.complete(query_success(std::string("stale response")))) {
            return failed();
        }
        if (host.requests[0].query.as_str() != "old input"
            || host.requests[4].query.as_str() != "new input") {
            return failed();
        }
        // Complete the selected new workflow before the old one. Publication is
        // an application decision; finishing an older call cannot overwrite it.
        auto delivered = false;
        {
            const auto worker =
                std::jthread([request = host.requests[4].request, &delivered]() noexcept {
                    delivered = request.complete(query_success(std::string("新阶段")));
                });
        }
        if (!delivered
            || latest->result()
            || host.requests[4].request.complete(query_success(std::string("duplicate")))
            || pump(*session) != HostSessionStatus::Waiting
            || latest->result()
            || host.requests.size() != 6
            || host.requests[5].query.as_str() != "updated follow-up: 新阶段"
            || !host.requests[5].request.complete(query_success(std::string("最新结果")))
            || pump(*session) != HostSessionStatus::Waiting
            || !text_result(*latest, "updated: 最新结果")) {
            return failed();
        }
        editor.collect();
        if (editor.rendered() != "updated: 最新结果") {
            return failed();
        }
        // Old native code still starts its second query after replacement, using
        // its original workflow and the same session-supplied host services.
        if (!host.requests[0].request.complete(query_success(std::string("旧阶段")))
            || pump(*session) != HostSessionStatus::Waiting
            || old->result()
            || host.requests.size() != 7
            || host.requests[6].query.as_str() != "compiled follow-up: 旧阶段"
            || !host.requests[6].request.complete(query_success(std::string("旧结果")))
            || pump(*session) != HostSessionStatus::Waiting
            || !text_result(*old, "preview: 旧结果")) {
            return failed();
        }
        editor.collect();
        if (editor.rendered() != "updated: 最新结果") {
            return failed();
        }
        retained = *old;
        retained->cancel(); // Completed handles retain results, without execution storage.
        // Each group has two queries in flight. An invalid first response is an
        // execution error, not a candidate cancellation that may wait for its sibling.
        const auto oversized = std::string(options.calls.limits.byte_work + 1, 'x');
        const auto limited_source = session->submit("preview_candidates", new_arguments);
        if (!limited_source
            || !host.requests[2].request.complete(query_success(oversized))
            || pump(*session) != HostSessionStatus::Waiting
            || host.requests.size() != 9) {
            return failed();
        }
        const auto* native_error = limited_native->result();
        if (!native_error) {
            std::println(
                std::cerr,
                "stopped candidate group still waiting; sibling active: {}",
                host.requests[3].request.active()
            );
            return failed();
        }
        if (*native_error
            || native_error->error().kind != HostErrorKind::Execution
            || host.requests[3].request.active()
            || host.cancelled.size() != 2
            || host.cancelled.back() != host.requests[3].request.id()
            || host.requests[3].request.complete(query_success(std::string("late native sibling")))
            || !host.requests[7].request.complete(query_success(oversized))
            || pump(*session) != HostSessionStatus::Idle) {
            return failed();
        }
        const auto* source_error = limited_source->result();
        if (!source_error
            || *source_error
            || source_error->error().kind != HostErrorKind::Execution
            || host.requests[8].request.active()
            || host.cancelled.size() != 3
            || host.cancelled.back() != host.requests[8].request.id()
            || session->active_count() != 0) {
            return failed();
        }
        const auto closing = editor.edit("closing input");
        if (!closing
            || pump(*session) != HostSessionStatus::Waiting
            || host.requests.size() != 10) {
            return failed();
        }
        editor.close();
        if (owner.alive() || endpoint.post(new_arguments) || host.requests[9].request.active()) {
            return failed();
        }
        session->close();
        if (!cancelled(*closing)
            || host.cancelled.size() != 4
            || host.cancelled.back() != host.requests[9].request.id()
            || host.requests[9].request.active()
            || session->poll(32uz) != HostSessionStatus::Closed) {
            return failed();
        }
    }
    // Completed handles and tokens outlive all program and execution storage.
    if (!retained
        || !text_result(*retained, "preview: 旧结果")
        || host.requests[9].request.complete(query_success(std::string("after destruction")))) {
        return failed();
    }
    {
        auto session = HostSession::create(query::source_module, options);
        if (!session) {
            return failed();
        }
        const auto arguments = std::array {HostValue(std::string("offline input"))};
        const auto interrupted = session->submit("preview_checked", arguments);
        if (!interrupted || pump(*session) != HostSessionStatus::Waiting) {
            return failed();
        }
        const auto error = HostError {
            .kind = HostErrorKind::Execution,
            .message = "provider disconnected",
            .location = std::nullopt,
            .calls = {}
        };
        const auto request = host.requests.back().request;
        if (!request.fail(error)
            || interrupted->result()
            || request.active()
            || pump(*session) != HostSessionStatus::Idle) {
            return failed();
        }
        const auto* interrupted_result = interrupted->result();
        if (!interrupted_result
            || *interrupted_result
            || interrupted_result->error().kind != error.kind
            || interrupted_result->error().message != error.message) {
            return failed();
        }
        // A request admitted to native code fails after reload. The new source
        // call catches the same nominal failure using its own business body.
        const auto first = host.requests.size();
        const auto checked_native = session->submit("preview_checked", arguments);
        const auto native = session->submit("preview", arguments);
        if (!checked_native
            || !native
            || pump(*session) != HostSessionStatus::Waiting
            || host.requests.size() != first + 2
            || !session->reload(updated_source)) {
            return failed();
        }
        const auto checked_source = session->submit("preview_checked", arguments);
        const auto source = session->submit("preview", arguments);
        if (!checked_source
            || !source
            || pump(*session) != HostSessionStatus::Waiting
            || host.requests.size() != first + 4) {
            return failed();
        }
        const auto unavailable = query::Failure0 {.code = 503};
        for (auto index = first; index < first + 4; ++index) {
            if (!host.requests[index].request.complete(Query::Response::failure(unavailable))) {
                return failed();
            }
        }
        if (pump(*session) != HostSessionStatus::Idle
            || !text_result(*native, "preview unavailable: 503")
            || !text_result(*source, "updated unavailable: 503")
            || host.requests.size() != first + 4) {
            return failed();
        }
        const auto* native_result = checked_native->result();
        const auto* source_result = checked_source->result();
        const auto* native_failure =
            native_result && *native_result ? std::get_if<HostFailure>(&**native_result) : nullptr;
        const auto* source_failure =
            source_result && *source_result ? std::get_if<HostFailure>(&**source_result) : nullptr;
        const auto* native_code = native_failure
            ? std::get<HostRecord>(native_failure->payload()).field("code")
            : nullptr;
        const auto* code = source_failure
            ? std::get<HostRecord>(source_failure->payload()).field("code")
            : nullptr;
        if (!native_failure
            || !source_failure
            || !native_failure->same_type(*source_failure)
            || source_failure->module_path() != query::source_module
            || source_failure->name() != "QueryError"
            || !native_code
            || !std::get_if<std::int32_t>(native_code)
            || !code
            || !std::get_if<std::int32_t>(code)
            || *std::get_if<std::int32_t>(code) != 503
            || *std::get_if<std::int32_t>(native_code) != 503) {
            return failed();
        }
        // Cancellation wins over an already delivered failure; catch must not turn
        // cancellation into successful fallback text.
        const auto cancelled_call = session->submit("preview", arguments);
        if (!cancelled_call
            || pump(*session) != HostSessionStatus::Waiting
            || !host.requests.back().request.complete(Query::Response::failure(unavailable))) {
            return failed();
        }
        cancelled_call->cancel();
        if (pump(*session) != HostSessionStatus::Idle || !cancelled(*cancelled_call)) {
            return failed();
        }
    }
    // A ready success cannot hide a stopped sibling in first_successful.
    auto race = HostSession::create(query::source_module, options);
    if (!race) {
        return failed();
    }
    for (const auto source : {false, true}) {
        if (source && !race->reload(updated_source)) {
            return failed();
        }
        const auto first = host.requests.size();
        const auto arguments = std::array {HostValue(std::string("race"))};
        const auto task = race->submit("preview_candidates", arguments);
        if (!task
            || pump(*race) != HostSessionStatus::Waiting
            || host.requests.size() != first + 2
            || !host.requests[first].request.complete(query_success("ready success"))
            || !host.requests[first + 1].request.complete(
                query_success(std::string(options.calls.limits.byte_work + 1, 'x'))
            )
            || pump(*race) != HostSessionStatus::Idle) {
            return failed();
        }
        const auto* result = task->result();
        if (!result || *result || result->error().kind != HostErrorKind::Execution) {
            return failed();
        }
        const auto child_index = host.requests.size();
        const auto child = race->submit("preview_child", arguments);
        if (!child
            || pump(*race) != HostSessionStatus::Waiting
            || host.requests.size() != child_index + 1
            || !host.requests[child_index].request.fail(
                HostError {
                    .kind = HostErrorKind::Execution,
                    .message = "child stopped",
                    .location = std::nullopt,
                    .calls = {}
                }
            )
            || pump(*race) != HostSessionStatus::Idle) {
            return failed();
        }
        const auto* child_result = child->result();
        if (!child_result || *child_result || child_result->error().message != "child stopped") {
            return failed();
        }
    }
    retained->cancel();
    std::println(
        "preview: document edits -> reload -> selected result; old workflow completes; scoped teardown"
    );
    return 0;
}
