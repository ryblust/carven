module carven:driver.process;

import std;

// Inherits the working directory, environment and standard streams; never uses a shell.
auto run_process(std::vector<std::string> arguments) noexcept -> std::expected<int, std::string>;
auto current_executable_path(std::string_view invocation) noexcept -> std::filesystem::path;
auto create_run_directory() noexcept -> std::expected<std::filesystem::path, std::string>;
