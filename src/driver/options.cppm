module carven:driver.options;

import :backend.generation.request;
import std;

struct DirectoryArtifactDestination final {
    std::filesystem::path root;
};

struct StandardOutputArtifactDestination final {};

using ArtifactDestination =
    std::variant<DirectoryArtifactDestination, StandardOutputArtifactDestination>;

enum class CompilationCommand { Check, Compile };

struct CompilationOptions final {
    CompilationCommand command;
    std::optional<ArtifactDestination> destination;
    TestGenerationMode test_mode;
    std::vector<std::string_view> input_paths;
    bool timings;
    bool query_crafts;
};

enum class CompilationOptionErrorKind {
    DestinationSpecifiedMoreThanOnce,
    TestModeSpecifiedMoreThanOnce,
    MissingOutputPath,
    EmptyOutputPath,
    UnknownOption,
    NoSourceInput,
};

struct CompilationOptionError final {
    CompilationOptionErrorKind kind;
    std::optional<std::string_view> option;
};

auto parse_compilation_options(
    std::span<const std::string> args,
    CompilationCommand command
) noexcept -> std::expected<CompilationOptions, CompilationOptionError>;

auto format_compilation_option_error(const CompilationOptionError& error) noexcept -> std::string;
