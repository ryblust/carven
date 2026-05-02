module;

#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>

export module zero.common.process;

import std;

export struct ProcessResult final {
    bool started;
    int exit_code;
};

export auto run_process(std::span<const std::string> args) noexcept -> ProcessResult {
    if (args.empty()) return { .started = false, .exit_code = 1 };

#if defined(_WIN32)
    return { .started = false, .exit_code = 1 };
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
