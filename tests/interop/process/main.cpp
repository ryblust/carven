module;

#include <carven/hosting/api.hpp>
#include <carven/std/process/native.hpp>

module carven:test.interop.process.main;

import std;

namespace {

auto read_source(const std::filesystem::path& path) noexcept -> std::string {
    auto input = std::ifstream(path);
    return {std::istreambuf_iterator<char>(input), {}};
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
    auto sources = std::vector<HostSource>();
    for (const auto& entry : std::filesystem::recursive_directory_iterator("crafts/carven")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cv") {
            continue;
        }
        auto module_path = entry.path();
        module_path.replace_extension();
        auto name = module_path.generic_string();
        std::ranges::replace(name, '/', '.');
        sources.push_back(
            {.module_path = std::move(name),
             .origin = entry.path().generic_string(),
             .text = read_source(entry.path())}
        );
    }
    std::ranges::sort(sources, {}, &HostSource::module_path);
    sources.push_back(
        {.module_path = "probe",
         .origin = "tests/crafts/process/bridge.cv",
         .text = "import std::fs using { FileError, read_text }\n"
             + read_source("tests/crafts/process/bridge.cv")}
    );
    // Loading optional native declarations is safe; invoking one still needs a provider.
    sources.back().text += R"(
private import(cpp) fn unavailable_native() -> i32 = ::probe::unavailable;
async fn missing_provider() -> i32 => unavailable_native();
fn host_environment() -> String throw ProcessError => environment("PATH")?;
fn missing_file() -> String throw FileError => read_text("carven-test-hosting-missing-file")?;
)";
    auto program = HostProgram::load("probe", sources);
    if (!program) {
        std::println(std::cerr, "{}", program.error().message);
        return 1;
    }
    const auto platform = program->function("host_platform");
    if (!platform) {
        std::println(std::cerr, "{}", platform.error().message);
        return 1;
    }
    const auto platform_result = platform->call({}, host_call_options());
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
    auto environment = program->function("host_environment");
    if (!environment) {
        std::println(std::cerr, "{}", environment.error().message);
        return 1;
    }
    auto call_options = host_call_options();
    const auto blocked = environment->call({}, call_options);
    if (blocked || blocked.error().kind != HostErrorKind::Execution) {
        std::println(std::cerr, "host effects must be enabled before execution");
        return 1;
    }
    call_options.allow_host_effects = true;
    const auto permitted = environment->call({}, call_options);
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
    const auto missing_file = program->function("missing_file");
    if (!missing_file) {
        std::println(std::cerr, "{}", missing_file.error().message);
        return 1;
    }
    const auto absent = missing_file->call({}, call_options);
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
    auto options = host_session_options();
    options.calls.allow_host_effects = true;
    auto session = HostSession::create(*program, options);
    if (!session) {
        std::println(std::cerr, "{}", session.error().message);
        return 1;
    }
    auto missing = session->submit("missing_provider", {});
    if (!missing) {
        std::println(std::cerr, "{}", missing.error().message);
        return 1;
    }
    session->poll(64);
    const auto* denied = missing->result();
    if (!denied || *denied || denied->error().kind != HostErrorKind::Execution) {
        std::println(std::cerr, "unregistered provider did not fail at its call site");
        return 1;
    }
    const auto arguments = std::array {HostValue(std::string(argv[1]))};
    auto task = session->submit("probe", arguments);
    if (!task) {
        std::println(std::cerr, "{}", task.error().message);
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!task->result() && std::chrono::steady_clock::now() < deadline) {
        if (session->poll(64) == HostSessionStatus::Waiting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const auto* result = task->result();
    if (!result) {
        std::println(std::cerr, "process bridge timed out");
        return 1;
    }
    if (!*result) {
        std::println(std::cerr, "{}", result->error().message);
        return 1;
    }
    const auto* value = std::get_if<HostValue>(&**result);
    const auto* status = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (!status || *status != 7) {
        std::println(std::cerr, "unexpected process result");
        return 1;
    }
    std::println("process: roundtrip, cancellation, shared handle, exit status");
    return 0;
}
