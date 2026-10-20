#include <carven/api/tests/interop/hosting/enumeration.hpp>
#include <carven/hosting/api.hpp>
#include <carven/hosting/entry.hpp>
#include <carven/hosting/service.hpp>
#include <carven/providers/tests/interop/hosting/enumeration.hpp>

#include "document_host.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace compiled = carven::api::tests::interop::hosting::enumeration;
namespace providers = carven::providers::tests::interop::hosting::enumeration::cv_escaped_functions;
using Sync = std::remove_cvref_t<decltype(providers::native_failure::service)>;
using Async = std::remove_cvref_t<decltype(providers::async_failure::service)>;
using Fault = providers::native_failure::Failure0;

auto native_failure(void*) noexcept -> Sync::Response {
    return Sync::Response::failure(Fault::Count(9000000000ll));
}

auto async_failure(void*, HostRequest<Async::Response> request) noexcept -> void {
    request.complete(Async::Response::failure(Fault::Count(-9000000000ll)));
}

auto enumeration(std::string name, std::string selected, std::vector<HostValue> payload) noexcept
    -> HostEnum {
    return {
        .module_path = std::string(providers::native_failure::source_module),
        .name = std::move(name),
        .case_name = std::move(selected),
        .payload = std::move(payload)
    };
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
    while (!task->result()) {
        session.poll(256);
    }
    return *task->result();
}

auto report_failure(
    std::string_view context,
    const std::expected<HostOutcome, HostError>& result
) noexcept -> bool {
    if (!result) {
        std::println(std::cerr, "enum {}: {}", context, result.error().message);
    } else {
        std::println(std::cerr, "enum {}: unexpected result payload", context);
    }
    return false;
}

auto count_failure(const std::expected<HostOutcome, HostError>& result, std::int64_t count) noexcept
    -> bool {
    const auto* failure = result ? std::get_if<HostFailure>(&*result) : nullptr;
    const auto* value = failure ? std::get_if<HostEnum>(&failure->payload()) : nullptr;
    const auto* actual = value && value->payload.size() == 1
        ? std::get_if<std::int64_t>(&value->payload[0])
        : nullptr;
    return value
        && failure->name() == "Fault"
        && value->case_name == "Count"
        && actual
        && *actual == count;
}

auto data_failure(const HostEnum& value) noexcept -> bool {
    if (value.name != "Fault" || value.case_name != "Data" || value.payload.size() != 2) {
        return false;
    }
    const auto* text = std::get_if<std::string>(&value.payload[0]);
    const auto* levels = std::get_if<std::vector<HostValue>>(&value.payload[1]);
    const auto* level =
        levels && levels->size() == 1 ? std::get_if<HostEnum>(&levels->front()) : nullptr;
    return text
        && *text == "owned"
        && level
        && level->name == "Level"
        && level->case_name == "High"
        && level->payload.empty();
}

auto check_calls(HostSession& session) noexcept -> bool {
    const auto fault = enumeration(
        "Fault",
        "Data",
        {std::string("owned"), std::vector<HostValue> {enumeration("Level", "High", {})}}
    );
    const auto envelope = HostRecord {
        .module_path = fault.module_path,
        .name = "Envelope",
        .fields = {
            {.name = "values",
             .value = std::vector<HostValue> {fault, enumeration("Fault", "Empty", {})}}
        }
    };
    const auto arguments = std::array {HostValue(envelope)};
    const auto reflected = call(session, "reflect", arguments);
    const auto* result = reflected ? std::get_if<HostValue>(&*reflected) : nullptr;
    const auto* record = result ? std::get_if<HostRecord>(result) : nullptr;
    const auto* field = record ? record->field("values") : nullptr;
    const auto* values = field ? std::get_if<std::vector<HostValue>>(field) : nullptr;
    const auto* first =
        values && values->size() == 2 ? std::get_if<HostEnum>(&(*values)[0]) : nullptr;
    const auto* second =
        values && values->size() == 2 ? std::get_if<HostEnum>(&(*values)[1]) : nullptr;
    if (!first
        || !data_failure(*first)
        || !second
        || second->case_name != "Empty"
        || !second->payload.empty()) {
        return report_failure("reflect", reflected);
    }
    const auto failed = call(session, "reject", {});
    const auto* failure = failed ? std::get_if<HostFailure>(&*failed) : nullptr;
    const auto* value = failure ? std::get_if<HostEnum>(&failure->payload()) : nullptr;
    if (!value || !data_failure(*value)) {
        return report_failure("reject", failed);
    }
    for (const auto& [name, count] :
         {std::pair {std::string_view("reject_async"), -9000000000ll},
          std::pair {std::string_view("provider_failure"), 9000000000ll},
          std::pair {std::string_view("provider_async"), -9000000000ll}}) {
        const auto result = call(session, name, {});
        if (!count_failure(result, count)) {
            return report_failure(name, result);
        }
    }
    const auto numeric = std::array {HostValue(enumeration("Level", "High", {}))};
    const auto echoed = call(session, "numeric", numeric);
    const auto* numeric_value = echoed ? std::get_if<HostValue>(&*echoed) : nullptr;
    const auto* selected = numeric_value ? std::get_if<HostEnum>(numeric_value) : nullptr;
    if (!selected || selected->case_name != "High") {
        return report_failure("numeric", echoed);
    }
    for (const auto& invalid :
         {enumeration("Level", "Unknown", {}), enumeration("Level", "High", {std::int32_t(17)})}) {
        const auto invalid_arguments = std::array {HostValue(invalid)};
        const auto rejected = session.submit("numeric", invalid_arguments);
        if (rejected || rejected.error().kind != HostErrorKind::Arguments) {
            std::println(
                std::cerr,
                "invalid enum {}.{} was admitted",
                invalid.name,
                invalid.case_name
            );
            return false;
        }
    }
    return true;
}

auto check_cold_reference(HostSession& session) noexcept -> bool {
    auto document = std::make_unique<documents::Document>("enum payload");
    const auto borrowed = enumeration("Fault", "Borrowed", {HostValue(document->reference())});
    const auto envelope = std::array {HostValue(
        HostRecord {
            .module_path = borrowed.module_path,
            .name = "Envelope",
            .fields = {{.name = "values", .value = std::vector<HostValue> {borrowed}}}
        }
    )};
    const auto reference = std::array {
        HostValue(enumeration("Reference", "Object", {HostValue(document->reference())}))
    };
    auto reflected = session.submit("reflect", envelope);
    auto pending = session.submit("cold_reference", reference);
    if (!reflected || !pending) {
        std::println(
            std::cerr,
            "cold enum admission: {}",
            !reflected ? reflected.error().message : pending.error().message
        );
        return false;
    }
    document.reset();
    while (!reflected->result() || !pending->result()) {
        session.poll(256);
    }
    const auto rejected = [](std::string_view name, const HostTask& task) noexcept {
        const auto& result = *task.result();
        if (result || result.error().kind != HostErrorKind::Arguments) {
            return report_failure(name, result);
        }
        return true;
    };
    return rejected("expired synchronous reference", *reflected)
        && rejected("expired async reference", *pending);
}

auto check_numeric_contract(HostSession& session, const std::string& source) noexcept -> bool {
    for (const auto& [original, replacement] :
         {std::pair {std::string_view("High = 17"), std::string_view("High = 19")},
          std::pair {std::string_view("enum Level: i32"), std::string_view("enum Level: u8")}}) {
        auto changed = source;
        changed.replace(changed.find(original), original.size(), replacement);
        const auto rejected = session.reload(changed);
        if (rejected || rejected.error().kind != HostErrorKind::Signature) {
            std::println(std::cerr, "changed numeric enum contract was admitted");
            return false;
        }
    }
    return true;
}

} // namespace

auto run_enumerations() noexcept -> bool {
    const auto binding = providers::native_failure::service.bind(nullptr, native_failure);
    const auto services = std::array {
        host_service(binding),
        host_async_service<async_failure, nullptr>(providers::async_failure::service, nullptr)
    };
    const auto entries = std::array {
        host_entry<compiled::reflect>(compiled::cv_escaped_functions::reflect::service),
        host_entry<compiled::numeric>(compiled::cv_escaped_functions::numeric::service),
        host_entry<compiled::cold_reference>(
            compiled::cv_escaped_functions::cold_reference::service
        ),
        host_entry<compiled::reject>(compiled::cv_escaped_functions::reject::service),
        host_entry<compiled::reject_async>(compiled::cv_escaped_functions::reject_async::service),
        host_entry<compiled::provider_failure>(
            compiled::cv_escaped_functions::provider_failure::service
        ),
        host_entry<compiled::provider_async>(
            compiled::cv_escaped_functions::provider_async::service
        ),
    };
    auto options = host_session_options();
    options.services = services;
    options.entries = entries;
    auto session = HostSession::create(providers::native_failure::source_module, options);
    if (!session) {
        std::println(std::cerr, "enum session: {}", session.error().message);
        return false;
    }
    if (!check_calls(*session) || !check_cold_reference(*session)) {
        std::println(std::cerr, "compiled enum boundary failed");
        return false;
    }
    auto input = std::ifstream("tests/interop/hosting/enumeration.cv");
    const auto source = std::string(std::istreambuf_iterator<char>(input), {});
    if (!check_numeric_contract(*session, source)) {
        return false;
    }
    auto loaded = HostProgram::load(providers::native_failure::source_module, source);
    if (!loaded) {
        std::println(std::cerr, "{}", loaded.error().message);
        return false;
    }
    auto reject = loaded->function("reject");
    if (!reject) {
        std::println(std::cerr, "{}", reject.error().message);
        return false;
    }
    const auto failure = reject->call({}, host_call_options());
    const auto* owned = failure ? std::get_if<HostFailure>(&*failure) : nullptr;
    const auto* value = owned ? std::get_if<HostEnum>(&owned->payload()) : nullptr;
    if (!value || !data_failure(*value) || !session->reload(source) || !check_calls(*session)) {
        std::println(std::cerr, "source enum boundary failed");
        return false;
    }
    session->close();
    return true;
}
