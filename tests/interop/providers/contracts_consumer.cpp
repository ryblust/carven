#include <carven/api/tests/interop/providers/contracts.hpp>
#include <carven/api/tests/interop/providers/contracts.hpp>

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace api = carven::api::tests::interop::providers::contracts;
using String = carven::runtime::String;
using Record = decltype(api::make_record());
using Failure = decltype(api::make_failure());
using FailureResult = decltype(api::failure(false));

static_assert(std::is_same_v<decltype(api::text), auto() noexcept -> String>);
static_assert(std::is_same_v<decltype(api::copy), auto(const String&) noexcept -> String>);
static_assert(std::is_same_v<decltype(api::replace), auto(String&) noexcept -> void>);
static_assert(
    std::is_same_v<decltype(api::take), auto(boundary::Owned) noexcept -> boundary::Owned>
);
static_assert(std::is_same_v<decltype(api::record), auto(const Record&) noexcept -> Record>);
static_assert(std::is_same_v<decltype(api::guarded_failure(false, true)), FailureResult>);

} // namespace

auto contract_consumer() noexcept -> bool {
    auto text = api::text();
    if (text.as_str() != "owned" || api::copy(text).as_str() != "owned") {
        return false;
    }
    api::replace(text);
    if (text.as_str() != "replaced") {
        return false;
    }
    auto owner = contract_owner();
    const auto* identity = owner.get();
    auto moved = api::take(std::move(owner));
    if (moved.get() != identity || *moved != 42) {
        return false;
    }
    auto number = std::int32_t {3};
    if (api::pointer(&number) != &number || number != 4) {
        return false;
    }
    const auto record = api::record(api::make_record());
    if (record.count != 9 || record.text.as_str() != "record") {
        return false;
    }
    const auto values = std::array<std::int32_t, 2> {4, 8};
    if (api::array(values) != values || api::view("view") != "view") {
        return false;
    }
    const auto view = api::slice(carven::runtime::as_slice(values));
    if (view.data() != values.data()
        || view.size() != 2
        || view[1] != 8
        || api::closure()(4) != 7) {
        return false;
    }
    const auto callback = [](std::int32_t value) noexcept {
        return value + 2;
    };
    if (api::callback(+callback) != 9) {
        return false;
    }
    auto success = api::failure(false);
    auto failure = api::failure(true);
    auto guarded_success = api::guarded_failure(false, true);
    auto guarded_failure = api::guarded_failure(true, true);
    auto void_success = api::failed_void(false);
    auto void_failure = api::failed_void(true);
    auto guarded_void_success = api::guarded_void(false, true);
    auto guarded_void_failure = api::guarded_void(true, true);
    return success.success_if() != nullptr
        && success.success_if()->value.as_str() == "success"
        && failure.failure_if<Failure>() != nullptr
        && failure.failure_if<Failure>()->code == 17
        && guarded_success.success_if() != nullptr
        && guarded_success.success_if()->value.as_str() == "success"
        && guarded_failure.failure_if<Failure>() != nullptr
        && guarded_failure.failure_if<Failure>()->code == 17
        && void_success.success_if() != nullptr
        && void_failure.failure_if<Failure>() != nullptr
        && void_failure.failure_if<Failure>()->code == 23
        && guarded_void_success.success_if() != nullptr
        && guarded_void_failure.failure_if<Failure>() != nullptr
        && guarded_void_failure.failure_if<Failure>()->code == 23;
}
