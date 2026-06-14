module;

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <cerrno>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

export module carven.common.process;

import std;

#if defined(_WIN32)
auto build_windows_command(std::span<const std::string> args) noexcept -> std::string {
    auto command = std::string();
    for (auto i = 0uz; i < args.size(); ++i) {
        if (i > 0) command += ' ';
        const auto needs_quotes = args[i].contains(' ') || args[i].contains('\t');
        command += needs_quotes ? std::format("\"{}\"", args[i]) : args[i];
    }
    return command;
}
#endif

export auto spawn(std::span<const std::string> args, std::string_view cwd) noexcept -> int {
    if (args.empty()) return -1;
    const auto cwd_text = std::string(cwd);

#if defined(_WIN32)
    auto command = build_windows_command(args);

    auto si = STARTUPINFOA{};
    si.cb = sizeof(si);
    auto pi = PROCESS_INFORMATION{};
    auto exit_code = DWORD{};

    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd_text.c_str(), &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exit_code);
#else
    auto argv = std::vector<char*>();
    argv.reserve(args.size() + 1);

    for (const auto& arg : args) {
        argv.emplace_back(const_cast<char*>(arg.c_str()));
    }
    argv.emplace_back(nullptr);

    int exec_pipe[2];
    if (pipe(exec_pipe) < 0) return -1;

    const auto pid = fork();
    if (pid < 0) {
        close(exec_pipe[0]); close(exec_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(exec_pipe[0]);
        if (chdir(cwd_text.c_str()) < 0) {
            write(exec_pipe[1], "x", 1);
            close(exec_pipe[1]);
            _exit(0);
        }
        execvp(argv[0], argv.data());
        write(exec_pipe[1], "x", 1);
        close(exec_pipe[1]);
        _exit(0);
    }

    close(exec_pipe[1]);

    char exec_failed;
    if (read(exec_pipe[0], &exec_failed, 1) > 0) {
        close(exec_pipe[0]);
        waitpid(pid, nullptr, 0);
        return -1;
    }
    close(exec_pipe[0]);

    auto status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

export auto spawn_capture(std::span<const std::string> args, std::string_view cwd) noexcept -> std::pair<int, std::optional<std::string>> {
    if (args.empty()) return { -1, std::nullopt };
    const auto cwd_text = std::string(cwd);

#if defined(_WIN32)
    auto command = build_windows_command(args);

    auto sa = SECURITY_ATTRIBUTES{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    auto stdout_read = HANDLE{};
    auto stdout_write = HANDLE{};
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) return { -1, std::nullopt };
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdout_read); CloseHandle(stdout_write);
        return { -1, std::nullopt };
    }

    auto si = STARTUPINFOA{};
    si.cb = sizeof(si);
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;
    si.dwFlags |= STARTF_USESTDHANDLES;

    auto pi = PROCESS_INFORMATION{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, cwd_text.c_str(), &si, &pi)) {
        CloseHandle(stdout_read); CloseHandle(stdout_write);
        return { -1, std::nullopt };
    }

    CloseHandle(stdout_write);

    auto raw = std::string();
    char buf[4096];
    auto bytes_read = DWORD{};
    while (ReadFile(stdout_read, buf, sizeof(buf), &bytes_read, nullptr) && bytes_read > 0) {
        raw.append(buf, bytes_read);
    }

    CloseHandle(stdout_read);

    WaitForSingleObject(pi.hProcess, INFINITE);
    auto exit_code = DWORD{};
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto output = std::string();
    output.reserve(raw.size());
    for (auto i = 0uz; i < raw.size(); ++i) {
        if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') continue;
        output += raw[i];
    }

    return { static_cast<int>(exit_code), std::move(output) };
#else
    auto argv = std::vector<char*>();
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.emplace_back(const_cast<char*>(arg.c_str()));
    }
    argv.emplace_back(nullptr);

    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) return { -1, std::nullopt };

    const auto pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return { -1, std::nullopt };
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        if (chdir(cwd_text.c_str()) < 0) {
            close(stdout_pipe[1]);
            _exit(126);
        }
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        execvp(argv[0], argv.data());
        _exit(126);
    }

    close(stdout_pipe[1]);

    auto output = std::string();
    char buf[4096];
    ssize_t bytes;
    while ((bytes = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
        output.append(buf, static_cast<std::size_t>(bytes));
    }
    close(stdout_pipe[0]);

    auto status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return { -1, std::nullopt };
    }

    if (WIFEXITED(status)) return { WEXITSTATUS(status), std::move(output) };
    if (WIFSIGNALED(status)) return { 128 + WTERMSIG(status), std::move(output) };
    return { 1, std::move(output) };
#endif
}
