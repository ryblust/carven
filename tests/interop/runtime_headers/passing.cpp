#include <carven/runtime/passing.hpp>

namespace {
struct Value final {
    int number;
};
static_assert(std::is_same_v<carven::runtime::ReadArg<Value>, const Value>);
static_assert(
    std::is_same_v<decltype(carven::runtime::transfer(std::declval<Value&>())), const Value&>
);
}
