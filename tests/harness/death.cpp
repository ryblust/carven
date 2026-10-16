module;
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include <cerrno>
#include <csignal>
#include <cstdio>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

module carven:test.harness.death.impl;

import :support.function_ref;
import :test.harness.death;
import :test.harness.framework;
import std;

namespace {

struct DeathResult final {
    bool aborted;
    std::string output;
    std::string error;
};

using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
constexpr auto timeout = std::chrono::milliseconds(30000);

[[noreturn]] auto setup_failed(int) noexcept -> void {
    std::_Exit(125);
}

[[noreturn]] auto run_body(TestFunction body) noexcept -> void {
    if (std::signal(SIGABRT, setup_failed) == SIG_ERR) {
        std::_Exit(125);
    }
    body();
    std::fflush(nullptr);
    std::_Exit(0);
}

#if defined(_WIN32)

constexpr auto child_environment_name = "CARVEN_TEST_DEATH";
auto termination_event = HANDLE {};

auto record_abort(int signal) noexcept -> void {
    if (signal == SIGABRT && SetEvent(termination_event) != FALSE) {
        std::_Exit(73);
    }
    std::_Exit(125);
}

auto inherited_event() noexcept -> HANDLE {
    const auto* text = std::getenv(child_environment_name);
    if (text == nullptr) {
        return nullptr;
    }
    auto value = std::uintptr_t {};
    const auto input = std::string_view(text);
    const auto end = text + input.size();
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end || value == 0u) {
        return nullptr;
    }
    return reinterpret_cast<HANDLE>(value);
}

auto child_environment(HANDLE event) noexcept -> std::optional<std::wstring> {
    const auto inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) {
        return std::nullopt;
    }
    const auto name = std::wstring(
        child_environment_name,
        child_environment_name + std::char_traits<char>::length(child_environment_name)
    );
    auto position = inherited;
    while (*position != L'\0') {
        const auto entry = std::wstring_view(position);
        if (CompareStringOrdinal(
                name.data(),
                static_cast<int>(name.size()),
                entry.data(),
                static_cast<int>(entry.find(L'=', 1)),
                TRUE
            )
            == CSTR_LESS_THAN) {
            break;
        }
        position += entry.size() + 1;
    }
    auto result = std::wstring(inherited, static_cast<std::size_t>(position - inherited));
    result += name + L"=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(event));
    result.push_back(L'\0');
    while (*position != L'\0') {
        const auto entry = std::wstring_view(position);
        result.append(entry);
        result.push_back(L'\0');
        position += entry.size() + 1;
    }
    result.push_back(L'\0');
    FreeEnvironmentStringsW(inherited);
    return result;
}

auto utf8_to_wide(std::string_view value) noexcept -> std::optional<std::wstring> {
    if (value.empty()) {
        return std::wstring();
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const auto size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        return std::nullopt;
    }
    auto result = std::wstring(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size
        )
        != size) {
        return std::nullopt;
    }
    return result;
}

auto current_executable() noexcept -> std::optional<std::wstring> {
    auto buffer = std::vector<wchar_t>(1024);
    while (buffer.size() <= 32768) {
        const auto size =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0u) {
            return std::nullopt;
        }
        if (static_cast<std::size_t>(size) < buffer.size()) {
            return std::wstring(buffer.data(), static_cast<std::size_t>(size));
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::nullopt;
}

auto append_windows_argument(std::wstring& command, std::wstring_view argument) noexcept -> void {
    if (!command.empty()) {
        command.push_back(L' ');
    }
    command.push_back(L'"');
    auto backslashes = std::size_t {};
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            command.append(backslashes * 2 + 1, L'\\');
            command.push_back(character);
            backslashes = 0;
            continue;
        }
        command.append(backslashes, L'\\');
        backslashes = 0;
        command.push_back(character);
    }
    command.append(backslashes * 2, L'\\');
    command.push_back(L'"');
}

auto run_child(
    std::string_view test_case,
    std::FILE* output,
    std::FILE* error,
    HANDLE event
) noexcept -> bool {
    const auto executable = current_executable();
    const auto wide_test_case = utf8_to_wide(test_case);
    auto environment = child_environment(event);
    if (!executable || !wide_test_case || !environment) {
        return false;
    }

    auto command = std::wstring();
    append_windows_argument(command, *executable);
    append_windows_argument(command, L"--test");
    append_windows_argument(command, *wide_test_case);

    const auto input = File(std::fopen("NUL", "rb"), std::fclose);
    if (!input) {
        return false;
    }
    auto startup = STARTUPINFOW {};
    startup.cb = static_cast<DWORD>(sizeof(STARTUPINFOW));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(input.get())));
    startup.hStdOutput = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(output)));
    startup.hStdError = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(error)));
    for (const auto handle : {startup.hStdInput, startup.hStdOutput, startup.hStdError}) {
        if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            return false;
        }
    }
    auto process = PROCESS_INFORMATION {};
    if (!CreateProcessW(
            executable->c_str(),
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            environment->data(),
            nullptr,
            &startup,
            &process
        )) {
        return false;
    }
    CloseHandle(process.hThread);

    const auto wait = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count()));
    if (wait != WAIT_OBJECT_0) {
        static_cast<void>(TerminateProcess(process.hProcess, 1u));
        static_cast<void>(WaitForSingleObject(process.hProcess, INFINITE));
        CloseHandle(process.hProcess);
        return false;
    }
    auto exit_code = DWORD {};
    const auto read_exit = GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
    CloseHandle(process.hProcess);
    return read_exit && exit_code == 73u;
}

auto expect_windows_termination(
    std::string_view test_case,
    std::FILE* output,
    std::FILE* error
) noexcept -> bool {
    auto security = SECURITY_ATTRIBUTES {};
    security.nLength = static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES));
    security.bInheritHandle = TRUE;
    const auto event = CreateEventW(&security, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }

    const auto aborted = run_child(test_case, output, error, event);
    const auto observed_abort = WaitForSingleObject(event, 0u) == WAIT_OBJECT_0;
    CloseHandle(event);
    return aborted && observed_abort;
}

#else

auto expect_posix_termination(TestFunction body, std::FILE* output, std::FILE* error) noexcept
    -> bool {
    std::fflush(nullptr);
    const auto child = fork();
    if (child == 0) {
        if (std::freopen("/dev/null", "rb", stdin) == nullptr
            || dup2(fileno(output), STDOUT_FILENO) < 0
            || dup2(fileno(error), STDERR_FILENO) < 0) {
            std::_Exit(125);
        }
        run_body(body);
    }
    if (child < 0) {
        return false;
    }

    auto status = 0;
    auto waited = pid_t {};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        waited = waitpid(child, &status, WNOHANG);
        if (waited == child || (waited < 0 && errno != EINTR)) {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            static_cast<void>(kill(child, SIGKILL));
            do {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
            // A timeout is a harness failure, not evidence of expected termination.
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return waited == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

#endif

auto temporary_stream() noexcept -> File {
#if defined(_WIN32)
    // The Windows CRT's tmpfile uses the drive root, not the user's temporary directory.
    auto directory = std::array<wchar_t, MAX_PATH + 1>();
    const auto size = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    if (size == 0 || size >= directory.size()) {
        return File(nullptr, std::fclose);
    }
    auto name = std::array<wchar_t, MAX_PATH>();
    if (!GetTempFileNameW(directory.data(), L"cv", 0, name.data())) {
        return File(nullptr, std::fclose);
    }
    auto stream = File(_wfopen(name.data(), L"w+bD"), std::fclose);
    if (!stream) {
        DeleteFileW(name.data());
    }
    return stream;
#else
    return File(std::tmpfile(), std::fclose);
#endif
}

auto read_stream(std::FILE* stream) noexcept -> std::optional<std::string> {
    if (std::fseek(stream, 0, SEEK_SET) != 0) {
        return std::nullopt;
    }
    auto text = std::string();
    auto buffer = std::array<char, 4096>();
    while (const auto size = std::fread(buffer.data(), 1, buffer.size(), stream)) {
        text.append(buffer.data(), size);
    }
    if (std::ferror(stream)) {
        return std::nullopt;
    }
    return text;
}

auto observe_abort(TestFunction body) noexcept -> DeathResult {
    const auto test_case = current_test_name();
    if (!body || test_case.empty()) {
        return {.aborted = false, .output = {}, .error = {}};
    }
#if defined(_WIN32)
    if (std::getenv(child_environment_name) != nullptr) {
        // Capture emitted bytes without the CRT's text-mode newline translation.
        if (_setmode(_fileno(stdout), _O_BINARY) == -1
            || _setmode(_fileno(stderr), _O_BINARY) == -1) {
            std::_Exit(125);
        }
        run_body(body);
    }
#endif
    const auto output = temporary_stream();
    const auto error = temporary_stream();
    if (!output || !error) {
        return {.aborted = false, .output = {}, .error = {}};
    }
#if defined(_WIN32)
    const auto aborted = expect_windows_termination(test_case, output.get(), error.get());
#else
    const auto aborted = expect_posix_termination(body, output.get(), error.get());
#endif
    auto captured_output = read_stream(output.get());
    auto captured_error = read_stream(error.get());
    return {
        .aborted = aborted && captured_output.has_value() && captured_error.has_value(),
        .output = captured_output ? std::move(*captured_output) : std::string(),
        .error = captured_error ? std::move(*captured_error) : std::string(),
    };
}

} // namespace

auto termination_test(TestFunction body, std::source_location location) noexcept -> TestBody {
    return TestBody(
        [body, location] noexcept {
            const auto result = observe_abort(body);
            expect(result.aborted, location).note(result.output, result.error);
        },
        location
    );
}

[[noreturn]] auto expect_termination(FunctionRef<void() noexcept> action) noexcept -> void {
    if (test_execution_failed()) {
        std::_Exit(1);
    }
    if (!action) {
        std::_Exit(125);
    }
#if defined(_WIN32)
    termination_event = inherited_event();
    if (termination_event == nullptr || std::signal(SIGABRT, record_abort) == SIG_ERR) {
        std::_Exit(125);
    }
#else
    if (std::signal(SIGABRT, SIG_DFL) == SIG_ERR) {
        std::_Exit(125);
    }
#endif
    action();
    std::fflush(nullptr);
    std::_Exit(0);
}
