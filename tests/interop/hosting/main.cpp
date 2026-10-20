#include <carven/hosting/api.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <fstream>
#include <iostream>
#include <iterator>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

auto integer_result(
    const std::expected<HostOutcome, HostError>& result,
    std::int32_t expected
) noexcept -> bool {
    const auto* value = result ? std::get_if<HostValue>(&*result) : nullptr;
    const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
    return integer != nullptr && *integer == expected;
}

} // namespace

auto main() -> int {
    auto file = std::ifstream("tests/interop/hosting/program.cv");
    if (!file) {
        return 1;
    }
    const auto text = std::string(std::istreambuf_iterator<char>(file), {});
    auto output = std::string();
    const auto sink = HostOutput {
        .context = &output,
        .write = [](void* context, bool, std::string_view bytes) noexcept {
            static_cast<std::string*>(context)->append(bytes);
        },
    };
    auto program = HostProgram::load("app", text, sink);
    if (!program) {
        std::println(std::cerr, "{}", program.error().message);
        return 1;
    }
    const auto function = program->function("calculate");
    if (!function) {
        return 1;
    }
    auto options = host_call_options();
    options.output = sink;
    auto arguments = std::array {HostValue(std::int32_t {4}), HostValue(false)};
    if (!integer_result(function->call(arguments, options), 9)) {
        return 1;
    }
    arguments[0] = std::int32_t {10};
    if (!integer_result(function->call(arguments, options), 21) || output != "load;call;call;") {
        return 1;
    }
    const auto wrong_arguments = std::array {HostValue(true), HostValue(false)};
    const auto invalid = function->call(wrong_arguments, options);
    if (invalid
        || invalid.error().kind != HostErrorKind::Arguments
        || output != "load;call;call;") {
        return 1;
    }
    arguments[1] = true;
    const auto rejected = function->call(arguments, options);
    const auto* failure = rejected ? std::get_if<HostFailure>(&*rejected) : nullptr;
    if (failure == nullptr || failure->module_path() != "app" || failure->name() != "Rejected") {
        return 1;
    }
    arguments[1] = false;
    if (!integer_result(function->call(arguments, options), 21)) {
        return 1;
    }
    const auto nominals = HostProgram::load("nominal", R"(
        struct Tag<T> {}
        struct Nested { value: Tag<i32>, }
        struct Node { children: Vector<Node> }
        fn echo(value: Node) -> Node => value;
        fn direct(_value: Tag<i32>) -> i32 => 0;
        fn nested(_value: Nested) -> i32 => 0;
    )");
    if (!nominals) {
        std::println(std::cerr, "{}", nominals.error().message);
        return 1;
    }
    for (const auto name : {"direct", "nested"}) {
        const auto unsupported = nominals->function(name);
        if (unsupported || unsupported.error().kind != HostErrorKind::Signature) {
            return 1;
        }
    }
    const auto echo = nominals->function("echo");
    if (!echo) {
        return 1;
    }
    const auto node = std::array {HostValue(
        HostRecord {
            .module_path = "nominal",
            .name = "Node",
            .fields = {{.name = "children", .value = std::vector<HostValue>()}}
        }
    )};
    const auto echoed = echo->call(node, options);
    const auto* echoed_value = echoed ? std::get_if<HostValue>(&*echoed) : nullptr;
    const auto* record = echoed_value ? std::get_if<HostRecord>(echoed_value) : nullptr;
    if (!record
        || record->module_path != "nominal"
        || record->name != "Node"
        || record->fields.size() != 1
        || record->fields[0].name != "children") {
        return 1;
    }
    const auto* children = std::get_if<std::vector<HostValue>>(&record->fields[0].value);
    if (!children || !children->empty()) {
        return 1;
    }
    std::println("scalar: 9, 21; rejected, recovered");
    return 0;
}
