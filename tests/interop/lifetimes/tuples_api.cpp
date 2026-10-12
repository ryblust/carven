#include <carven/runtime/value/void.hpp>

#include <carven/api/tests/interop/lifetimes/tuples.hpp>

#include <type_traits>

namespace api = carven::api::tests::interop::lifetimes::tuples;

static_assert(
    std::is_same_v<decltype(api::consume_void), auto(carven::runtime::Void) noexcept -> void>
);
