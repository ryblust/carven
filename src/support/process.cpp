module;

#include <cerrno>

#if defined(_WIN32)
#include <cstdlib>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <corecrt_startup.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif
#endif

module carven:support.process.impl;

import :support.path;
import :support.process;
import :support.utf8;
import std;

namespace {

auto last_system_error() noexcept -> std::error_code {
#if defined(_WIN32)
    return std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    return std::error_code(errno, std::generic_category());
#endif
}

#if defined(_WIN32)
auto windows_text_to_utf8(const wchar_t* text) noexcept
    -> std::expected<std::string, std::string_view> {
    const auto length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        return std::unexpected("host text is not valid UTF-16");
    }
    auto result = std::string(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            result.data(),
            length,
            nullptr,
            nullptr
        )
        != length) {
        return std::unexpected("cannot encode host text as UTF-8");
    }
    result.pop_back();
    return result;
}

struct HandleCloser final {
    auto operator()(void* handle) const noexcept -> void;
};

auto HandleCloser::operator()(void* handle) const noexcept -> void {
    CloseHandle(handle);
}

using OwnedHandle = std::unique_ptr<void, HandleCloser>;

auto standard_handle(DWORD stream, bool closed_input) noexcept -> OwnedHandle {
    const auto source = closed_input ? INVALID_HANDLE_VALUE : GetStdHandle(stream);
    if (source == nullptr || source == INVALID_HANDLE_VALUE) {
        auto attributes = SECURITY_ATTRIBUTES {
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE,
        };
        const auto handle = CreateFileW(
            L"NUL",
            stream == STD_INPUT_HANDLE ? GENERIC_READ : GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        return OwnedHandle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
    }
    auto duplicate = HANDLE();
    if (!DuplicateHandle(
            GetCurrentProcess(),
            source,
            GetCurrentProcess(),
            &duplicate,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS
        )) {
        return {};
    }
    return OwnedHandle(duplicate);
}

auto append_argument(std::wstring& command, std::wstring_view argument) noexcept -> void {
    command += L'"';
    auto slashes = std::size_t();
    for (const auto character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        command.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        command += character;
    }
    command.append(slashes * 2, L'\\');
    command += L'"';
}
#else
struct Descriptor final {
    int value;

    ~Descriptor() noexcept;
};

Descriptor::~Descriptor() noexcept {
    if (value != -1) {
        ::close(value);
    }
}
#endif

} // namespace

namespace process {

auto arguments(int argc, [[maybe_unused]] const char* const* argv) noexcept
    -> std::expected<std::vector<std::string>, std::string_view> {
#if defined(_WIN32)
    // Narrow argv may have lost characters through the system code page.
    // The CRT preserves quoting and empty arguments in its wide argument vector.
    if (_configure_wide_argv(_crt_argv_unexpanded_arguments) != 0) {
        return std::unexpected("cannot read process arguments");
    }
    argc = __argc;
#endif
    auto result = std::vector<std::string>();
    result.reserve(static_cast<std::size_t>(argc));
    for (auto index = 0; index < argc; ++index) {
#if defined(_WIN32)
        auto text = windows_text_to_utf8(__wargv[index]);
        if (!text) {
            return std::unexpected(text.error());
        }
        result.push_back(std::move(*text));
#else
        const auto text = std::string_view(argv[index]);
        if (!UTF8Decoder::is_valid(text)) {
            return std::unexpected("process argument is not valid UTF-8");
        }
        result.emplace_back(text);
#endif
    }
    return result;
}

auto environment(const char* name) noexcept
    -> std::expected<std::optional<std::string>, std::string_view> {
#if defined(_WIN32)
    const auto name_length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    if (name_length == 0) {
        return std::unexpected("environment variable name is not valid UTF-8");
    }
    auto wide_name = std::wstring(static_cast<std::size_t>(name_length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name.data(), name_length)
        != name_length) {
        return std::unexpected("cannot encode environment variable name");
    }
    // Read the original Unicode environment rather than the CRT's narrow copy.
    SetLastError(ERROR_SUCCESS);
    const auto length = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (length == 0) {
        const auto error = GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        if (error == ERROR_SUCCESS) {
            return std::string();
        }
        return std::unexpected("cannot read environment variable");
    }
    auto value = std::wstring(static_cast<std::size_t>(length), L'\0');
    SetLastError(ERROR_SUCCESS);
    const auto written = GetEnvironmentVariableW(wide_name.c_str(), value.data(), length);
    if (written == 0) {
        if (GetLastError() == ERROR_SUCCESS) {
            return std::string();
        }
        return std::unexpected("cannot read environment variable");
    }
    if (written >= length) {
        return std::unexpected("cannot read environment variable");
    }
    auto text = windows_text_to_utf8(value.c_str());
    if (!text) {
        return std::unexpected(text.error());
    }
    return std::move(*text);
#else
    const auto* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto text = std::string_view(value);
    if (!UTF8Decoder::is_valid(text)) {
        return std::unexpected("environment variable is not valid UTF-8");
    }
    return std::string(text);
#endif
}

auto enable_binary_standard_streams() noexcept -> bool {
#if defined(_WIN32)
    return _setmode(0, _O_BINARY) != -1 && _setmode(1, _O_BINARY) != -1;
#else
    return true;
#endif
}

auto executable() noexcept -> std::expected<std::filesystem::path, std::error_code> {
#if defined(_WIN32)
    auto path = std::wstring(1024, L'\0');
    for (;;) {
        const auto count =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (count == 0) {
            return std::unexpected(last_system_error());
        }
        if (count < path.size()) {
            path.resize(count);
            break;
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    auto size = std::uint32_t();
    static_cast<void>(_NSGetExecutablePath(nullptr, &size));
    auto path = std::string(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    path.pop_back();
#elif defined(__linux__)
    auto path = std::string(1024, '\0');
    for (;;) {
        const auto count = ::readlink("/proc/self/exe", path.data(), path.size());
        if (count < 0) {
            return std::unexpected(last_system_error());
        }
        if (static_cast<std::size_t>(count) < path.size()) {
            path.resize(static_cast<std::size_t>(count));
            break;
        }
        path.resize(path.size() * 2);
    }
#endif
    auto error = std::error_code();
    auto resolved = std::filesystem::canonical(path, error);
    if (error) {
        return std::unexpected(error);
    }
    return resolved;
}

auto run(
    const std::filesystem::path& program,
    std::span<const std::string> arguments,
    const std::filesystem::path& directory,
    Stdio stdio
) noexcept -> std::expected<Result, std::error_code> {
    const auto capture = stdio == Stdio::CaptureOutput;
    auto result = Result {.status = 0, .output = {}};
    auto error = std::error_code();
#if defined(_WIN32)
    auto command = std::wstring();
    append_argument(command, program.native());
    for (const auto& argument : arguments) {
        command += L' ';
        append_argument(command, path_from_utf8(argument).native());
    }
    auto input = standard_handle(STD_INPUT_HANDLE, capture);
    if (!input) {
        return std::unexpected(last_system_error());
    }
    auto diagnostic = standard_handle(STD_ERROR_HANDLE, false);
    if (!diagnostic) {
        return std::unexpected(last_system_error());
    }
    auto reader = OwnedHandle();
    auto output = OwnedHandle();
    if (capture) {
        auto read_handle = HANDLE();
        auto write_handle = HANDLE();
        if (!CreatePipe(&read_handle, &write_handle, nullptr, 0)) {
            return std::unexpected(last_system_error());
        }
        reader.reset(read_handle);
        output.reset(write_handle);
        if (!SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            return std::unexpected(last_system_error());
        }
    } else {
        output = standard_handle(STD_OUTPUT_HANDLE, false);
        if (!output) {
            return std::unexpected(last_system_error());
        }
    }
    auto startup = STARTUPINFOEXW {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = diagnostic.get();

    // Only this child's standard streams may be inherited, including when the
    // compiler is embedded in a host with other live processes and handles.
    auto attribute_size = SIZE_T();
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    auto attributes = std::vector<std::uintptr_t>(
        (attribute_size + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t)
    );
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size)) {
        return std::unexpected(last_system_error());
    }
    const auto cleanup = std::unique_ptr<
        std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>,
        decltype(&DeleteProcThreadAttributeList)>(
        startup.lpAttributeList,
        DeleteProcThreadAttributeList
    );
    auto handles = std::array {input.get(), output.get(), diagnostic.get()};
    if (!UpdateProcThreadAttribute(
            startup.lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles.data(),
            sizeof(handles),
            nullptr,
            nullptr
        )) {
        return std::unexpected(last_system_error());
    }
    auto info = PROCESS_INFORMATION {};
    if (!CreateProcessW(
            program.c_str(),
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            directory.empty() ? nullptr : directory.c_str(),
            &startup.StartupInfo,
            &info
        )) {
        return std::unexpected(last_system_error());
    }
    const auto child = OwnedHandle(info.hProcess);
    CloseHandle(info.hThread);
    output.reset();
    if (capture) {
        auto buffer = std::array<char, 65536>();
        for (;;) {
            auto count = DWORD();
            if (!ReadFile(
                    reader.get(),
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &count,
                    nullptr
                )) {
                const auto code = GetLastError();
                if (code != ERROR_BROKEN_PIPE) {
                    error = std::error_code(static_cast<int>(code), std::system_category());
                    TerminateProcess(child.get(), 1);
                }
                break;
            }
            if (count == 0) {
                break;
            }
            result.output.append(buffer.data(), count);
        }
    }
    if (WaitForSingleObject(child.get(), INFINITE) == WAIT_FAILED) {
        return std::unexpected(last_system_error());
    }
    auto status = DWORD();
    if (!GetExitCodeProcess(child.get(), &status)) {
        return std::unexpected(last_system_error());
    }
    result.status = static_cast<int>(status);
#else
    auto native_actions = posix_spawn_file_actions_t();
    auto code = posix_spawn_file_actions_init(&native_actions);
    if (code != 0) {
        return std::unexpected(std::error_code(code, std::generic_category()));
    }
    const auto actions =
        std::unique_ptr<posix_spawn_file_actions_t, decltype(&posix_spawn_file_actions_destroy)>(
            &native_actions,
            posix_spawn_file_actions_destroy
        );
    auto reader = Descriptor {.value = -1};
    auto writer = Descriptor {.value = -1};
    if (capture) {
        auto descriptors = std::array<int, 2>();
        if (::pipe(descriptors.data()) != 0) {
            return std::unexpected(last_system_error());
        }
        reader.value = descriptors[0];
        writer.value = descriptors[1];
        if (::fcntl(reader.value, F_SETFD, FD_CLOEXEC) == -1
            || ::fcntl(writer.value, F_SETFD, FD_CLOEXEC) == -1) {
            return std::unexpected(last_system_error());
        }
        code =
            posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        if (code == 0) {
            code = posix_spawn_file_actions_adddup2(actions.get(), writer.value, STDOUT_FILENO);
        }
    }
    if (code == 0 && !directory.empty()) {
#if defined(__APPLE__)
        code = posix_spawn_file_actions_addchdir(actions.get(), directory.c_str());
#else
        code = posix_spawn_file_actions_addchdir_np(actions.get(), directory.c_str());
#endif
    }
    if (code != 0) {
        return std::unexpected(std::error_code(code, std::generic_category()));
    }
    // POSIX declares argv mutable but does not modify its strings.
    auto argv = std::vector<char*> {const_cast<char*>(program.c_str())};
    argv.reserve(arguments.size() + 2);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    auto child = pid_t();
#if defined(__APPLE__)
    char* const* const environment = *_NSGetEnviron();
#else
    char* const* const environment = environ;
#endif
    code = posix_spawn(&child, program.c_str(), actions.get(), nullptr, argv.data(), environment);
    if (code != 0) {
        return std::unexpected(std::error_code(code, std::generic_category()));
    }
    if (capture) {
        ::close(std::exchange(writer.value, -1));
        auto buffer = std::array<char, 65536>();
        for (;;) {
            const auto count = ::read(reader.value, buffer.data(), buffer.size());
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                error = std::error_code(errno, std::generic_category());
                ::kill(child, SIGKILL);
                break;
            }
            if (count == 0) {
                break;
            }
            result.output.append(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    auto status = int();
    while (::waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            return std::unexpected(last_system_error());
        }
    }
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    if (error) {
        return std::unexpected(error);
    }
    return result;
}

} // namespace process
