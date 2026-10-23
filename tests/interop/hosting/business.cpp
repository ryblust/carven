#include <carven/hosting/api.hpp>
#include <carven/hosting/service.hpp>
#include <carven/providers/tests/interop/hosting/business.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

auto run_document() noexcept -> bool;
auto run_enumerations() noexcept -> bool;
auto run_document_pair() noexcept -> bool;

namespace {

namespace services = carven::providers::tests::interop::hosting::business::cv_escaped_functions;

struct HostState final {
    std::int32_t minimum;
    std::string prefix;
    std::vector<services::submit::Parameter0> submitted;
};

auto host_error(std::string message) noexcept -> std::unexpected<HostError> {
    return std::unexpected(
        HostError {
            .kind = HostErrorKind::Arguments,
            .message = std::move(message),
            .location = std::nullopt,
            .calls = {}
        }
    );
}

auto minimum_score(void* context) noexcept -> services::minimum_score::Result {
    return static_cast<HostState*>(context)->minimum;
}

auto decorate(void* context, services::decorate::Argument0 text) noexcept
    -> services::decorate::Result {
    auto decorated = services::decorate::Result();
    decorated.push(
        services::decorate::Parameter0::from_utf8(
            static_cast<HostState*>(context)->prefix + std::string(text.as_str())
        )
    );
    decorated.push(text);
    return decorated;
}

auto submit(void* context, services::submit::Argument0 decision) noexcept
    -> services::submit::Result {
    static_cast<HostState*>(context)->submitted.push_back(decision);
    return {};
}

auto decision(
    const std::expected<HostOutcome, HostError>& result,
    bool accepted,
    std::string_view label
) noexcept -> bool {
    if (!result) {
        std::println(std::cerr, "{}", result.error().message);
        return false;
    }
    const auto* value = std::get_if<HostValue>(&*result);
    const auto* record = value ? std::get_if<HostRecord>(value) : nullptr;
    const auto* flag = record ? record->field("accepted") : nullptr;
    const auto* text = record ? record->field("label") : nullptr;
    const auto* actual_flag = flag ? std::get_if<bool>(flag) : nullptr;
    const auto* actual_text = text ? std::get_if<std::string>(text) : nullptr;
    return actual_flag && *actual_flag == accepted && actual_text && *actual_text == label;
}

} // namespace

auto main() -> int {
    auto file = std::ifstream("tests/interop/hosting/business.cv");
    if (!file) {
        return 1;
    }
    const auto text = std::string(std::istreambuf_iterator<char>(file), {});
    auto host = HostState {.minimum = 60, .prefix = "review: ", .submitted = {}};
    const auto minimum_binding = services::minimum_score::service.bind(&host, minimum_score);
    const auto decorate_binding = services::decorate::service.bind(&host, decorate);
    const auto submit_binding = services::submit::service.bind(&host, submit);
    const auto bindings = std::array {
        host_service(minimum_binding),
        host_service(decorate_binding),
        host_service(submit_binding),
    };
    const auto options = host_call_options();
    auto session_options = host_session_options();
    session_options.calls = options;
    session_options.services = bindings;
    auto session = HostSession::load(services::submit::source_module, text, session_options);
    if (!session) {
        std::println(std::cerr, "{}", session.error().message);
        return 1;
    }
    const auto call =
        [&](std::span<const HostValue> values) noexcept -> std::expected<HostOutcome, HostError> {
        auto task = session->submit("process", values);
        if (!task) {
            return std::unexpected(std::move(task.error()));
        }
        while (session->poll(64) == HostSessionStatus::Runnable) {
        }
        const auto* result = task->result();
        return result
            ? *result
            : std::expected<HostOutcome, HostError>(host_error("unexpected pending business call"));
    };
    auto arguments = std::array {HostValue(
        HostRecord {
            .module_path = std::string(services::submit::source_module),
            .name = "Record",
            .fields = {
                HostField {.name = "text", .value = std::string("记录 A")},
                HostField {.name = "score", .value = std::int32_t {50}},
            }
        }
    )};
    const auto first = call(arguments);
    if (!decision(first, false, "review: 记录 A") || host.submitted.size() != 1) {
        return 1;
    }
    // The host owns both its input and submitted output after the invocation ends.
    auto* input = std::get_if<HostRecord>(&arguments[0]);
    if (!input) {
        return 1;
    }
    input->fields[0].value = std::string();
    const auto rejected = call(arguments);
    const auto* failure = rejected ? std::get_if<HostFailure>(&*rejected) : nullptr;
    const auto* payload = failure ? std::get_if<HostRecord>(&failure->payload()) : nullptr;
    const auto* reason = payload ? payload->field("reason") : nullptr;
    const auto* message = reason ? std::get_if<std::string>(reason) : nullptr;
    if (!failure
        || failure->name() != "RecordError"
        || !message
        || *message != "empty record"
        || host.submitted.size() != 1) {
        return 1;
    }
    auto updated_file = std::ifstream("tests/interop/hosting/business_updated.cv");
    if (!updated_file) {
        return 1;
    }
    const auto updated_source = std::string(std::istreambuf_iterator<char>(updated_file), {});
    const auto reload = session->reload(updated_source);
    if (!reload) {
        std::println(std::cerr, "{}", reload.error().message);
        return 1;
    }
    const auto updated_rejection = call(arguments);
    const auto* updated_failure =
        updated_rejection ? std::get_if<HostFailure>(&*updated_rejection) : nullptr;
    if (!updated_failure || !failure->same_type(*updated_failure)) {
        return 1;
    }
    // Only the Carven rule changed. Existing C++ configuration and submissions survive.
    input->fields[0].value = std::string("记录 A");
    if (!decision(call(arguments), true, "updated review: 记录 A") || host.submitted.size() != 2) {
        return 1;
    }
    host.minimum = 40;
    host.prefix = "accepted: ";
    input->fields[0].value = std::string("记录 A");
    const auto second = call(arguments);
    if (!decision(second, true, "updated accepted: 记录 A") || host.submitted.size() != 3) {
        return 1;
    }
    const auto& retained = host.submitted.front();
    if (retained.label.as_str() != "review: 记录 A"
        || retained.notes.size() != 3
        || retained.notes.begin()[0].as_str() != "review: 记录 A"
        || retained.notes.begin()[1].as_str() != "记录 A"
        || retained.notes.begin()[2].as_str() != "score: 50") {
        return 1;
    }
    session->close();
    // These owned failures remain useful after both source programs are released.
    const auto failure_copy = *failure;
    if (!failure_copy.same_type(*failure)
        || !failure_copy.same_type(*updated_failure)
        || failure_copy.module_path() != services::submit::source_module
        || failure_copy.name() != "RecordError") {
        return 1;
    }
    const auto* copied_record = std::get_if<HostRecord>(&failure_copy.payload());
    const auto* copied_reason = copied_record ? copied_record->field("reason") : nullptr;
    const auto* copied_text = copied_reason ? std::get_if<std::string>(copied_reason) : nullptr;
    if (!copied_text || *copied_text != "empty record") {
        return 1;
    }
    std::println("business: review -> empty record -> reload -> accepted; 3 submissions");
    return run_enumerations() && run_document() && run_document_pair() ? 0 : 1;
}
