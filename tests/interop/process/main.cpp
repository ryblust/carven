module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/standard.hpp>
#include <carven/std/process/native.hpp>

module carven:test.interop.process.main;

import std;

namespace {

auto read_source(const std::filesystem::path& path) noexcept -> std::string {
    auto input = std::ifstream(path);
    return {std::istreambuf_iterator<char>(input), {}};
}

auto standard_source(void*, std::string_view module_path) noexcept
    -> std::expected<std::optional<HostSource>, HostError> {
    constexpr auto prefix = std::string_view("crafts.carven.std.");
    if (!module_path.starts_with(prefix)) {
        return std::nullopt;
    }
    auto relative = std::string(module_path.substr(prefix.size()));
    std::ranges::replace(relative, '.', '/');
    const auto filename = std::format("crafts/carven/std/{}.cv", relative);
    auto file = std::ifstream(filename, std::ios::binary);
    if (!file) {
        return std::unexpected(hosting_error(HostErrorKind::Input, "cannot read " + filename));
    }
    return HostSource {
        .module_path = std::string(module_path),
        .origin = filename,
        .text = std::string(std::istreambuf_iterator<char>(file), {})
    };
}

auto call(
    HostSession& session,
    std::string_view name,
    std::span<const HostValue> arguments = {}
) noexcept -> std::expected<HostOutcome, HostError> {
    auto task = session.submit(name, arguments);
    if (!task) {
        return std::unexpected(std::move(task.error()));
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!task->result() && std::chrono::steady_clock::now() < deadline) {
        if (session.poll(64) == HostSessionStatus::Waiting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (const auto* result = task->result()) {
        return *result;
    }
    return std::unexpected(
        HostError {
            .kind = HostErrorKind::Execution,
            .message = "hosted process call timed out",
            .location = std::nullopt,
            .calls = {},
        }
    );
}

} // namespace

// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
extern "C++" auto main(int argc, char** argv) noexcept -> int {
    if (argc != 4) {
        return 1;
    }
    const auto executable = carven::standard::process::executable();
    const auto path_from_text = [](std::string_view text) noexcept {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    };
    const auto selected_program = carven::standard::process::find_executable(argv[1]);
    if (executable.code != 0
        || !path_from_text(executable.text.as_str()).is_absolute()
        || selected_program.code != 0
        || path_from_text(selected_program.text.as_str()).filename()
            != path_from_text(argv[1]).filename()) {
        std::println(
            std::cerr,
            "process queries must return absolute paths and preserve executable names"
        );
        return 1;
    }
    auto sources = std::array {HostSource {
        .module_path = "probe",
        .origin = "tests/crafts/process/bridge.cv",
        .text = "import std::fs using { FileError, read_text }\n"
            + read_source("tests/crafts/process/bridge.cv")
    }};
    // Loading optional native declarations is safe; invoking one still needs a provider.
    sources.back().text += R"(
private import(cpp) fn unavailable_native() -> i32 = ::probe::unavailable;
async fn missing_provider() -> i32 => unavailable_native();
fn host_environment() -> String throw ProcessError => environment("PATH")?;
fn missing_file() -> String throw FileError => read_text("carven-test-hosting-missing-file")?;
)";
    const auto services = host_standard_services();
    auto options = host_session_options();
    options.services = services;
    const auto dependencies = HostSourceResolver {.context = nullptr, .resolve = standard_source};
    auto restricted = HostSession::load("probe", sources, options, dependencies);
    if (!restricted) {
        std::println(std::cerr, "{}", restricted.error().message);
        return 1;
    }
    const auto platform_result = call(*restricted, "host_platform");
    const auto* platform_value =
        platform_result ? std::get_if<HostValue>(&*platform_result) : nullptr;
    const auto* platform_case = platform_value ? std::get_if<HostEnum>(platform_value) : nullptr;
#if defined(_WIN32)
    constexpr auto expected_platform = "Windows";
#elif defined(__APPLE__)
    constexpr auto expected_platform = "MacOS";
#elif defined(__linux__)
    constexpr auto expected_platform = "Linux";
#else
    constexpr auto expected_platform = "Other";
#endif
    if (!platform_case || platform_case->case_name != expected_platform) {
        std::println(std::cerr, "operating_system must identify the host platform");
        return 1;
    }
    const auto blocked = call(*restricted, "host_environment");
    if (blocked || blocked.error().kind != HostErrorKind::Execution) {
        std::println(std::cerr, "host effects must be enabled before execution");
        return 1;
    }
    options.calls.allow_host_effects = true;
    auto session = HostSession::load("probe", sources, options, dependencies);
    if (!session) {
        std::println(std::cerr, "{}", session.error().message);
        return 1;
    }
    const auto permitted = call(*session, "host_environment");
    if (!permitted) {
        std::println(std::cerr, "{}", permitted.error().message);
        return 1;
    }
    const auto* environment_value = std::get_if<HostValue>(&*permitted);
    const auto* text = environment_value ? std::get_if<std::string>(environment_value) : nullptr;
    const auto* path = std::getenv("PATH");
    if (!text || *text != (path ? path : "")) {
        std::println(std::cerr, "enabled host effects must read the host environment");
        return 1;
    }
    const auto absent = call(*session, "missing_file");
    const auto* failure = absent ? std::get_if<HostFailure>(&*absent) : nullptr;
    const auto* payload = failure ? std::get_if<HostEnum>(&failure->payload()) : nullptr;
    const auto* code = payload && payload->payload.size() == 1
        ? std::get_if<std::int32_t>(&payload->payload[0])
        : nullptr;
    if (!payload
        || payload->name != "FileError"
        || payload->case_name != "System"
        || !code
        || *code == 0) {
        std::println(std::cerr, "filesystem failures must retain their enum case and payload");
        return 1;
    }
    const auto denied = call(*session, "missing_provider");
    if (denied || denied.error().kind != HostErrorKind::Execution) {
        std::println(std::cerr, "unregistered provider did not fail at its call site");
        return 1;
    }
    const auto arguments = std::array {HostValue(std::string(argv[1]))};
    const auto result = call(*session, "probe", arguments);
    if (!result) {
        std::println(std::cerr, "{}", result.error().message);
        return 1;
    }
    const auto* value = std::get_if<HostValue>(&*result);
    const auto* status = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (!status || *status != 7) {
        std::println(std::cerr, "unexpected process result");
        return 1;
    }
    std::println("process: roundtrip, cancellation, shared handle, exit status");
    return 0;
}
