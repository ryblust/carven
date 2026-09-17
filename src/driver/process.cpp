module;
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

module carven:driver.process.impl;

import :driver.process;
import :support.path;
import std;

auto windows_command_line(std::span<const std::string> arguments) noexcept -> std::string {
    auto command = std::string();
    for (const auto& argument : arguments) {
        if (!command.empty()) {
            command += ' ';
        }
        command += '"';
        auto slashes = 0uz;
        for (const auto character : argument) {
            if (character == '\\') {
                ++slashes;
                continue;
            }
            command.append(character == '"' ? slashes * 2uz + 1uz : slashes, '\\');
            command += character;
            slashes = 0uz;
        }
        command.append(slashes * 2uz, '\\');
        command += '"';
    }
    return command;
}

auto run_process(std::vector<std::string> arguments) noexcept -> std::expected<int, std::string> {
    if (arguments.empty() || arguments.front().empty()) {
        return std::unexpected("process executable is empty");
    }
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos) {
            return std::unexpected("process argument contains NUL");
        }
    }
#ifdef _WIN32
    const auto encoded = windows_command_line(arguments);
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected("process command line exceeds the Windows limit");
    }
    const auto count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        encoded.data(),
        static_cast<int>(encoded.size()),
        nullptr,
        0
    );
    if (count == 0) {
        return std::unexpected("process command line is not valid UTF-8");
    }
    if (count > 32766) {
        return std::unexpected("process command line exceeds the Windows limit");
    }
    auto command = std::wstring(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            encoded.data(),
            static_cast<int>(encoded.size()),
            command.data(),
            count
        )
        != count) {
        return std::unexpected("cannot encode process command line");
    }
    auto startup = STARTUPINFOW {};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    auto process = PROCESS_INFORMATION {};
    // Quoted executable plus CRT-encoded arguments; CreateProcess does not invoke cmd.exe.
    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process
        )) {
        return std::unexpected(
            std::format(
                "cannot start '{}': {}",
                arguments.front(),
                std::error_code(static_cast<int>(GetLastError()), std::system_category()).message()
            )
        );
    }
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, INFINITE);
    auto status = DWORD {};
    const auto completed = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &status);
    const auto error = GetLastError();
    CloseHandle(process.hProcess);
    if (!completed) {
        return std::unexpected(
            std::format(
                "cannot wait for child process: {}",
                std::error_code(static_cast<int>(error), std::system_category()).message()
            )
        );
    }
    return static_cast<int>(status);
#else
    auto argv = std::vector<char*>();
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    auto child = pid_t();
    const auto error = posix_spawnp(&child, argv.front(), nullptr, nullptr, argv.data(), environ);
    if (error != 0) {
        return std::unexpected(
            std::format(
                "cannot start '{}': {}",
                arguments.front(),
                std::error_code(error, std::generic_category()).message()
            )
        );
    }
    auto status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            return std::unexpected("cannot wait for child process");
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
#endif
}

auto current_executable_path(std::string_view invocation) noexcept -> std::filesystem::path {
#ifdef _WIN32
    static_cast<void>(invocation);
    auto buffer = std::wstring(1024uz, L'\0');
    while (buffer.size() <= 32768uz) {
        const auto count =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (count == 0) {
            return {};
        }
        if (count < buffer.size()) {
            buffer.resize(count);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2uz);
    }
    return {};
#else
    auto program = path_from_utf8(invocation);
    auto error = std::error_code();
    if (!program.has_parent_path()) {
        if (const auto* search = std::getenv("PATH")) {
            for (const auto part : std::string_view(search) | std::views::split(':')) {
                const auto candidate = path_from_utf8(std::string_view(part)) / program;
                if (std::filesystem::is_regular_file(candidate, error)
                    && access(candidate.c_str(), X_OK) == 0) {
                    program = candidate;
                    break;
                }
            }
        }
    }
    const auto canonical = std::filesystem::canonical(program, error);
    return error ? std::filesystem::path() : canonical;
#endif
}

auto create_run_directory() noexcept -> std::expected<std::filesystem::path, std::string> {
    auto error = std::error_code();
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) {
        return std::unexpected(
            std::format("cannot locate temporary directory: {}", error.message())
        );
    }
#ifdef _WIN32
    // CreateDirectory is atomic: a collision never gives us ownership of an existing directory.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (auto attempt = 0u; attempt < 128u; ++attempt) {
        const auto path =
            root / std::format("carven-run-{}-{}-{}", GetCurrentProcessId(), nonce, attempt);
        if (CreateDirectoryW(path.c_str(), nullptr)) {
            return path;
        }
        const auto code = GetLastError();
        if (code != ERROR_ALREADY_EXISTS) {
            return std::unexpected(
                std::format(
                    "cannot create temporary run directory: {}",
                    std::error_code(static_cast<int>(code), std::system_category()).message()
                )
            );
        }
    }
    return std::unexpected("cannot create a unique temporary run directory");
#else
    auto pattern = (root / "carven-run-XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) {
        return std::unexpected(
            std::format(
                "cannot create temporary run directory: {}",
                std::error_code(errno, std::generic_category()).message()
            )
        );
    }
    return std::filesystem::path(pattern);
#endif
}
