#include <carven/api/tests/interop/hosting/document_pair.hpp>
#include <carven/hosting/api.hpp>
#include <carven/hosting/entry.hpp>
#include <carven/hosting/service.hpp>
#include <carven/providers/tests/interop/hosting/document_pair.hpp>
#include "document_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace compiled = carven::api::tests::interop::hosting::document_pair;

namespace providers =
    carven::providers::tests::interop::hosting::document_pair::cv_escaped_functions;

using Read = std::remove_cvref_t<decltype(providers::read::service)>;
using Inspect = std::remove_cvref_t<decltype(providers::inspect::service)>;

auto source(std::string_view path) noexcept -> std::string {
    auto input = std::ifstream(std::string(path));
    return {std::istreambuf_iterator<char>(input), {}};
}

struct DocumentAccess final {
    std::size_t reads;
    std::size_t publications;
};

auto alive(void*, providers::alive::Argument0 reference) noexcept -> bool {
    return reference.get() != nullptr;
}

auto read(void* context, providers::read::Argument0 reference) noexcept -> Read::Response {
    ++static_cast<DocumentAccess*>(context)->reads;
    return Read::Response::success_from([&]() noexcept {
        return providers::read::Result::from_utf8(reference.get()->read());
    });
}

auto publish(
    void* context,
    providers::publish::Argument0 reference,
    providers::publish::Argument1 text
) noexcept -> bool {
    ++static_cast<DocumentAccess*>(context)->publications;
    return reference.get()->publish(std::string(text.as_str()));
}

auto native_text(void*) noexcept -> providers::native_text::Result {
    return "native text";
}

auto consume(void*, providers::consume::Argument0 text) noexcept -> providers::consume::Result {
    text += " consumed";
    return providers::consume::Result::from_utf8(std::move(text));
}

auto inspect(void*, providers::inspect::Argument0 accepted) noexcept -> Inspect::Response {
    if (accepted) {
        return Inspect::Response::success_from([]() noexcept {
            return providers::inspect::Result {.code = 23};
        });
    }
    return Inspect::Response::failure(providers::inspect::Failure0 {.code = 17});
}

auto token(void*) noexcept -> providers::token::Result {
    return providers::token::Result(41);
}

auto call(
    HostSession& session,
    std::string_view name,
    std::span<const HostValue> arguments
) noexcept -> std::expected<HostOutcome, HostError> {
    auto task = session.submit(name, arguments);
    if (!task) {
        return std::unexpected(std::move(task.error()));
    }
    for (auto turn = 0; turn < 10000 && task->result() == nullptr; ++turn) {
        session.poll(64);
    }
    const auto* result = task->result();
    if (!result) {
        return std::unexpected(
            HostError {
                .kind = HostErrorKind::Execution,
                .message = "synchronous document call did not finish",
                .location = std::nullopt,
                .calls = {}
            }
        );
    }
    return *result;
}

auto render(HostSession& session, documents::Ref document, std::int32_t index) noexcept -> bool {
    const auto arguments = std::array {HostValue(std::move(document)), HostValue(index)};
    const auto result = call(session, "render", arguments);
    if (!result) {
        std::println(std::cerr, "{}", result.error().message);
        return false;
    }
    const auto* value = std::get_if<HostValue>(&*result);
    const auto* flag = value ? std::get_if<bool>(value) : nullptr;
    return flag && *flag;
}

auto copied_text(HostSession& session) noexcept -> bool {
    const auto result = call(session, "copied_text", {});
    const auto* value = result ? std::get_if<HostValue>(&*result) : nullptr;
    const auto* text = value ? std::get_if<std::string>(value) : nullptr;
    return text && *text == "native text consumed";
}

auto contract_results(HostSession& session) noexcept -> bool {
    for (const auto accepted : {true, false}) {
        const auto arguments = std::array {HostValue(accepted)};
        const auto result = call(session, "inspection", arguments);
        if (!result) {
            return false;
        }
        const auto* value = std::get_if<HostValue>(&*result);
        const auto* failure = std::get_if<HostFailure>(&*result);
        const auto* record = value ? std::get_if<HostRecord>(value)
            : failure              ? std::get_if<HostRecord>(&failure->payload())
                                   : nullptr;
        const auto* field = record ? record->field("code") : nullptr;
        const auto* code = field ? std::get_if<std::int32_t>(field) : nullptr;
        if (!code || (accepted ? !value || *code != 23 : !failure || *code != 17)) {
            return false;
        }
    }
    const auto result = call(session, "make_token", {});
    const auto* value = result ? std::get_if<HostValue>(&*result) : nullptr;
    const auto* box = value ? std::get_if<HostObjectRef>(value) : nullptr;
    const auto* token = box ? box->get<documents::Token>() : nullptr;
    return token && token->value == 41 && token->is_stopped();
}

} // namespace

auto run_document_pair() noexcept -> bool {
    auto first = std::make_unique<documents::Document>("A");
    auto second = std::make_unique<documents::Document>("B");
    const auto first_ref = first->reference();
    const auto second_ref = second->reference();
    auto access = DocumentAccess {.reads = 0, .publications = 0};
    const auto alive_binding = providers::alive::service.bind(nullptr, alive);
    const auto read_binding = providers::read::service.bind(&access, read);
    const auto publish_binding = providers::publish::service.bind(&access, publish);
    const auto text_binding = providers::native_text::service.bind(nullptr, native_text);
    const auto consume_binding = providers::consume::service.bind(nullptr, consume);
    const auto inspect_binding = providers::inspect::service.bind(nullptr, inspect);
    const auto token_binding = providers::token::service.bind(nullptr, token);
    const auto services = std::array {
        host_service(alive_binding),
        host_service(read_binding),
        host_service(publish_binding),
        host_service(text_binding),
        host_service(consume_binding),
        host_service(inspect_binding),
        host_service(token_binding),
    };
    const auto entries = std::array {
        host_entry<&compiled::render>(compiled::cv_escaped_functions::render::service),
        host_entry<&compiled::identity>(compiled::cv_escaped_functions::identity::service),
        host_entry<&compiled::read_checked>(compiled::cv_escaped_functions::read_checked::service),
        host_entry<&compiled::selected>(compiled::cv_escaped_functions::selected::service),
        host_entry<&compiled::copied_text>(compiled::cv_escaped_functions::copied_text::service),
        host_entry<&compiled::texts>(compiled::cv_escaped_functions::texts::service),
        host_entry<&compiled::inspection>(compiled::cv_escaped_functions::inspection::service),
        host_entry<&compiled::make_token>(compiled::cv_escaped_functions::make_token::service),
    };
    auto options = host_session_options();
    options.services = services;
    options.entries = entries;
    // No source is loaded or interpreted before replacement. These names initially
    // select the implementations generated from document_pair.cv into the static library.
    auto session = HostSession::create(providers::read::source_module, options);
    if (!session || !session->diagnostics().empty() || !contract_results(*session)) {
        return false;
    }
    auto limited_options = options;
    limited_options.calls.limits.byte_work = 6;
    auto limited_access = DocumentAccess {.reads = 0, .publications = 0};
    const auto limited_read = providers::read::service.bind(
        &limited_access,
        [](void* context, providers::read::Argument0) noexcept -> Read::Response {
            ++static_cast<DocumentAccess*>(context)->reads;
            return Read::Response::success_from([]() noexcept {
                return providers::read::Result::from_str("blocked");
            });
        }
    );
    const auto limited_publish = providers::publish::service.bind(&limited_access, publish);
    auto limited_services = services;
    limited_services[1] = host_service(limited_read);
    limited_services[2] = host_service(limited_publish);
    limited_options.services = limited_services;
    auto limited = HostSession::create(providers::read::source_module, limited_options);
    const auto text_arguments = std::array {HostValue(
        HostRecord {
            .module_path = std::string(compiled::cv_escaped_functions::texts::source_module),
            .name = "Texts",
            .fields = {
                {.name = "first", .value = std::string("four")},
                {.name = "second", .value = std::string("four")}
            },
        }
    )};
    const auto exceeds_budget = [&]() noexcept {
        const auto rejected = limited->submit("texts", text_arguments);
        return !rejected && rejected.error().kind == HostErrorKind::Arguments;
    };
    if (!limited || !exceeds_budget()) {
        return false;
    }
    const auto stopped_arguments = std::array {HostValue(first_ref), HostValue(std::int32_t {0})};
    const auto native_stop = call(*limited, "render", stopped_arguments);
    if (native_stop
        || !limited->reload(source("tests/interop/hosting/document_pair_updated.cv"))
        || !exceeds_budget()) {
        return false;
    }
    const auto source_stop = call(*limited, "render", stopped_arguments);
    if (source_stop
        || native_stop.error().kind != HostErrorKind::Execution
        || source_stop.error().kind != native_stop.error().kind
        || source_stop.error().message != native_stop.error().message
        || limited_access.reads != 2
        || limited_access.publications != 0) {
        return false;
    }
    auto first_subscription = session->subscribe(first_ref, "render");
    auto second_subscription = session->subscribe(second_ref, "render");
    if (!first_subscription || !second_subscription) {
        return false;
    }
    const auto first_event = first_subscription->event();
    const auto second_event = second_subscription->event();
    const auto notify =
        [](const HostEvent& event, documents::Ref document, std::int32_t index) noexcept {
            const auto arguments = std::array {HostValue(std::move(document)), HostValue(index)};
            return event.post(arguments);
        };
    const auto pump = [&]() noexcept {
        auto status = HostSessionStatus::Runnable;
        for (auto turn = 0; turn < 10000 && status == HostSessionStatus::Runnable; ++turn) {
            status = session->poll(64);
        }
        return status;
    };
    const auto succeeded = [](const HostTask& task) noexcept {
        const auto* result = task.result();
        const auto* value = result && *result ? std::get_if<HostValue>(&**result) : nullptr;
        const auto* flag = value ? std::get_if<bool>(value) : nullptr;
        return flag && *flag;
    };
    auto native_event = notify(second_event, second_ref, 2);
    if (!native_event
        || native_event->result()
        || !render(*session, first_ref, 1)
        || pump() != HostSessionStatus::Idle
        || !succeeded(*native_event)
        || access.reads != 2
        || access.publications != 2
        || first->rendered() != "document 1: A"
        || second->rendered() != "document 2: B") {
        return false;
    }
    // The alternate binding fails. Both native code and replacement source run
    // their Carven catch arm, then successfully publish the fallback.
    auto alternate_reads = std::size_t {0};
    const auto alternate_read = providers::read::service.bind(
        &alternate_reads,
        [](void* context, providers::read::Argument0) noexcept -> Read::Response {
            ++*static_cast<std::size_t*>(context);
            return Read::Response::failure(providers::read::Failure0 {.code = 17});
        }
    );
    auto alternate_services = services;
    alternate_services[1] = host_service(alternate_read);
    auto alternate_options = options;
    alternate_options.services = alternate_services;
    auto alternate = HostSession::create(providers::read::source_module, alternate_options);
    const auto read_arguments = std::array {HostValue(second_ref)};
    if (!alternate) {
        return false;
    }
    auto native_read = call(*alternate, "read_checked", read_arguments);
    if (!render(*alternate, second_ref, 9)
        || second->rendered() != "document 9: unavailable 17"
        || !alternate->reload(source("tests/interop/hosting/document_pair_updated.cv"))
        || !render(*alternate, second_ref, 10)
        || second->rendered() != "updated 10: unavailable 17") {
        return false;
    }
    auto source_read = call(*alternate, "read_checked", read_arguments);
    const auto* native_failure = native_read ? std::get_if<HostFailure>(&*native_read) : nullptr;
    const auto* source_failure = source_read ? std::get_if<HostFailure>(&*source_read) : nullptr;
    alternate->close();
    const auto* native_code =
        native_failure ? std::get<HostRecord>(native_failure->payload()).field("code") : nullptr;
    const auto* code =
        source_failure ? std::get<HostRecord>(source_failure->payload()).field("code") : nullptr;
    if (!native_failure
        || !source_failure
        || !native_failure->same_type(*source_failure)
        || source_failure->module_path() != providers::read::source_module
        || source_failure->name() != "ReadError"
        || !native_code
        || !std::get_if<std::int32_t>(native_code)
        || !code
        || std::get_if<std::int32_t>(code) == nullptr
        || *std::get_if<std::int32_t>(code) != 17
        || *std::get_if<std::int32_t>(native_code) != 17
        || alternate_reads != 4
        || access.reads != 2) {
        return false;
    }
    const auto selection_arguments = [](HostValue reference) noexcept {
        return std::array {HostValue(
            HostRecord {
                .module_path = std::string(compiled::cv_escaped_functions::selected::source_module),
                .name = "Selection",
                .fields = {
                    {.name = "documents", .value = std::vector<HostValue> {std::move(reference)}}
                },
            }
        )};
    };
    const auto selected_arguments = selection_arguments(HostValue(second_ref));
    const auto native_selection = call(*session, "selected", selected_arguments);
    const auto* selected_value =
        native_selection ? std::get_if<HostValue>(&*native_selection) : nullptr;
    const auto* selected_box =
        selected_value ? std::get_if<HostObjectRef>(selected_value) : nullptr;
    const auto* selected_ref = selected_box ? selected_box->get<documents::Ref>() : nullptr;
    const auto wrong_selection =
        selection_arguments(HostObjectRef::own(std::string("wrong native type")));
    const auto rejected_selection = session->submit("selected", wrong_selection);
    if (!selected_ref
        || !selected_ref->same_object(second_ref)
        || rejected_selection
        || rejected_selection.error().kind != HostErrorKind::Arguments) {
        return false;
    }
    const auto identity_arguments = std::array {HostValue(second_ref)};
    const auto native_identity = call(*session, "identity", identity_arguments);
    const auto* native_value =
        native_identity ? std::get_if<HostValue>(&*native_identity) : nullptr;
    const auto* native_box = native_value ? std::get_if<HostObjectRef>(native_value) : nullptr;
    const auto* native_ref = native_box ? native_box->get<documents::Ref>() : nullptr;
    if (!native_ref || !native_ref->same_object(second_ref) || !copied_text(*session)) {
        return false;
    }
    const auto wrong = std::array {HostValue(first_ref), HostValue(std::string("wrong index"))};
    const auto bad_native = session->submit("render", wrong);
    if (bad_native || bad_native.error().kind != HostErrorKind::Arguments) {
        return false;
    }
    const auto old_arguments = std::array {HostValue(first_ref), HostValue(std::int32_t {3})};
    const auto old = session->submit("render", old_arguments);
    auto superseded = session->submit("render", old_arguments);
    auto queued_event = notify(second_event, second_ref, 4);
    if (!old || !superseded || !queued_event) {
        return false;
    }
    superseded->cancel();
    const auto updated = source("tests/interop/hosting/document_pair_updated.cv");
    auto changed_native = updated;
    const auto native_name = changed_native.find("documents::Ref");
    if (native_name == std::string::npos) {
        return false;
    }
    changed_native.replace(
        native_name,
        std::string_view("documents::Ref").size(),
        "documents::alternate::Ref"
    );
    const auto rejected_native = session->reload(changed_native);
    if (rejected_native
        || rejected_native.error().kind != HostErrorKind::Signature
        || old->result()) {
        return false;
    }
    auto incompatible = updated;
    const auto offset = incompatible.find("index: i32");
    if (offset == std::string::npos) {
        return false;
    }
    incompatible.replace(offset, std::string_view("index: i32").size(), "index: bool");
    const auto rejected = session->reload(incompatible);
    if (rejected || rejected.error().kind != HostErrorKind::Signature || old->result()) {
        return false;
    }
    const auto reload = session->reload(updated);
    if (!reload) {
        std::println(std::cerr, "{}", reload.error().message);
        return false;
    }
    if (!contract_results(*session)) {
        return false;
    }
    // An admitted call keeps the compiled implementation, whereas a queued event
    // selects the new source body when dispatched. Neither endpoint is replaced.
    if (pump() != HostSessionStatus::Idle
        || !succeeded(*old)
        || !succeeded(*queued_event)
        || !superseded->result()
        || !*superseded->result()
        || !std::holds_alternative<HostCancelled>(**superseded->result())
        || access.reads != 4
        || access.publications != 6
        || first->rendered() != "document 3: A"
        || second->rendered() != "updated 4: B") {
        return false;
    }
    const auto source_selection = call(*session, "selected", selected_arguments);
    const auto* source_selected_value =
        source_selection ? std::get_if<HostValue>(&*source_selection) : nullptr;
    const auto* source_selected_box =
        source_selected_value ? std::get_if<HostObjectRef>(source_selected_value) : nullptr;
    const auto* source_selected_ref =
        source_selected_box ? source_selected_box->get<documents::Ref>() : nullptr;
    if (!source_selected_ref
        || !source_selected_ref->same_object(second_ref)
        || !copied_text(*session)) {
        return false;
    }
    const auto bad_source = session->submit("render", wrong);
    first->edit("A edited");
    second->edit("B edited");
    auto updated_event = notify(second_event, second_ref, 6);
    if (bad_source
        || bad_source.error().kind != HostErrorKind::Arguments
        || !render(*session, first_ref, 5)
        || !updated_event
        || pump() != HostSessionStatus::Idle
        || !succeeded(*updated_event)
        || first->rendered() != "updated 5: A edited"
        || second->rendered() != "updated 6: B edited") {
        return false;
    }
    // Identity is omitted by the update and still runs its compiled implementation.
    const auto identity = call(*session, "identity", identity_arguments);
    const auto* value = identity ? std::get_if<HostValue>(&*identity) : nullptr;
    const auto* retained_box = value ? std::get_if<HostObjectRef>(value) : nullptr;
    const auto* retained = retained_box ? retained_box->get<documents::Ref>() : nullptr;
    if (!retained || !retained->same_object(second_ref)) {
        return false;
    }
    // Loading support declarations alone leaves both entry bindings intact. Render
    // must keep its older source alive even with no invocation using it at reload.
    const auto support = updated.substr(0, updated.find("fn render"));
    if (!session->reload(support)
        || !render(*session, first_ref, 8)
        || first->rendered() != "updated 8: A edited") {
        return false;
    }
    first.reset();
    const auto expired = session->submit("render", old_arguments);
    const auto expired_selection =
        session->submit("selected", selection_arguments(HostValue(first_ref)));
    if (expired_selection || expired_selection.error().kind != HostErrorKind::Arguments) {
        return false;
    }
    auto surviving = notify(second_event, second_ref, 7);
    if (first_ref.alive()
        || notify(first_event, first_ref, 7)
        || expired
        || expired.error().kind != HostErrorKind::Arguments
        || !surviving
        || pump() != HostSessionStatus::Idle
        || !succeeded(*surviving)
        || second->rendered() != "updated 7: B edited") {
        return false;
    }
    session->close();
    if (!retained->same_object(second_ref) || second_event.post(identity_arguments)) {
        return false;
    }
    second.reset();
    if (retained->alive() || native_ref->alive()) {
        return false;
    }
    std::println(
        "documents: render replaced; identity compiled; same calls, subscriptions, and objects"
    );
    return true;
}
