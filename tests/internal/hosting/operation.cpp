module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/operation.hpp>
#include <carven/runtime/execution/execution.hpp>

module carven:test.internal.hosting.operation;

import :hosting.operation;
import :test.harness.framework;
import std;

namespace {

struct Note final {
    std::string text;
    std::int32_t code;

    auto size() const noexcept -> std::int32_t { return static_cast<std::int32_t>(text.size()); }

    auto add(std::int32_t value) noexcept -> void { code += value; }

    auto take_text() noexcept -> std::string { return std::exchange(text, {}); }
};

struct Other final {
    std::int32_t code;
};

struct FirstFailure final {
    std::int32_t code;
};

struct SecondFailure final {
    std::int32_t code;
};

auto carven_type_name(FirstFailure*) noexcept -> std::string_view {
    return "probe.First";
}

auto carven_type_name(SecondFailure*) noexcept -> std::string_view {
    return "probe.Second";
}

auto carven_fields(FirstFailure*) noexcept {
    return std::make_tuple(std::pair(std::string_view("code"), &FirstFailure::code));
}

auto carven_fields(SecondFailure*) noexcept {
    return std::make_tuple(std::pair(std::string_view("code"), &SecondFailure::code));
}

template<typename... Failures>
auto fail_native() noexcept
    -> carven::runtime::execution::AsyncOperation<std::int32_t, Failures...> {
    co_return carven::runtime::execution::Completion<std::int32_t, Failures...>::failure(
        FirstFailure {.code = 17}
    );
}

auto make_note(std::string_view text) noexcept -> Note {
    return {std::string(text), 7};
}

auto make_other() noexcept -> Other {
    return {11};
}

auto consume(Note note) noexcept -> std::int32_t {
    note.text.clear();
    return note.code;
}

auto signed_value(std::int32_t value) noexcept -> std::int32_t {
    return value;
}

auto unsigned_value(std::int32_t value) noexcept -> std::uint32_t {
    return value;
}

auto copy_bytes(carven::runtime::Slice<std::uint8_t> bytes) noexcept
    -> carven::runtime::Vector<std::uint8_t> {
    return carven::runtime::Vector<std::uint8_t>::from_slice(bytes);
}

auto echo_byte(const std::uint8_t& value) noexcept -> std::uint8_t {
    return value;
}

auto bindings() noexcept -> std::vector<HostServiceBinding> {
    return {
        host_cpp_operation<make_note>("&::probe::make"),
        host_cpp_operation<make_other>("probe::other"),
        host_cpp_operation<consume>("probe::consume"),
        host_cpp_operation<&Note::code>("&probe::Note::code"),
        host_cpp_operation<&Other::code>("&probe::Other::code"),
        host_cpp_operation<&Note::size>("size"),
        host_cpp_operation<&Note::add>("&probe::Note::add"),
        host_cpp_operation<&Note::take_text>("take_text"),
        host_cpp_conversion<std::int32_t>(),
        host_cpp_conversion<std::string>(),
        host_cpp_conversion<Note>("probe::Note"),
    };
}

auto execute(HostSession& session, std::span<const HostValue> arguments = {}) noexcept
    -> std::expected<HostOutcome, HostError> {
    auto task = session.submit("run", arguments);
    if (!task) {
        return std::unexpected(task.error());
    }
    for (auto slice = 0; slice < 32 && task->result() == nullptr; ++slice) {
        session.poll(64);
    }
    if (task->result() == nullptr) {
        return std::unexpected(
            HostError {
                .kind = HostErrorKind::Execution,
                .message = "test did not finish",
                .location = {},
                .calls = {}
            }
        );
    }
    return *task->result();
}

const TestSuite suite([] static noexcept {
    "Hosting native: byte vectors retain binary data through records and native slices"_test =
        [] static noexcept {
            auto bytes = carven::runtime::Vector<std::uint8_t>::with_capacity(100'000);
            for (auto index = 0uz; index < 100'000uz; ++index) {
                bytes.push(static_cast<std::uint8_t>(index));
            }
            const auto services = std::array {
                host_cpp_operation<copy_bytes>("probe::copy_bytes"),
                host_cpp_operation<echo_byte>("probe::echo_byte"),
                host_cpp_conversion<carven::runtime::Vector<std::uint8_t>>(),
                host_cpp_conversion<std::uint8_t>(),
            };
            auto options = host_session_options();
            options.services = services;
            auto session = HostSession::load(
                "probe",
                R"(
import "probe.hpp" using probe;
struct Packet { body: Vector<u8> }
fn run(value: Packet) -> Packet {
    assert((probe::echo_byte(value.body[1]) as u8) == 1);
    let view: [u8] = value.body;
    let copied = probe::copy_bytes(view) as Vector<u8>;
    return Packet { body: copied };
}
)",
                options
            );
            if (!expect(session).note([&]() noexcept { return session.error().message; })) {
                return;
            }
            const auto arguments = std::array {HostValue(
                HostRecord {
                    .module_path = "probe",
                    .name = "Packet",
                    .fields = {{.name = "body", .value = HostValue(bytes)}}
                }
            )};
            const auto result = execute(*session, arguments);
            if (!expect(result).note([&]() noexcept { return result.error().message; })) {
                return;
            }
            const auto* value = std::get_if<HostValue>(&*result);
            const auto* packet = value ? std::get_if<HostRecord>(value) : nullptr;
            const auto* body = packet ? packet->field("body") : nullptr;
            const auto* copied =
                body ? std::get_if<carven::runtime::Vector<std::uint8_t>>(body) : nullptr;
            if (expect(copied != nullptr)) {
                expect(std::ranges::equal(*copied, bytes));
            }
        };

    "Hosting native: async failures match nominal contracts independently of carrier order"_test =
        [] static noexcept {
            const auto services = std::array {
                host_cpp_operation<fail_native<FirstFailure, SecondFailure>>("probe::first"),
                host_cpp_operation<fail_native<SecondFailure, FirstFailure>>("probe::reverse"),
            };
            auto options = host_session_options();
            options.services = services;
            auto session = HostSession::load(
                "probe",
                R"(
import std::async using Operation;
import "probe.hpp" using probe;
struct First { code: i32 }
struct Second { code: i32 }
async fn run() -> i32 {
    let first: Operation<i32, throw First + Second> = probe::first();
    let left = try { await first? } catch {
        First(error) => error.code,
        Second(_) => -1,
    };
    let reverse: Operation<i32, throw First + Second> = probe::reverse();
    let right = try { await reverse? } catch {
        First(error) => error.code,
        Second(_) => -1,
    };
    return left + right;
}
)",
                options
            );
            if (!expect(session).note([&]() noexcept { return session.error().message; })) {
                return;
            }
            const auto result = execute(*session);
            if (expect(result).note([&]() noexcept { return result.error().message; })) {
                const auto* value = std::get_if<HostValue>(&*result);
                const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
                expect(integer && *integer == 34);
            }

            auto mismatched = HostSession::load(
                "probe",
                R"(
import std::async using Operation;
import "probe.hpp" using probe;
struct First { code: i32 }
struct Other { code: i32 }
async fn run() -> i32 {
    let pending: Operation<i32, throw First + Other> = probe::first();
    return try { await pending? } catch { First(_) | Other(_) => 0 };
}
)",
                options
            );
            if (!expect(mismatched).note([&]() noexcept { return mismatched.error().message; })) {
                return;
            }
            const auto rejected = execute(*mismatched);
            expect(!rejected);
            if (!rejected) {
                expect(rejected.error().message.contains("no registered implementation"));
            }
        };

    "Hosting native: bindings survive module changes and preserve Read arguments"_test =
        [] static noexcept {
            const auto services = bindings();
            auto options = host_session_options();
            options.services = services;
            for (const auto module_path : {"first", "second"}) {
                auto session = HostSession::load(
                    module_path,
                    R"(
import "probe.hpp" using probe;
struct Box { note: probe::Note }
async fn run() -> i32 {
    let box = Box { note: probe::make("hello") };
    var note = box.note;
    let copy = probe::consume(note) as i32;
    let length = note.size() as i32;
    (&note).add(2);
    let text = (&note).take_text() as String;
    assert(text == "hello");
    assert((note.size() as i32) == 0);
    let other = probe::other();
    return copy + length + (note.code as i32) + (other.code as i32);
}
)",
                    options
                );
                if (!expect(session).note([&]() noexcept { return session.error().message; })) {
                    continue;
                }
                const auto result = execute(*session);
                if (!expect(result).note([&]() noexcept { return result.error().message; })) {
                    continue;
                }
                const auto* value = std::get_if<HostValue>(&*result);
                const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
                expect(integer && *integer == 32);
            }
        };

    "Hosting native: member carrier, access and arity must match"_test = [] static noexcept {
        const auto services = std::vector {
            host_cpp_operation<make_note>("probe::make"),
            host_cpp_operation<make_other>("probe::other"),
            host_cpp_operation<&Note::code>("code"),
            host_cpp_operation<&Note::add>("add"),
            host_cpp_conversion<std::int32_t>(),
            host_cpp_conversion<Note>("probe::Note"),
        };
        auto options = host_session_options();
        options.services = services;
        for (const auto body : {
                 "let value = ::probe::other(); return value.code as i32;",
                 "let value = ::probe::make(\"hello\"); value.add(2); return 0;",
                 "let value = ::probe::make(); return value.code as i32;",
             }) {
            auto session = HostSession::load(
                "probe",
                std::format("import \"probe.hpp\"; async fn run() -> i32 {{ {} }}", body),
                options
            );
            if (!expect(session).note([&]() noexcept { return session.error().message; })) {
                continue;
            }
            const auto result = execute(*session);
            expect(!result);
            if (!result) {
                expect_equal(result.error().kind, HostErrorKind::Execution);
                expect(result.error().message.contains("no registered implementation"));
            }
        }
    };

    "Hosting native: conflicting registrations do not silently select the first"_test =
        [] static noexcept {
            const auto services = std::vector {
                host_cpp_operation<signed_value>("probe::value"),
                host_cpp_operation<unsigned_value>("probe::value"),
                host_cpp_conversion<std::int32_t>(),
            };
            auto options = host_session_options();
            options.services = services;
            auto session = HostSession::load(
                "probe",
                R"(
import "probe.hpp";
async fn run() -> i32 => ::probe::value(3) as i32;
)",
                options
            );
            if (!expect(session).note([&]() noexcept { return session.error().message; })) {
                return;
            }
            const auto result = execute(*session);
            expect(!result);
            if (!result) {
                expect(result.error().message.contains("ambiguous bindings"));
            }
            const auto duplicate = std::vector {
                host_cpp_operation<signed_value>("probe::value"),
                host_cpp_operation<signed_value>("probe::value"),
            };
            options.services = duplicate;
            const auto rejected = HostSession::create("probe", options);
            expect(!rejected);
            if (!rejected) {
                expect_equal(rejected.error().kind, HostErrorKind::Arguments);
            }
        };
});

} // namespace
