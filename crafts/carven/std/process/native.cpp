#include <carven/std/process/native.hpp>

#include <carven/runtime/execution/clock.hpp>
#include <carven/runtime/io/stream.hpp>
#include <carven/runtime/text/utf8.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <coroutine>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <corecrt_startup.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif
#endif

namespace carven::standard::process {

class State final {
public:
#if !defined(_WIN32)
    int socket = -1;
    pid_t pid = -1;
#else
    HANDLE process = nullptr;
    HANDLE input = nullptr;
    HANDLE output = nullptr;
#endif
    bool input_closed = false;
    bool reading = false;
    bool writing = false;
    bool waiting = false;
    std::optional<int> status;
    ~State() noexcept;
};

State::~State() noexcept {
#if !defined(_WIN32)
    if (socket >= 0) {
        ::close(socket);
    }
    if (pid > 0 && !status) {
        ::kill(pid, SIGKILL);
        int value;
        while (::waitpid(pid, &value, 0) < 0 && errno == EINTR) {
        }
    }
#else
    if (input) {
        CloseHandle(input);
    }
    if (output) {
        CloseHandle(output);
    }
    if (process) {
        if (!status) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, INFINITE);
        }
        CloseHandle(process);
    }
#endif
}

auto TextResult::take_text() noexcept -> runtime::String {
    return std::move(text);
}

auto ArgumentsResult::take_arguments() noexcept -> runtime::Vector<runtime::String> {
    return std::move(arguments);
}

namespace {

auto path_from_utf8(std::string_view value) noexcept -> std::filesystem::path {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

auto path_result(const std::filesystem::path& path) noexcept -> TextResult {
    const auto encoded = path.generic_u8string();
    const auto text =
        std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    if (!runtime::utf8_is_valid(text)) {
        return {.code = EILSEQ, .text = {}};
    }
    return {.code = 0, .text = runtime::String::from_str(text)};
}

#if defined(_WIN32)
auto windows_text(std::wstring_view value) noexcept -> TextResult {
    if (value.empty()) {
        return {.code = 0, .text = {}};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {.code = E2BIG, .text = {}};
    }
    const auto count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (count == 0) {
        return {.code = static_cast<std::int32_t>(GetLastError()), .text = {}};
    }
    auto text = std::string(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            text.data(),
            count,
            nullptr,
            nullptr
        )
        != count) {
        return {.code = static_cast<std::int32_t>(GetLastError()), .text = {}};
    }
    return {.code = 0, .text = runtime::String::from_utf8(std::move(text))};
}
#endif

} // namespace

auto arguments() noexcept -> ArgumentsResult {
    auto result = runtime::Vector<runtime::String>();
#if defined(_WIN32)
    const auto code = _configure_wide_argv(_crt_argv_unexpanded_arguments);
    if (code != 0) {
        return {.code = code, .arguments = {}};
    }
    for (auto index = 1; index < __argc; ++index) {
        auto text = windows_text(__wargv[index]);
        if (text.code != 0) {
            return {.code = text.code, .arguments = {}};
        }
        result.push(std::move(text.text));
    }
#elif defined(__APPLE__)
    const auto count = *_NSGetArgc();
    const auto values = *_NSGetArgv();
    for (auto index = 1; index < count; ++index) {
        const auto text = std::string_view(values[index]);
        if (!runtime::utf8_is_valid(text)) {
            return {.code = EILSEQ, .arguments = {}};
        }
        result.push(runtime::String::from_str(text));
    }
#elif defined(__linux__)
    auto input = std::ifstream("/proc/self/cmdline", std::ios::binary);
    if (!input) {
        return {.code = errno == 0 ? EIO : errno, .arguments = {}};
    }
    auto text = std::string();
    static_cast<void>(std::getline(input, text, '\0'));
    while (std::getline(input, text, '\0')) {
        if (!runtime::utf8_is_valid(text)) {
            return {.code = EILSEQ, .arguments = {}};
        }
        result.push(runtime::String::from_str(text));
    }
    if (input.bad()) {
        return {.code = errno == 0 ? EIO : errno, .arguments = {}};
    }
#else
    return {.code = ENOTSUP, .arguments = {}};
#endif
    return {.code = 0, .arguments = std::move(result)};
}

auto executable() noexcept -> TextResult {
#if defined(_WIN32)
    auto path = std::wstring(1024, L'\0');
    for (;;) {
        const auto count =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (count == 0) {
            return {.code = static_cast<std::int32_t>(GetLastError()), .text = {}};
        }
        if (count < path.size()) {
            auto text = windows_text(std::wstring_view(path).substr(0, count));
            if (text.code != 0) {
                return text;
            }
            return path_result(path_from_utf8(text.text.as_str()).lexically_normal());
        }
        if (path.size() > std::numeric_limits<DWORD>::max() / 2) {
            return {.code = E2BIG, .text = {}};
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    auto size = std::uint32_t {0};
    static_cast<void>(_NSGetExecutablePath(nullptr, &size));
    auto path = std::string(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return {.code = EIO, .text = {}};
    }
    auto error = std::error_code();
    const auto resolved = std::filesystem::canonical(path.c_str(), error);
    return error ? TextResult {.code = error.value(), .text = {}} : path_result(resolved);
#elif defined(__linux__)
    auto path = std::string(1024, '\0');
    for (;;) {
        const auto count = ::readlink("/proc/self/exe", path.data(), path.size());
        if (count < 0) {
            return {.code = errno, .text = {}};
        }
        if (static_cast<std::size_t>(count) < path.size()) {
            path.resize(static_cast<std::size_t>(count));
            if (!runtime::utf8_is_valid(path)) {
                return {.code = EILSEQ, .text = {}};
            }
            return {.code = 0, .text = runtime::String::from_utf8(std::move(path))};
        }
        path.resize(path.size() * 2);
    }
#else
    return {.code = ENOTSUP, .text = {}};
#endif
}

auto find_executable(std::string_view name) noexcept -> TextResult {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        return {.code = EINVAL, .text = {}};
    }
    // Windows adds an executable extension below.
    // NOLINTNEXTLINE(misc-const-correctness)
    auto program = path_from_utf8(name);
#if defined(_WIN32)
    if (!program.has_extension()) {
        program += ".exe";
    }
#endif
    const auto resolve = [](const std::filesystem::path& path) noexcept -> TextResult {
        auto error = std::error_code();
        if (!std::filesystem::is_regular_file(path, error)) {
            return {.code = error ? error.value() : ENOENT, .text = {}};
        }
#if !defined(_WIN32)
        if (::access(path.c_str(), X_OK) != 0) {
            return {.code = errno, .text = {}};
        }
#endif
        const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
        return error ? TextResult {.code = error.value(), .text = {}} : path_result(absolute);
    };
    if (program.has_parent_path()) {
        return resolve(program);
    }
    const auto search = environment("PATH");
    if (search.code != 0) {
        return {.code = search.code, .text = {}};
    }
#if defined(_WIN32)
    constexpr auto separator = ';';
#else
    constexpr auto separator = ':';
#endif
    const auto paths = search.text.as_str();
    auto begin = std::size_t {0};
    while (begin <= paths.size()) {
        const auto end = paths.find(separator, begin);
        const auto part = paths.substr(begin, end == std::string_view::npos ? end : end - begin);
        auto found = resolve(path_from_utf8(part) / program);
        if (found.code == 0) {
            return found;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return {.code = ENOENT, .text = {}};
}

auto operating_system() noexcept -> std::int32_t {
#if defined(_WIN32)
    return 0;
#elif defined(__APPLE__)
    return 1;
#elif defined(__linux__)
    return 2;
#else
    return 3;
#endif
}

auto environment(std::string_view name) noexcept -> TextResult {
    if (name.find('\0') != std::string_view::npos) {
        return {.code = EINVAL, .text = {}};
    }
#ifdef _WIN32
    if (name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {.code = E2BIG, .text = {}};
    }
    const auto count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        name.data(),
        static_cast<int>(name.size()),
        nullptr,
        0
    );
    if (count == 0) {
        return {.code = static_cast<std::int32_t>(GetLastError()), .text = {}};
    }
    auto key = std::wstring(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            name.data(),
            static_cast<int>(name.size()),
            key.data(),
            count
        )
        != count) {
        return {.code = static_cast<std::int32_t>(GetLastError()), .text = {}};
    }
    SetLastError(ERROR_SUCCESS);
    const auto length = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (length == 0) {
        const auto code = GetLastError();
        return {
            .code = code == ERROR_SUCCESS || code == ERROR_ENVVAR_NOT_FOUND
                ? 0
                : static_cast<std::int32_t>(code),
            .text = {}
        };
    }
    auto value = std::wstring(static_cast<std::size_t>(length), L'\0');
    SetLastError(ERROR_SUCCESS);
    const auto written = GetEnvironmentVariableW(key.c_str(), value.data(), length);
    if (written == 0) {
        return {.code = static_cast<std::int32_t>(GetLastError()), .text = {}};
    }
    if (written >= length || written > static_cast<DWORD>(std::numeric_limits<int>::max())) {
        return {.code = E2BIG, .text = {}};
    }
    return windows_text(std::wstring_view(value).substr(0, written));
#else
    const auto key = std::string(name);
    const auto* value = std::getenv(key.c_str());
    if (value && !runtime::utf8_is_valid(value)) {
        return {.code = EILSEQ, .text = {}};
    }
    return {.code = 0, .text = value ? runtime::String::from_str(value) : runtime::String()};
#endif
}

auto SpawnResult::take_process() noexcept -> Handle {
    return std::move(process);
}

auto Result::take_bytes() noexcept -> runtime::Vector<std::uint8_t> {
    return std::move(bytes);
}

namespace {

namespace async = runtime::execution;

#if defined(_WIN32)
struct HandleCloser final {
    auto operator()(void* handle) const noexcept -> void;
};

auto HandleCloser::operator()(void* handle) const noexcept -> void {
    CloseHandle(handle);
}

using OwnedHandle = std::unique_ptr<void, HandleCloser>;

auto inherited_handle(DWORD standard) noexcept -> OwnedHandle {
    const auto source = GetStdHandle(standard);
    if (!source || source == INVALID_HANDLE_VALUE) {
        auto attributes = SECURITY_ATTRIBUTES {
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE
        };
        const auto handle = CreateFileW(
            L"NUL",
            standard == STD_INPUT_HANDLE ? GENERIC_READ : GENERIC_WRITE,
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
#endif

auto spawn_impl(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view directory,
    bool inherited,
    std::string_view output = {}
) noexcept -> SpawnResult {
    if (program.empty()
        || program.find('\0') != std::string_view::npos
        || directory.find('\0') != std::string_view::npos
        || output.find('\0') != std::string_view::npos) {
        return {.process = {}, .code = EINVAL};
    }
#if defined(_WIN32)
    auto command = std::string();
    const auto quote = [&](std::string_view value) noexcept {
        command += '"';
        auto slashes = std::size_t();
        for (const auto character : value) {
            if (character == '\\') {
                ++slashes;
                continue;
            }
            command.append(character == '"' ? slashes * 2 + 1 : slashes, '\\');
            slashes = 0;
            command += character;
        }
        command.append(slashes * 2, '\\');
        command += '"';
    };
    quote(program);
    for (const auto& argument : arguments) {
        if (argument.as_str().find('\0') != std::string_view::npos) {
            return {.process = {}, .code = EINVAL};
        }
        command += ' ';
        quote(argument.as_str());
    }
    if (command.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {.process = {}, .code = E2BIG};
    }
    const auto count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        command.data(),
        static_cast<int>(command.size()),
        nullptr,
        0
    );
    if (!count) {
        return {.process = {}, .code = EINVAL};
    }
    if (count > 32766) {
        return {.process = {}, .code = E2BIG};
    }
    auto wide = std::wstring(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        command.data(),
        static_cast<int>(command.size()),
        wide.data(),
        count
    );
    if (!runtime::utf8_is_valid(directory) || !runtime::utf8_is_valid(output)) {
        return {.process = {}, .code = EILSEQ};
    }
    const auto cwd = path_from_utf8(directory);
    auto state = std::make_shared<State>();
    auto child_input = OwnedHandle();
    auto child_output = OwnedHandle();
    const auto child_error = inherited_handle(STD_ERROR_HANDLE);
    if (!child_error) {
        return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
    }
    if (inherited) {
        child_input = inherited_handle(STD_INPUT_HANDLE);
        if (!child_input) {
            return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
        }
        child_output = inherited_handle(STD_OUTPUT_HANDLE);
        if (!child_output) {
            return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
        }
    } else {
        HANDLE reader;
        HANDLE writer;
        if (!CreatePipe(&reader, &writer, nullptr, 0)) {
            return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
        }
        child_input.reset(reader);
        state->input = writer;
        if (!CreatePipe(&reader, &writer, nullptr, 0)) {
            return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
        }
        state->output = reader;
        child_output.reset(writer);
        if (!SetHandleInformation(child_input.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)
            || !SetHandleInformation(
                child_output.get(),
                HANDLE_FLAG_INHERIT,
                HANDLE_FLAG_INHERIT
            )) {
            return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
        }
    }
    if (!output.empty()) {
        auto attributes = SECURITY_ATTRIBUTES {
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE
        };
        const auto path = path_from_utf8(output);
        const auto handle = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            &attributes,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
        }
        child_output.reset(handle);
    }
    auto startup = STARTUPINFOEXW {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input.get();
    startup.StartupInfo.hStdOutput = child_output.get();
    startup.StartupInfo.hStdError = child_error.get();

    // Inherit exactly this child's stdio. Other live children must not keep its
    // pipes open, even when several process operations coexist.
    auto attribute_size = SIZE_T();
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    auto attributes = std::vector<std::uintptr_t>(
        (attribute_size + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t)
    );
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size)) {
        return {.process = {}, .code = static_cast<std::int32_t>(GetLastError())};
    }
    auto handles = std::array {child_input.get(), child_output.get(), child_error.get()};
    if (!UpdateProcThreadAttribute(
            startup.lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles.data(),
            handles.size() * sizeof(HANDLE),
            nullptr,
            nullptr
        )) {
        const auto code = GetLastError();
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        return {.process = {}, .code = static_cast<std::int32_t>(code)};
    }
    auto info = PROCESS_INFORMATION {};
    const auto started = CreateProcessW(
        nullptr,
        wide.data(),
        nullptr,
        nullptr,
        TRUE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        directory.empty() ? nullptr : cwd.c_str(),
        &startup.StartupInfo,
        &info
    );
    const auto start_error = GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!started) {
        return {.process = {}, .code = static_cast<std::int32_t>(start_error)};
    }
    CloseHandle(info.hThread);
    state->process = info.hProcess;
    return {.process = {.state = std::move(state)}, .code = 0};
#else
    auto owned = std::vector<std::string> {std::string(program)};
    for (const auto& argument : arguments) {
        if (argument.as_str().find('\0') != std::string_view::npos) {
            return {.process = {}, .code = EINVAL};
        }
        owned.emplace_back(argument.as_str());
    }
    auto argv = std::vector<char*>();
    for (auto& argument : owned) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    int sockets[2] = {-1, -1};
    if (!inherited && ::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        return {.process = {}, .code = errno};
    }
    // Both endpoints are private to this spawn. Preserve only the child's stdio copies.
    for (const auto socket : sockets) {
        if (socket < 0) {
            continue;
        }
        ::fcntl(socket, F_SETFD, FD_CLOEXEC);
    }
    posix_spawn_file_actions_t actions;
    auto code = ::posix_spawn_file_actions_init(&actions);
    if (code == 0) {
        if (!inherited) {
            code = ::posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
            if (code == 0) {
                code = ::posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
            }
            if (code == 0) {
                code = ::posix_spawn_file_actions_addclose(&actions, sockets[0]);
            }
        }
        if (code == 0 && !output.empty()) {
            const auto path = std::string(output);
            code = ::posix_spawn_file_actions_addopen(
                &actions,
                STDOUT_FILENO,
                path.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC,
                0666
            );
        }
        if (code == 0 && !directory.empty()) {
            const auto cwd = std::string(directory);
#if defined(__APPLE__)
            code = ::posix_spawn_file_actions_addchdir(&actions, cwd.c_str());
#else
            code = ::posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#endif
        }
        auto child = pid_t(-1);
        if (code == 0) {
            code =
                ::posix_spawnp(&child, owned[0].c_str(), &actions, nullptr, argv.data(), environ);
        }
        ::posix_spawn_file_actions_destroy(&actions);
        if (sockets[1] >= 0) {
            ::close(sockets[1]);
        }
        if (code != 0) {
            if (sockets[0] >= 0) {
                ::close(sockets[0]);
            }
            return {.process = {}, .code = code};
        }
        auto state = std::make_shared<State>();
        state->pid = child;
        state->socket = sockets[0];
        if (inherited) {
            return {.process = {.state = std::move(state)}, .code = 0};
        }
        const auto flags = ::fcntl(state->socket, F_GETFL);
        if (flags < 0 || ::fcntl(state->socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            return {.process = {}, .code = errno};
        }
#if defined(SO_NOSIGPIPE)
        const auto enabled = 1;
        if (::setsockopt(state->socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
            return {.process = {}, .code = errno};
        }
#endif
        return {.process = {.state = std::move(state)}, .code = 0};
    }
    if (sockets[0] >= 0) {
        ::close(sockets[0]);
    }
    if (sockets[1] >= 0) {
        ::close(sockets[1]);
    }
    return {.process = {}, .code = code};
#endif
}

} // namespace

auto spawn(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view directory
) noexcept -> SpawnResult {
    return spawn_impl(program, arguments, directory, false);
}

auto spawn_inherited(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view output
) noexcept -> SpawnResult {
    return spawn_impl(program, arguments, {}, true, output);
}

namespace {

struct Poll final {
    std::int32_t code;
    bool done;
    std::int32_t status;
};

auto poll_exit(const Handle& process) noexcept -> Poll {
    if (!process.state) {
        return {.code = EBADF, .done = false, .status = 0};
    }
    auto& state = *process.state;
    if (state.status) {
        return {.code = 0, .done = true, .status = *state.status};
    }
#if defined(_WIN32)
    const auto ready = WaitForSingleObject(state.process, 0);
    if (ready == WAIT_TIMEOUT) {
        return {.code = 0, .done = false, .status = 0};
    }
    auto status = DWORD();
    if (ready != WAIT_OBJECT_0 || !GetExitCodeProcess(state.process, &status)) {
        return {.code = static_cast<std::int32_t>(GetLastError()), .done = false, .status = 0};
    }
    state.status = static_cast<std::int32_t>(status);
#else
    int status;
    const auto result = ::waitpid(state.pid, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) {
        return {.code = 0, .done = false, .status = 0};
    }
    if (result < 0) {
        return {.code = errno, .done = false, .status = 0};
    }
    state.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    return {.code = 0, .done = true, .status = *state.status};
}

} // namespace

auto wait_any(std::span<const Handle> processes) noexcept -> Exit {
    if (processes.empty() || processes.size() > static_cast<std::size_t>(INT_MAX)) {
        return {.index = 0, .status = 0, .code = EINVAL};
    }
    for (const auto& process : processes) {
        if (!process.state || process.state->waiting) {
            return {.index = 0, .status = 0, .code = process.state ? EBUSY : EBADF};
        }
    }

    struct Waiting final {
        std::span<const Handle> handles;

        ~Waiting() noexcept {
            for (const auto& handle : handles) {
                handle.state->waiting = false;
            }
        }
    };

    const auto waiting = Waiting {.handles = processes};

    for (const auto& process : processes) {
        process.state->waiting = true;
    }
    for (;;) {
        for (auto index = std::size_t(); index < processes.size(); ++index) {
            const auto result = poll_exit(processes[index]);
            if (result.code || result.done) {
                return {
                    .index = static_cast<std::int32_t>(index),
                    .status = result.status,
                    .code = result.code
                };
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

auto run(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view directory,
    std::string_view output
) noexcept -> RunResult {
    std::cout.flush();
    std::cerr.flush();
    auto child = spawn_impl(program, arguments, directory, true, output);
    if (child.code != 0) {
        return {.status = 0, .code = child.code};
    }
    const auto completed = wait_any(std::span(&child.process, 1));
    return {.status = completed.status, .code = completed.code};
}

auto group_new() noexcept -> Group {
    return {.children = std::make_shared<std::vector<Handle>>()};
}

auto group_spawn(
    const Group& group,
    std::string_view program,
    runtime::Slice<runtime::String> arguments
) noexcept -> std::int32_t {
    if (!group.children) {
        return EBADF;
    }
    auto child = spawn_inherited(program, arguments);
    if (child.code == 0) {
        group.children->push_back(std::move(child.process));
    }
    return child.code;
}

auto group_wait(const Group& group) noexcept -> Exit {
    if (!group.children) {
        return {.index = 0, .status = 0, .code = EBADF};
    }
    const auto result = wait_any(*group.children);
    if (result.code == 0) {
        group.children->erase(group.children->begin() + result.index);
    }
    return result;
}

auto parallelism() noexcept -> std::int32_t {
    return static_cast<std::int32_t>(
        std::clamp(std::thread::hardware_concurrency(), 1u, static_cast<unsigned int>(INT_MAX))
    );
}

namespace {

enum class Kind { Read, Write, Wait };

class Request final : public async::EventSource {
    using Completion = async::Completion<Result>;
    Handle process;
    Kind kind;
    runtime::Vector<std::uint8_t> bytes;
#if !defined(_WIN32)
    std::size_t offset = 0;
#endif
    bool active = false;
    bool attached = false;
    async::ResumeContinuation continuation;
    std::optional<Completion> completion;
#if defined(_WIN32)
    runtime::io::detail::WriteWorker writer;
#endif

    auto busy() noexcept -> bool&;
    auto attempt() noexcept -> std::optional<Completion>;
    auto suspend() noexcept -> bool;

    auto finish(std::int32_t code, bool end, std::int32_t status) noexcept -> Completion;

public:
    Request(Handle process, Kind kind, runtime::Vector<std::uint8_t> bytes) noexcept;
    ~Request() noexcept override;

    auto await_ready() const noexcept -> bool;

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
        continuation.bind(handle);
        return suspend();
    }

    auto await_resume() noexcept -> Completion;
    auto poll(async::Driver& driver) noexcept -> void override;
    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool override;
};

auto Request::suspend() noexcept -> bool {
    if (continuation.cancel_requested()) {
        completion.emplace(Completion::cancelled());
        return false;
    }
    if (!process.state) {
        completion.emplace(finish(EBADF, false, 0));
        return false;
    }
    if (busy()) {
        completion.emplace(finish(EBUSY, false, 0));
        return false;
    }
    busy() = true;
    active = true;
    auto ready = attempt();
    if (ready) {
        completion.emplace(std::move(*ready));
        return false;
    }
    continuation.driver().attach(*this);
    attached = true;
    return true;
}

auto Request::await_ready() const noexcept -> bool {
    return false;
}

auto Request::await_resume() noexcept -> Completion {
    return std::move(*completion);
}

Request::Request(Handle process, Kind kind, runtime::Vector<std::uint8_t> bytes) noexcept
    : process(std::move(process)),
      kind(kind),
      bytes(std::move(bytes)) {}

Request::~Request() noexcept {
    if (attached) {
        continuation.driver().detach(*this);
    }
#if defined(_WIN32)
    writer.close();
#endif
    if (active) {
        busy() = false;
    }
}

auto Request::busy() noexcept -> bool& {
    if (kind == Kind::Read) {
        return process.state->reading;
    }
    if (kind == Kind::Write) {
        return process.state->writing;
    }
    return process.state->waiting;
}

auto Request::finish(std::int32_t code, bool end, std::int32_t status) noexcept -> Completion {
    return Completion::success_from([&]() noexcept {
        return Result {.bytes = std::move(bytes), .code = code, .status = status, .end = end};
    });
}

auto Request::attempt() noexcept -> std::optional<Completion> {
    if (kind == Kind::Wait) {
        const auto result = poll_exit(process);
        if (result.code || result.done) {
            return finish(result.code, false, result.status);
        }
        return std::nullopt;
    }
#if defined(_WIN32)
    const auto& state = *process.state;
    if (kind == Kind::Write) {
        if (state.input_closed || !state.input) {
            return finish(EBADF, false, 0);
        }
        if (bytes.empty()) {
            return finish(0, false, 0);
        }
        if (!writer.started()) {
            const auto code = writer.start(state.input, std::span(bytes.begin(), bytes.size()));
            if (code != 0) {
                return finish(code, false, 0);
            }
        }
        if (!writer.ready()) {
            return std::nullopt;
        }
        return finish(writer.result(), false, 0);
    }
    if (!state.output) {
        return finish(EBADF, false, 0);
    }
    auto available = DWORD();
    if (!PeekNamedPipe(state.output, nullptr, 0, nullptr, &available, nullptr)) {
        const auto code = GetLastError();
        return finish(
            code == ERROR_BROKEN_PIPE ? 0 : static_cast<std::int32_t>(code),
            code == ERROR_BROKEN_PIPE,
            0
        );
    }
    if (available == 0) {
        return std::nullopt;
    }
    auto count = DWORD();
    if (!ReadFile(
            state.output,
            const_cast<std::uint8_t*>(bytes.begin()),
            std::min(available, static_cast<DWORD>(bytes.size())),
            &count,
            nullptr
        )) {
        const auto code = GetLastError();
        return finish(
            code == ERROR_BROKEN_PIPE ? 0 : static_cast<std::int32_t>(code),
            code == ERROR_BROKEN_PIPE,
            0
        );
    }
    bytes = runtime::Vector<std::uint8_t>::from_slice(
        runtime::Slice<std::uint8_t>(std::span(bytes.begin(), static_cast<std::size_t>(count)))
    );
    return finish(0, count == 0, 0);
#else
    const auto& state = *process.state;
    if (kind == Kind::Write && offset == bytes.size()) {
        return finish(0, false, 0);
    }
    for (;;) {
#if defined(MSG_NOSIGNAL)
        constexpr auto flags = MSG_NOSIGNAL;
#else
        constexpr auto flags = 0;
#endif
        const auto count = kind == Kind::Read
            ? ::recv(state.socket, const_cast<std::uint8_t*>(bytes.begin()), bytes.size(), 0)
            : ::send(state.socket, bytes.begin() + offset, bytes.size() - offset, flags);
        if (count >= 0) {
            if (kind == Kind::Read) {
                auto received = runtime::Vector<std::uint8_t>::from_slice(
                    runtime::Slice<std::uint8_t>(
                        std::span(bytes.begin(), static_cast<std::size_t>(count))
                    )
                );
                bytes = std::move(received);
                return finish(0, count == 0, 0);
            }
            if (count == 0) {
                return finish(EIO, false, 0);
            }
            offset += static_cast<std::size_t>(count);
            if (offset == bytes.size()) {
                return finish(0, false, 0);
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::nullopt;
        }
        return finish(errno, false, 0);
    }
#endif
}

auto Request::poll(async::Driver& driver) noexcept -> void {
    if (completion) {
        return;
    }
#if defined(_WIN32)
    if (continuation.cancel_requested() && writer.started()) {
        writer.cancel();
        if (!writer.ready()) {
            return;
        }
    }
#endif
    auto ready =
        continuation.cancel_requested() ? std::optional(Completion::cancelled()) : attempt();
    if (ready) {
        completion.emplace(std::move(*ready));
        continuation.enqueue(driver);
    }
}

auto Request::wait(std::optional<std::int64_t> deadline) noexcept -> bool {
    if (completion) {
        return false;
    }
#if !defined(_WIN32)
    auto timeout = async::detail::steady_poll_timeout(deadline);
    if (kind == Kind::Wait) {
        // waitpid has no portable readiness descriptor; bound exit observation latency.
        if (timeout < 0 || timeout > 10) {
            timeout = 10;
        }
        ::poll(nullptr, 0, timeout);
    } else {
        auto descriptor = pollfd {
            .fd = process.state->socket,
            .events = static_cast<short>(kind == Kind::Read ? POLLIN : POLLOUT),
            .revents = 0
        };
        if (::poll(&descriptor, 1, timeout) < 0 && errno != EINTR) {
            completion.emplace(finish(errno, false, 0));
            continuation.enqueue(continuation.driver());
        }
    }
#else
    const auto timeout = async::detail::steady_poll_timeout(deadline);
    if (kind == Kind::Wait) {
        WaitForSingleObject(
            process.state->process,
            timeout < 0 ? INFINITE : static_cast<DWORD>(timeout)
        );
    } else if (kind == Kind::Write) {
        writer.wait(timeout < 0 ? INFINITE : static_cast<DWORD>(timeout));
    } else {
        // Anonymous pipe reads have no readiness event. Peek before each read
        // and bound the polling interval without consuming data on cancellation.
        Sleep(timeout < 0 ? 10 : static_cast<DWORD>(std::min(timeout, 10)));
    }
#endif
    return true;
}

} // namespace

auto read(Handle process, std::size_t limit) noexcept -> async::AsyncOperation<Result> {
    auto bytes = runtime::Vector<std::uint8_t>();
    for (auto i = std::size_t(0); i < std::min(limit, std::size_t(65536)); ++i) {
        bytes.push(0);
    }
    co_return co_await Request(std::move(process), Kind::Read, std::move(bytes));
}

auto write(Handle process, runtime::Vector<std::uint8_t> bytes) noexcept
    -> async::AsyncOperation<Result> {
    co_return co_await Request(std::move(process), Kind::Write, std::move(bytes));
}

auto wait(Handle process) noexcept -> async::AsyncOperation<Result> {
    co_return co_await Request(std::move(process), Kind::Wait, {});
}

auto close_input(const Handle& process) noexcept -> std::int32_t {
    if (!process.state) {
        return EBADF;
    }
    auto& state = *process.state;
    if (state.input_closed) {
        return 0;
    }
    if (state.writing) {
        return EBUSY;
    }
#if defined(_WIN32)
    if (!state.input) {
        return EBADF;
    }
    if (!CloseHandle(state.input)) {
        return static_cast<std::int32_t>(GetLastError());
    }
    state.input = nullptr;
#else
    if (::shutdown(state.socket, SHUT_WR) != 0) {
        const auto code = errno;
        // The child may close its endpoint immediately after its final reply.
        // EOF is already satisfied then; do not turn a successful exit into failure.
        if (code != ENOTCONN) {
            return code;
        }
    }
#endif
    state.input_closed = true;
    return 0;
}

auto terminate(const Handle& process) noexcept -> std::int32_t {
#if defined(_WIN32)
    if (!process.state) {
        return EBADF;
    }
    const auto completed = poll_exit(process);
    if (completed.code || completed.done) {
        return completed.code;
    }
    if (TerminateProcess(process.state->process, 1)) {
        return 0;
    }
    const auto code = GetLastError();
    // The process may have exited between the nonblocking observation and kill.
    return poll_exit(process).done ? 0 : static_cast<std::int32_t>(code);
#else
    if (!process.state) {
        return EBADF;
    }
    if (process.state->status) {
        return 0;
    }
    return ::kill(process.state->pid, SIGKILL) == 0 ? 0 : errno;
#endif
}

} // namespace carven::standard::process
