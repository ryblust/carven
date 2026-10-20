module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/entry.hpp>

module carven:test.internal.hosting.program;

import :test.harness.framework;
import std;

namespace {

auto expect_integer(
    const std::expected<HostOutcome, HostError>& result,
    std::int32_t expected
) noexcept -> void {
    if (!expect(result).note([&]() noexcept { return result.error().message; })) {
        return;
    }
    const auto* value = std::get_if<HostValue>(&*result);
    const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (expect(integer != nullptr)) {
        expect_equal(*integer, expected);
    }
}

auto expect_task(HostSession& session, const HostTask& task, std::int32_t value) noexcept -> void {
    for (auto slice = 0; slice < 32 && task.result() == nullptr; ++slice) {
        session.poll(64);
    }
    if (expect(task.result() != nullptr)) {
        expect_integer(*task.result(), value);
    }
}

auto compiled_value() noexcept -> std::int32_t {
    return 7;
}

auto combined_byte_count(
    const carven::runtime::Vector<std::uint8_t>& first,
    const carven::runtime::Vector<std::uint8_t>& second
) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(first.size() + second.size());
}

const TestSuite suite([] static noexcept {
    "Hosting calls: byte work is shared across arguments and resets after rejection"_test =
        [] static noexcept {
            const auto entries = std::array {host_entry<combined_byte_count>("count")};
            auto options = host_session_options();
            options.entries = entries;
            options.calls.limits.byte_work = 12;
            auto session = HostSession::create("app", options);
            if (!expect(session)) {
                return;
            }
            auto bytes = carven::runtime::Vector<std::uint8_t>::with_capacity(8);
            for (auto index = 0uz; index < 8uz; ++index) {
                bytes.push(static_cast<std::uint8_t>(index));
            }
            const auto check = [&]() noexcept {
                auto arguments = std::array {HostValue(bytes), HostValue(bytes)};
                const auto rejected = session->submit("count", arguments);
                if (expect(!rejected)) {
                    expect_equal(rejected.error().kind, HostErrorKind::Arguments);
                }
                arguments[0] = carven::runtime::Vector<std::uint8_t>(std::uint8_t {7});
                arguments[1] = carven::runtime::Vector<std::uint8_t>();
                const auto accepted = session->submit("count", arguments);
                if (expect(accepted)) {
                    expect_task(*session, *accepted, 1);
                }
            };
            check();
            const auto replacement = session->reload(R"(
fn count(first: Vector<u8>, second: Vector<u8>) -> i32 {
    return (first.length() + second.length()) as i32;
}
)");
            if (expect(replacement)) {
                check();
            }
        };

    "Hosting lookup: selected modules admit bare functions and retain private helpers"_test =
        [] static noexcept {
            for (const auto module_path : {"app", "crafts.demo.app"}) {
                const auto program = HostProgram::load(module_path, R"(
private fn hidden() -> i32 => 7;
fn value() -> i32 => hidden();
export fn published() -> i32 => value() + 2;
)");
                if (!expect(program).note([&]() noexcept { return program.error().message; })) {
                    continue;
                }
                const auto value = program->function("value");
                if (expect(value)) {
                    expect_integer(value->call({}, host_call_options()), 7);
                }
                const auto published = program->function("published");
                if (expect(published)) {
                    expect_integer(published->call({}, host_call_options()), 9);
                }
                for (const auto name : {"hidden", "missing"}) {
                    const auto rejected = program->function(name);
                    if (expect(!rejected)) {
                        expect_equal(rejected.error().kind, HostErrorKind::Lookup);
                    }
                }
                auto session = HostSession::create(*program, host_session_options());
                if (!expect(session)) {
                    continue;
                }
                const auto task = session->submit("value", {});
                if (expect(task)) {
                    expect_task(*session, *task, 7);
                }
                const auto hidden = session->submit("hidden", {});
                if (expect(!hidden)) {
                    expect_equal(hidden.error().kind, HostErrorKind::Lookup);
                }
            }
        };

    "Hosting lookup: source imports retain craft visibility and entry-module boundaries"_test =
        [] static noexcept {
            for (const auto exported : {false, true}) {
                const auto sources = std::array {
                    HostSource {
                        .module_path = "app",
                        .origin = "app.cv",
                        .text = "import demo::api using value; fn run() -> i32 => value();"
                    },
                    HostSource {
                        .module_path = "crafts.demo.api",
                        .origin = "crafts/demo/api.cv",
                        .text = std::string(exported ? "export " : "") + "fn value() -> i32 => 42;"
                    },
                    HostSource {
                        .module_path = "peer",
                        .origin = "peer.cv",
                        .text = "export fn elsewhere() -> i32 => 99;"
                    },
                };
                const auto program = HostProgram::load("app", sources);
                if (!exported) {
                    if (expect(!program)) {
                        expect_equal(program.error().kind, HostErrorKind::Analysis);
                    }
                    continue;
                }
                if (!expect(program).note([&]() noexcept { return program.error().message; })) {
                    continue;
                }
                const auto run = program->function("run");
                if (expect(run)) {
                    expect_integer(run->call({}, host_call_options()), 42);
                }
                for (const auto name : {"value", "elsewhere"}) {
                    const auto rejected = program->function(name);
                    if (expect(!rejected)) {
                        expect_equal(rejected.error().kind, HostErrorKind::Lookup);
                    }
                }
            }
        };

    "Hosting sources: dependencies extend every explicit root"_test = [] static noexcept {
        const auto sources = std::array {
            HostSource {
                .module_path = "app",
                .origin = "app.cv",
                .text = "import demo::api using value; fn run() -> i32 => value();"
            },
            HostSource {
                .module_path = "peer",
                .origin = "peer.cv",
                .text = "const { print(\"explicit\"); }"
            },
        };
        const auto available = std::array {
            HostSource {
                .module_path = "crafts.demo.api",
                .origin = "crafts/demo/api.cv",
                .text = "import .helper using answer; export fn value() -> i32 => answer();"
            },
            HostSource {
                .module_path = "crafts.demo.helper",
                .origin = "crafts/demo/helper.cv",
                .text = "const { print(\"dependency\"); } fn answer() -> i32 => 42;"
            },
            HostSource {
                .module_path = "crafts.demo.unused",
                .origin = "crafts/demo/unused.cv",
                .text = "const { print(\"unused\"); }"
            },
        };
        auto requested = std::set<std::string>();
        auto resolve = [&](
                           std::string_view module_path
                       ) noexcept -> std::expected<std::optional<HostSource>, HostError> {
            requested.emplace(module_path);
            const auto found = std::ranges::find(available, module_path, &HostSource::module_path);
            return found == available.end() ? std::nullopt : std::optional(*found);
        };
        auto output = std::string();
        const auto sink = HostOutput {
            .context = std::addressof(output),
            .write = [](void* context, bool, std::string_view bytes) static noexcept {
                static_cast<std::string*>(context)->append(bytes);
            },
        };
        const auto program = HostProgram::load(
            "app",
            sources,
            sink,
            {.context = &resolve,
             .resolve = [](void* context, std::string_view module_path) static noexcept {
                 return (*static_cast<decltype(resolve)*>(context))(module_path);
             }}
        );
        if (!expect(program).note([&]() noexcept { return program.error().message; })) {
            return;
        }
        expect(requested == std::set<std::string> {"crafts.demo.api", "crafts.demo.helper"});
        expect(output.contains("explicit"));
        expect(output.contains("dependency"));
        expect(!output.contains("unused"));
        const auto run = program->function("run");
        if (expect(run)) {
            expect_integer(run->call({}, host_call_options()), 42);
        }
    };

    "Hosting reload: bare replacements preserve contracts and admitted calls"_test =
        [] static noexcept {
            const auto entries = std::array {host_entry<compiled_value>("value")};
            auto options = host_session_options();
            options.entries = entries;
            auto session = HostSession::create("app", options);
            if (!expect(session)) {
                return;
            }
            const auto original = session->submit("value", {});
            if (!expect(original)) {
                return;
            }
            const auto replaced = session->reload(R"(
private fn increment() -> i32 => 2;
fn value() -> i32 => 40 + increment();
)");
            if (!expect(replaced).note([&]() noexcept { return replaced.error().message; })) {
                return;
            }
            const auto replacement = session->submit("value", {});
            if (!expect(replacement)) {
                return;
            }
            expect_task(*session, *original, 7);
            expect_task(*session, *replacement, 42);

            const auto incompatible = session->reload("fn value() -> bool => true;");
            if (expect(!incompatible)) {
                expect_equal(incompatible.error().kind, HostErrorKind::Signature);
            }
            const auto retained = session->submit("value", {});
            if (expect(retained)) {
                expect_task(*session, *retained, 42);
            }

            const auto private_source = session->reload("private fn value() -> bool => true;");
            if (!expect(private_source)) {
                return;
            }
            const auto unchanged = session->submit("value", {});
            if (expect(unchanged)) {
                expect_task(*session, *unchanged, 42);
            }
            const auto published = session->reload("export fn value() -> i32 => 43;");
            if (!expect(published)) {
                return;
            }
            const auto published_task = session->submit("value", {});
            if (expect(published_task)) {
                expect_task(*session, *published_task, 43);
            }
        };
});

} // namespace
