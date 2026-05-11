export module zero.common.process;

import std;

export struct ProcessResult final {
    bool started;
    int exit_code;
};

export auto run_process(std::span<const std::string> args) noexcept -> ProcessResult;
