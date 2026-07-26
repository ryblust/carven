module carven:driver.options;

import :compilation.request;
import std;

struct DirectoryArtifactDestination final {
    std::filesystem::path root;
};

struct StandardOutputArtifactDestination final {};

using ArtifactDestination =
    std::variant<DirectoryArtifactDestination, StandardOutputArtifactDestination>;

struct CompileCommandOptions final {
    ArtifactDestination destination;
    TestEmissionMode test_mode;
    std::optional<std::string_view> linkage_domain;
    std::vector<std::string_view> input_paths;
};

enum class CompileOptionErrorKind {
    DestinationSpecifiedMoreThanOnce,
    TestModeSpecifiedMoreThanOnce,
    MissingOutputPath,
    EmptyOutputPath,
    LinkageDomainSpecifiedMoreThanOnce,
    MissingLinkageDomain,
    UnknownOption,
    NoSourceInput,
};

struct CompileOptionError final {
    CompileOptionErrorKind kind;
    std::optional<std::string_view> option;
};

auto parse_compile_command_options(std::span<const char* const> args) noexcept
    -> std::expected<CompileCommandOptions, CompileOptionError>;

auto format_compile_option_error(const CompileOptionError& error) noexcept -> std::string;
