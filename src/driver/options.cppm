module carven:driver.options;

import :backend.generation.request;
import :driver.sources;
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
    std::vector<SourceInput> mapped_inputs;
    bool timings;
};

enum class CompilationOptionErrorKind {
    DestinationSpecifiedMoreThanOnce,
    TestModeSpecifiedMoreThanOnce,
    MissingOutputPath,
    EmptyOutputPath,
    InvalidSourceInput,
    UnknownOption,
    NoSourceInput,
};

struct CompilationOptionError final {
    CompilationOptionErrorKind kind;
    std::optional<std::string_view> option;
};

auto parse_compilation_options(
    std::span<const std::string> args,
    CompilationCommand command = CompilationCommand::Compile
) noexcept -> std::expected<CompilationOptions, CompilationOptionError>;

auto format_compilation_option_error(const CompilationOptionError& error) noexcept -> std::string;
