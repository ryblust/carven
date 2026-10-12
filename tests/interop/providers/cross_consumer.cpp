#include <carven/api/tests/interop/providers/cross_api.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace api = carven::api::tests::interop::providers::cross_api;

template<typename Result, typename Argument>
auto argument_type(Result (*)(Argument) noexcept)
    -> std::type_identity<std::remove_cvref_t<Argument>>;

using Token = decltype(argument_type(api::take))::type;

template<template<typename, typename...> class Carrier, typename Result, typename Failure>
auto denied(const Carrier<Result, Failure>& result) noexcept -> bool {
    const auto* error = result.template failure_if<Failure>();
    return error != nullptr
        && *error == Failure::Denied(carven::runtime::String::from_str(std::string_view("failed")));
}

} // namespace

// Include only the public API for the source-defined payloads. This translation
// unit constructs, inspects and destroys them without generated detail headers.
auto cross_api_consumer() noexcept -> bool {
    const auto payload = api::make();
    if (payload.text.as_str() != "native") {
        return false;
    }
    auto token = Token {carven::runtime::String::from_str(std::string_view("taken"))};
    if (api::take(std::move(token)) != 5) {
        return false;
    }
    const auto failure = api::fail();
    if (!denied(failure)) {
        return false;
    }
    const auto completed = carven::runtime::execution::drive_root(api::complete(false));
    if (completed.success_if() == nullptr
        || completed.success_if()->value.text.as_str() != "async") {
        return false;
    }
    const auto failed = carven::runtime::execution::drive_root(api::complete(true));
    return denied(failed);
}
