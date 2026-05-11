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

module zero.common.process;

import std;

auto run_process(std::span<const std::string> args) noexcept -> ProcessResult {
    if (args.empty()) return { .started = false, .exit_code = 1 };

#if defined(_WIN32)
    auto command = std::string();

    for (auto i = 0uz; i < args.size(); ++i) {
        if (i > 0) command.push_back(' ');
        const auto needs_quotes = args[i].contains(' ') || args[i].contains('\t');
        if (needs_quotes) command.push_back('"');
        command.append(args[i]);
        if (needs_quotes) command.push_back('"');
    }

    auto si = STARTUPINFOA();
    si.cb = sizeof(si);
    auto pi = PROCESS_INFORMATION();

    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return { .started = false, .exit_code = 1 };
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    auto exit_code = DWORD();
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return { .started = true, .exit_code = static_cast<int>(exit_code) };
#else
    auto argv = std::vector<char*>();
    argv.reserve(args.size() + 1);

    for (const auto& arg : args) {
        argv.emplace_back(const_cast<char*>(arg.c_str()));
    }
    argv.emplace_back(nullptr);

    const auto pid = fork();
    if (pid < 0) return { .started = false, .exit_code = 1 };

    if (pid == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }

    auto status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return { .started = false, .exit_code = 1 };
    }

    if (WIFEXITED(status)) {
        return { .started = true, .exit_code = WEXITSTATUS(status) };
    }

    if (WIFSIGNALED(status)) {
        return { .started = true, .exit_code = 128 + WTERMSIG(status) };
    }

    return { .started = true, .exit_code = 1 };
#endif
}
