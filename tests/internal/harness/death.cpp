module;
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <csignal>
#include <cstdio>
#if defined(_WIN32)
#include <stdlib.h>
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

module carven:test.internal.harness.death.impl;

import :test.internal.harness.death;
import std;

namespace {

#if defined(_WIN32)

constexpr auto scenario_environment = "CARVEN_INTERNAL_DEATH_SCENARIO";
constexpr auto event_environment = "CARVEN_INTERNAL_DEATH_EVENT";

struct SavedEnvironment final {
    bool present;
    std::string value;
};

struct ChildResult final {
    bool launched;
    bool successful_exit;
};

auto save_environment(const char* name) noexcept -> SavedEnvironment {
    const auto* value = std::getenv(name);
    if (value == nullptr) {
        return {.present = false, .value = {}};
    }
    return {.present = true, .value = value};
}

auto set_environment(const char* name, std::string_view value) noexcept -> bool {
    const auto owned = std::string(value);
    return _putenv_s(name, owned.c_str()) == 0;
}

auto restore_environment(const char* name, const SavedEnvironment& saved) noexcept -> bool {
    if (saved.present) {
        return set_environment(name, saved.value);
    }
    return _putenv_s(name, "") == 0;
}

auto inherited_event() noexcept -> HANDLE {
    const auto* text = std::getenv(event_environment);
    if (text == nullptr) {
        return nullptr;
    }
    auto value = std::uintptr_t {};
    const auto end = text + std::char_traits<char>::length(text);
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end || value == 0u) {
        return nullptr;
    }
    return reinterpret_cast<HANDLE>(value);
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
    auto buffer = std::vector<wchar_t>(1024uz);
    while (buffer.size() <= 32768uz) {
        const auto size =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0u) {
            return std::nullopt;
        }
        if (static_cast<std::size_t>(size) < buffer.size()) {
            return std::wstring(buffer.data(), static_cast<std::size_t>(size));
        }
        buffer.resize(buffer.size() * 2uz);
    }
    return std::nullopt;
}

auto append_windows_argument(std::wstring& command, std::wstring_view argument) noexcept -> void {
    if (!command.empty()) {
        command.push_back(L' ');
    }
    command.push_back(L'"');
    auto backslashes = 0uz;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            command.append(backslashes * 2uz + 1uz, L'\\');
            command.push_back(character);
            backslashes = 0uz;
            continue;
        }
        command.append(backslashes, L'\\');
        backslashes = 0uz;
        command.push_back(character);
    }
    command.append(backslashes * 2uz, L'\\');
    command.push_back(L'"');
}

auto current_test_case_name() noexcept -> std::optional<std::string_view> {
    const auto* context = doctest::getContextOptions();
    if (context == nullptr || context->currentTest == nullptr) {
        return std::nullopt;
    }
    return context->currentTest->m_name;
}

auto run_child(std::string_view test_case) noexcept -> ChildResult {
    const auto executable = current_executable();
    const auto wide_test_case = utf8_to_wide(test_case);
    if (!executable.has_value() || !wide_test_case.has_value()) {
        return {.launched = false, .successful_exit = false};
    }

    auto command = std::wstring();
    append_windows_argument(command, *executable);
    append_windows_argument(command, std::wstring(L"--test-case=") + *wide_test_case);
    append_windows_argument(command, L"--no-intro=true");
    append_windows_argument(command, L"--no-version=true");
    append_windows_argument(command, L"--no-colors=true");

    auto startup = STARTUPINFOW {};
    startup.cb = static_cast<DWORD>(sizeof(STARTUPINFOW));
    auto process = PROCESS_INFORMATION {};
    if (!CreateProcessW(
            executable->c_str(),
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process
        )) {
        return {.launched = false, .successful_exit = false};
    }
    CloseHandle(process.hThread);

    const auto wait = WaitForSingleObject(process.hProcess, 30000u);
    if (wait != WAIT_OBJECT_0) {
        static_cast<void>(TerminateProcess(process.hProcess, 1u));
        static_cast<void>(WaitForSingleObject(process.hProcess, INFINITE));
        CloseHandle(process.hProcess);
        return {.launched = false, .successful_exit = false};
    }
    auto exit_code = DWORD {};
    const auto read_exit = GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
    CloseHandle(process.hProcess);
    return {
        .launched = read_exit,
        .successful_exit = read_exit && exit_code == 0u,
    };
}

auto expect_windows_termination(
    std::string_view scenario,
    DeathTestAction action,
    void* context
) noexcept -> bool {
    const auto* selected_scenario = std::getenv(scenario_environment);
    const auto* event_text = std::getenv(event_environment);
    if (selected_scenario != nullptr && event_text != nullptr) {
        if (scenario != selected_scenario) {
            // The filtered child replays earlier assertions before reaching its scenario.
            return true;
        }
        const auto event = inherited_event();
        std::signal(SIGABRT, SIG_DFL);
        if (event == nullptr || SetEvent(event) == FALSE) {
            std::_Exit(125);
        }
        action(context);
        std::_Exit(0);
    }

    const auto test_case = current_test_case_name();
    if (!test_case.has_value()) {
        return false;
    }
    auto security = SECURITY_ATTRIBUTES {};
    security.nLength = static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES));
    security.bInheritHandle = TRUE;
    const auto event = CreateEventW(&security, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }

    const auto saved_scenario = save_environment(scenario_environment);
    const auto saved_event = save_environment(event_environment);
    const auto event_value = std::format("{}", reinterpret_cast<std::uintptr_t>(event));
    if (!set_environment(scenario_environment, scenario)) {
        CloseHandle(event);
        return false;
    }
    if (!set_environment(event_environment, event_value)) {
        static_cast<void>(restore_environment(scenario_environment, saved_scenario));
        CloseHandle(event);
        return false;
    }

    std::fflush(nullptr);
    const auto child = run_child(*test_case);
    const auto restored_event = restore_environment(event_environment, saved_event);
    const auto restored_scenario = restore_environment(scenario_environment, saved_scenario);
    const auto entered_action = WaitForSingleObject(event, 0u) == WAIT_OBJECT_0;
    CloseHandle(event);
    return child.launched
        && !child.successful_exit
        && entered_action
        && restored_event
        && restored_scenario;
}

#else

auto expect_posix_termination(DeathTestAction action, void* context) noexcept -> bool {
    std::fflush(nullptr);
    auto readiness = std::array<int, 2> {};
    if (pipe(readiness.data()) != 0) {
        return false;
    }
    const auto child = fork();
    if (child == 0) {
        static_cast<void>(close(readiness[0]));
        std::signal(SIGABRT, SIG_DFL);
        constexpr auto entered = char {'1'};
        auto written = ssize_t {};
        do {
            written = write(readiness[1], &entered, sizeof(entered));
        } while (written < 0 && errno == EINTR);
        static_cast<void>(close(readiness[1]));
        if (written != static_cast<ssize_t>(sizeof(entered))) {
            _exit(125);
        }
        action(context);
        _exit(0);
    }
    static_cast<void>(close(readiness[1]));
    if (child < 0) {
        static_cast<void>(close(readiness[0]));
        return false;
    }

    auto status = 0;
    auto waited = pid_t {};
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    auto entered = char {};
    auto read_count = ssize_t {};
    do {
        read_count = read(readiness[0], &entered, sizeof(entered));
    } while (read_count < 0 && errno == EINTR);
    static_cast<void>(close(readiness[0]));
    if (waited != child) {
        return false;
    }
    const auto entered_action =
        read_count == static_cast<ssize_t>(sizeof(entered)) && entered == '1';
    const auto abnormal_exit =
        WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
    return entered_action && abnormal_exit;
}

#endif

} // namespace

auto run_death_test(std::string_view scenario, DeathTestAction action, void* context) noexcept
    -> bool {
    if (scenario.empty()) {
        return false;
    }
#if defined(_WIN32)
    return expect_windows_termination(scenario, action, context);
#else
    return expect_posix_termination(action, context);
#endif
}
