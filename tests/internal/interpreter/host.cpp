module;

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

module carven:test.internal.interpreter.host;

import :diagnostics.code;
import :frontend.program.parse;
import :interpreter.execute;
import :semantic.analyze;
import :semantic.evaluation.execution;
import :semantic.semir.decl;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import :test.internal.source.fixture;
import std;

namespace {

#if !defined(_WIN32)

auto run_program(
    std::string_view text,
    ExecutionOutput output,
    InterpreterOptions options = {.limits = static_execution_limits(), .trace = {}, .report = {}}
) noexcept -> bool {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("host.cv", std::string(text));
    const auto path = CanonicalModulePath::from_value("host");
    if (!source || !path) {
        return false;
    }
    auto inputs = std::vector {SourceModuleInput {.source_id = *source, .module_path = *path}};
    constexpr auto modules = std::to_array<std::string_view>({
        "io",
        "time",
        "utf",
        "utf.scan",
        "utf.block",
        "simd.bytes",
    });
    append_standard_sources(sources, inputs, modules);
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    if (!parsed) {
        return false;
    }
    auto analyzed = analyze(std::move(*parsed), output, {}, {}, HostEffects::Allow);
    if (!analyzed) {
        for (const auto& diagnostic : analyzed.error()) {
            std::println(
                std::cerr,
                "{}: {}",
                diagnostic_code_info(diagnostic.finding.code).name,
                diagnostic.finding.message
            );
        }
        return false;
    }
    for (const auto row : analyzed->value.declarations().functions()) {
        if (row.value.entry_point) {
            return interpret(analyzed->value, row.id, output, options).has_value();
        }
    }
    return false;
}

// Input cursor state belongs to a process. Each scenario starts before touching stdin.
template<typename Action>
auto isolated(Action action) noexcept -> int {
    const auto child = fork();
    if (child == 0) {
        _exit(action());
    }
    if (child < 0) {
        return 125;
    }
    auto status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (true) {
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
        if (waited < 0 && errno != EINTR) {
            return 125;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            static_cast<void>(kill(child, SIGKILL));
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            return 124;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

auto write_bytes(int descriptor, std::string_view bytes) noexcept -> bool {
    while (!bytes.empty()) {
        const auto written = write(descriptor, bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

const TestSuite suite([] static noexcept {
    "Interpreter host: failed input result construction preserves unread bytes"_test =
        [] static noexcept {
            const auto result = isolated([] static noexcept -> int {
                auto descriptors = std::array<int, 2>();
                if (pipe(descriptors.data()) != 0) {
                    return 1;
                }
                if (dup2(descriptors[0], STDIN_FILENO) < 0) {
                    return 2;
                }
                close(descriptors[0]);
                if (!write_bytes(descriptors[1], std::string(4096, 'x'))) {
                    return 3;
                }
                close(descriptors[1]);
                auto observed = std::string();
                const auto output = [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    observed.append(bytes);
                };
                auto limited = false;
                const auto report = [&](std::optional<TestID>,
                                        const ExecutionEvent& event) noexcept {
                    limited = limited || event.reason() == ExecutionReason::Limit;
                };
                auto limits = static_execution_limits();
                limits.steps = 2000;
                const auto options =
                    InterpreterOptions {.limits = limits, .trace = {}, .report = report};
                const auto ran = run_program(
                    R"(
import std::io using IOError;
async fn main() throw IOError { let _ = await stdin().read(4096)?; }
)",
                    output,
                    options
                );
                if (ran || !limited) {
                    return 4;
                }
                const auto recovered = run_program(
                    R"(
import std::io using IOError;
async fn main() throw IOError {
    let retained = match await stdin().read(1)? {
        .Data(bytes) => bytes.length() == 1 && bytes[0] == 120,
        .End => false,
    };
    assert(retained);
    println("retained input");
}
)",
                    output,
                    options
                );
                return recovered && observed == "retained input\n" ? 0 : 5;
            });
            expect_equal(result, 0);
        };

    "Interpreter host: timeout retains partial input and static prefetch continues at runtime"_test =
        [] static noexcept {
            const auto result = isolated([] static noexcept -> int {
                auto descriptors = std::array<int, 2>();
                if (pipe(descriptors.data()) != 0) {
                    return 1;
                }
                if (dup2(descriptors[0], STDIN_FILENO) < 0) {
                    return 2;
                }
                close(descriptors[0]);
                if (!write_bytes(descriptors[1], "part")) {
                    return 3;
                }
                auto timed_out = std::atomic_bool(false);
                auto writer_ok = std::atomic_bool(false);
                auto writer = std::jthread([&](std::stop_token stop) noexcept {
                    while (!timed_out.load()) {
                        if (stop.stop_requested()) {
                            return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    writer_ok = write_bytes(descriptors[1], "ial\nnext\n");
                    close(descriptors[1]);
                });
                auto observed = std::string();
                const auto output = [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    observed.append(bytes);
                    if (observed == "expired\n") {
                        timed_out = true;
                    }
                };
                const auto ran = run_program(
                    R"(
import std::io using { IOError, Read };
import std::time using { RangeError, Timeout, timeout };
const async fn one() -> String throw IOError {
    return match await stdin().read_line()? { .Data(text) => text, .End => "" };
}
const async fn capture() -> String throw IOError + RangeError {
    let expired = try { let _ = await timeout(one(), 1ms)?; false }
        catch { Timeout(_) => true, IOError(_) => false, RangeError(_) => false };
    assert(expired);
    println("expired");
    return await one()?;
}
const captured = await capture()?;
const { assert(captured == "partial"); println("static", captured); }
async fn main() throw IOError { println("runtime", await one()?); }
)",
                    output
                );
                writer.request_stop();
                writer.join();
                if (!ran) {
                    return 4;
                }
                if (!writer_ok) {
                    return 5;
                }
                return observed == "expired\nstatic partial\nruntime next\n" ? 0 : 6;
            });
            expect_equal(result, 0);
        };

    "Interpreter host: standard input system failures use the source IOError contract"_test =
        [] static noexcept {
            const auto result = isolated([] static noexcept -> int {
                close(STDIN_FILENO);
                auto observed = std::string();
                const auto output = [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    observed.append(bytes);
                };
                const auto ran = run_program(
                    R"(
import std::io using IOError;
const async fn probe() -> bool {
    let failed = try { let _ = await stdin().read_all(8)?; false }
        catch { IOError(error) => match error { .System(code) => code != 0, _ => false } };
    let empty = try {
        let result = match await stdin().read(0)? {
            .Data(bytes) => bytes.is_empty(), .End => false
        };
        result
    } catch { IOError(_) => false };
    return failed && empty;
}
const rejected = await probe();
const { assert(rejected); println("static system"); }
async fn main() { assert(await probe()); println("runtime system"); }
)",
                    output
                );
                return ran && observed == "static system\nruntime system\n" ? 0 : 1;
            });
            expect_equal(result, 0);
        };
});

#endif

} // namespace
