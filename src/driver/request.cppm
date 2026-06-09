export module carven.driver.request;

import std;

export struct CommandError final {
    std::string command;
    std::string message;
};

export struct SingleFileRun final {
    std::string_view source_file;
    std::vector<std::string_view> args;
};

export struct ProjectRun final {
    std::optional<std::string_view> target;
    std::vector<std::string_view> args;
};

export struct SingleFileBuild final {
    std::string_view source_file;
};

export struct ProjectBuild final {
    std::optional<std::string_view> target;
};

export using RunMode = std::variant<SingleFileRun, ProjectRun>;
export using BuildMode = std::variant<SingleFileBuild, ProjectBuild>;

export struct InitRequest final {
    std::string_view project_dir;
    std::uint8_t language_standard = 23;
};

export struct RunRequest final {
    std::uint8_t language_standard = 23;
    bool import_std = false;
    RunMode mode;
};

export struct BuildRequest final {
    std::uint8_t language_standard = 23;
    bool import_std = false;
    BuildMode mode;
};

export struct TranspileRequest final {
    std::uint8_t language_standard = 23;
    bool import_std = false;
    std::optional<std::string_view> output_file;
    std::vector<std::string_view> source_files;
};

export struct DumpRequest final {
    std::string_view source_file;
    bool only_tokens = false;
    bool only_ast = false;
};

export struct CheckRequest final {
    std::string_view source_file;
};

export using CommandRequest = std::variant<InitRequest, RunRequest, BuildRequest, TranspileRequest, DumpRequest, CheckRequest>;

export struct CommandInvocation final {
    std::string_view carven_executable;
    CommandRequest request;
};
