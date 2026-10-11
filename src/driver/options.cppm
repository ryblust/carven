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

struct CompileCommandOptions final {
    ArtifactDestination destination;
    std::optional<std::filesystem::path> artifact_manifest;
    TestGenerationMode test_mode;
    std::optional<LinkageDomain> linkage_domain;
    std::optional<SourceLibrary> library;
    std::vector<std::string_view> input_paths;
    std::vector<SourceInput> mapped_inputs;
    bool explicit_inputs;
    bool timings;
};

enum class CompileOptionErrorKind {
    DestinationSpecifiedMoreThanOnce,
    TestModeSpecifiedMoreThanOnce,
    MissingOutputPath,
    EmptyOutputPath,
    ManifestSpecifiedMoreThanOnce,
    EmptyManifestPath,
    LinkageDomainSpecifiedMoreThanOnce,
    EmptyLinkageDomain,
    LibraryDomainSpecifiedMoreThanOnce,
    InvalidLibraryModule,
    IncompleteLibraryOptions,
    InvalidSourceInput,
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
