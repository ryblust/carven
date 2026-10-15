#include "providers/generated_child.hpp"
#include "providers/generated_native.hpp"

namespace contract = generated_types::generated_record;

auto generated_record(std::int32_t mode, contract::Parameter1 local, contract::Parameter2 io)
    -> contract::Completion {
    if (mode == 1) {
        return contract::Completion::failure(local);
    }
    if (mode == 2) {
        return contract::Completion::failure(io);
    }
    return contract::Completion::success_from([]() noexcept {
        return contract::Result {
            .text = carven::runtime::String::from_str("generated"),
            .count = 42,
        };
    });
}

auto generated_void() -> void {}

auto generated_converted() -> double {
    return 42.75;
}

auto generated_write(generated_types::generated_write::Parameter0& value) -> void {
    value.count *= 6;
}

auto generated_slice(generated_types::generated_slice::Parameter0 values) -> std::int32_t {
    return values[0].count + values[1].count;
}

auto generated_links(generated_types::generated_links::Parameter0 value) -> std::int32_t {
    if (value == nullptr || value->next == nullptr || value->next->previous != value) {
        return -1;
    }
    return value->next->previous->value + value->next->value;
}

auto generated_namespace_child() noexcept -> std::int32_t {
    return 9;
}
