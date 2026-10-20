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

static_assert(std::is_same_v<
              decltype(api::copy),
              auto(const String&) noexcept -> carven::runtime::Outcome<String>>);

static_assert(
    std::is_same_v<decltype(api::replace), auto(String&) noexcept -> carven::runtime::Outcome<void>>
);

static_assert(std::is_same_v<
              decltype(api::take),
              auto(boundary::Owned) noexcept -> carven::runtime::Outcome<boundary::Owned>>);

static_assert(std::is_same_v<
              decltype(api::record),
              auto(const Record&) noexcept -> carven::runtime::Outcome<Record>>);

static_assert(std::is_same_v<decltype(api::guarded_failure(false, true)), FailureResult>);

static_assert(!std::is_constructible_v<
              carven::runtime::FunctionRef<String(const String&) noexcept>,
              decltype(&api::copy)>);

template<typename Factory>
concept VoidImport =
    requires (Factory factory) { carven::runtime::Outcome<void>::invoke(factory); };

static_assert(VoidImport<decltype([]() noexcept {})>);

static_assert(
    VoidImport<decltype([]() noexcept { return carven::runtime::Outcome<void>::stopped(); })>
);

static_assert(
    !VoidImport<decltype([]() noexcept { return carven::runtime::Outcome<int>::stopped(); })>
);

static_assert(
    !VoidImport<decltype([]() noexcept { return carven::runtime::Outcome<void, int>::stopped(); })>
);

} // namespace

auto contract_consumer() noexcept -> bool {
    auto text = api::text();
    const auto copied = api::copy(text);
    if (text.as_str() != "owned"
        || !copied.success_if()
        || copied.success_if()->value.as_str() != "owned") {
        return false;
    }
    const auto replaced = api::replace(text);
    if (!replaced.success_if() || text.as_str() != "replaced") {
        return false;
    }
    auto owner = contract_owner();
    const auto* identity = owner.get();
    const auto moved = api::take(std::move(owner));
    if (!moved.success_if()
        || moved.success_if()->value.get() != identity
        || *moved.success_if()->value != 42) {
        return false;
    }
    auto moves = std::int32_t {0};
    if (api::consume_copy_trivial(boundary::CopyTrivial(moves, 42)) != 42 || moves != 0) {
        return false;
    }
    if (api::consume_copy_only(boundary::CopyOnly(17)) != 17) {
        return false;
    }
    auto number = std::int32_t {3};
    const auto pointer = api::pointer(&number);
    if (!pointer.success_if() || pointer.success_if()->value != &number || number != 4) {
        return false;
    }
    const auto record = api::record(api::make_record());
    if (!record.success_if()
        || record.success_if()->value.count != 9
        || record.success_if()->value.text.as_str() != "record") {
        return false;
    }
    constexpr auto values = std::array<std::int32_t, 2> {4, 8};
    const auto array = api::array(values);
    if (!array.success_if() || array.success_if()->value != values || api::view("view") != "view") {
        return false;
    }
    const auto view = api::slice(carven::runtime::slice(values));
    if (view.data() != values.data()
        || view.size() != 2
        || view[1] != 8
        || api::closure()(4) != 7) {
        return false;
    }
    constexpr auto callback = [](std::int32_t value) noexcept {
        return value + 2;
    };
    const auto called = api::callback(+callback);
    if (!called.success_if() || called.success_if()->value != 9) {
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
