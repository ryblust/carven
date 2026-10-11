module carven:driver.options.impl;

import :backend.generation.request;
import :driver.options;
import :driver.sources;
import :source.module_path;
import :support.path;
import std;

namespace {

auto compile_option_error(CompileOptionErrorKind kind) noexcept -> CompileOptionError {
    return {
        .kind = kind,
        .option = std::nullopt,
    };
}

auto compile_option_error(CompileOptionErrorKind kind, std::string_view option) noexcept
    -> CompileOptionError {
    return {
        .kind = kind,
        .option = option,
    };
}

} // namespace

auto parse_compile_command_options(std::span<const char* const> args) noexcept
    -> std::expected<CompileCommandOptions, CompileOptionError> {
    auto request = CompileCommandOptions {
        .destination = DirectoryArtifactDestination {.root = "."},
        .artifact_manifest = std::nullopt,
        .test_mode = TestGenerationMode::None,
        .linkage_domain = std::nullopt,
        .library = std::nullopt,
        .input_paths = {},
        .mapped_inputs = {},
        .explicit_inputs = false,
        .timings = false,
    };
    auto has_test_option = false;
    auto has_destination_option = false;
    auto has_linkage_domain_option = false;
    auto library_domain = std::optional<LinkageDomain>();
    auto library_modules = std::vector<CanonicalModulePath>();
    auto emit_library = false;

    const auto set_destination =
        [&](ArtifactDestination destination) noexcept -> std::expected<void, CompileOptionError> {
        if (has_destination_option) {
            return std::unexpected(
                compile_option_error(CompileOptionErrorKind::DestinationSpecifiedMoreThanOnce)
            );
        }
        request.destination = std::move(destination);
        has_destination_option = true;
        return {};
    };

    const auto output_path = [](std::string_view value) static noexcept
        -> std::expected<std::filesystem::path, CompileOptionError> {
        auto path = std::filesystem::path(value);
        if (path.empty()) {
            return std::unexpected(compile_option_error(CompileOptionErrorKind::EmptyOutputPath));
        }
        return path;
    };
    const auto next_output_path = [&](std::size_t& index, std::string_view option) noexcept
        -> std::expected<std::filesystem::path, CompileOptionError> {
        if (index + 1 >= args.size() || std::string_view(args[index + 1]).starts_with('-')) {
            return std::unexpected(
                compile_option_error(CompileOptionErrorKind::MissingOutputPath, option)
            );
        }
        return output_path(args[++index]);
    };
    const auto linkage_domain = [](std::string_view value) static noexcept
        -> std::expected<LinkageDomain, CompileOptionError> {
        auto domain = LinkageDomain::explicit_value(std::string(value));
        if (!domain.has_value()) {
            return std::unexpected(
                compile_option_error(CompileOptionErrorKind::EmptyLinkageDomain)
            );
        }
        return std::move(*domain);
    };

    for (auto index = 0uz; index < args.size(); ++index) {
        const auto arg = std::string_view(args[index]);
        if (arg == "--timings") {
            request.timings = true;
        } else if (arg == "--explicit-inputs") {
            request.explicit_inputs = true;
        } else if (arg.starts_with("--source=")) {
            auto input = parse_source_input(arg.substr(std::string_view("--source=").size()));
            if (!input) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::InvalidSourceInput, arg)
                );
            }
            request.mapped_inputs.push_back(std::move(*input));
        } else if (arg.starts_with("--artifact-manifest=")) {
            if (request.artifact_manifest) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::ManifestSpecifiedMoreThanOnce)
                );
            }
            const auto value = arg.substr(std::string_view("--artifact-manifest=").size());
            if (value.empty()) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::EmptyManifestPath)
                );
            }
            request.artifact_manifest = path_from_utf8(value);
        } else if (arg == "--tests=main" || arg == "--tests=runner") {
            if (has_test_option) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::TestModeSpecifiedMoreThanOnce)
                );
            }
            request.test_mode = arg == "--tests=runner" ? TestGenerationMode::RunnerHeader
                                                        : TestGenerationMode::RunnerEntryPoint;
            has_test_option = true;
        } else if (arg == "-o" || arg == "--output-dir") {
            auto path = next_output_path(index, arg);
            if (!path) {
                return std::unexpected(path.error());
            }
            if (const auto selected =
                    set_destination(DirectoryArtifactDestination {.root = std::move(*path)});
                !selected) {
                return std::unexpected(selected.error());
            }
        } else if (arg.starts_with("--output-dir=")) {
            auto path = output_path(arg.substr(std::string_view("--output-dir=").size()));
            if (!path) {
                return std::unexpected(path.error());
            }
            if (const auto selected =
                    set_destination(DirectoryArtifactDestination {.root = std::move(*path)});
                !selected) {
                return std::unexpected(selected.error());
            }
        } else if (arg == "--stdout") {
            if (const auto selected = set_destination(StandardOutputArtifactDestination {});
                !selected) {
                return std::unexpected(selected.error());
            }
        } else if (arg.starts_with("--linkage-domain=")) {
            if (has_linkage_domain_option) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::LinkageDomainSpecifiedMoreThanOnce)
                );
            }
            auto domain = linkage_domain(arg.substr(std::string_view("--linkage-domain=").size()));
            if (!domain.has_value()) {
                return std::unexpected(domain.error());
            }
            request.linkage_domain = std::move(*domain);
            has_linkage_domain_option = true;
        } else if (arg.starts_with("--library-domain=")) {
            if (library_domain) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::LibraryDomainSpecifiedMoreThanOnce)
                );
            }
            auto domain = linkage_domain(arg.substr(std::string_view("--library-domain=").size()));
            if (!domain) {
                return std::unexpected(domain.error());
            }
            library_domain = std::move(*domain);
        } else if (arg.starts_with("--library-module=")) {
            const auto value = arg.substr(std::string_view("--library-module=").size());
            auto module_path = CanonicalModulePath::from_value(value);
            if (!module_path) {
                return std::unexpected(
                    compile_option_error(CompileOptionErrorKind::InvalidLibraryModule, value)
                );
            }
            library_modules.push_back(std::move(*module_path));
        } else if (arg == "--emit-library") {
            emit_library = true;
        } else if (arg.starts_with('-')) {
            return std::unexpected(
                compile_option_error(CompileOptionErrorKind::UnknownOption, arg)
            );
        } else {
            request.input_paths.push_back(arg);
        }
    }

    if (request.input_paths.empty() && request.mapped_inputs.empty()) {
        return std::unexpected(compile_option_error(CompileOptionErrorKind::NoSourceInput));
    }
    if (library_domain || !library_modules.empty() || emit_library) {
        if (!library_domain || library_modules.empty()) {
            return std::unexpected(
                compile_option_error(CompileOptionErrorKind::IncompleteLibraryOptions)
            );
        }
        request.library = SourceLibrary {
            .modules = std::move(library_modules),
            .linkage_domain = std::move(*library_domain),
            .emit_implementations = emit_library,
        };
    }
    return request;
}

auto format_compile_option_error(const CompileOptionError& error) noexcept -> std::string {
    switch (error.kind) {
        case CompileOptionErrorKind::DestinationSpecifiedMoreThanOnce:
            return "artifact destination was specified more than once";
        case CompileOptionErrorKind::TestModeSpecifiedMoreThanOnce:
            return "test emission mode was specified more than once";
        case CompileOptionErrorKind::MissingOutputPath:
            return std::format("missing output path after '{}'", *error.option);
        case CompileOptionErrorKind::EmptyOutputPath: return "output directory is empty";
        case CompileOptionErrorKind::ManifestSpecifiedMoreThanOnce:
            return "artifact manifest was specified more than once";
        case CompileOptionErrorKind::EmptyManifestPath: return "artifact manifest path is empty";
        case CompileOptionErrorKind::LinkageDomainSpecifiedMoreThanOnce:
            return "linkage domain was specified more than once";
        case CompileOptionErrorKind::EmptyLinkageDomain: return "linkage domain is empty";
        case CompileOptionErrorKind::LibraryDomainSpecifiedMoreThanOnce:
            return "library domain was specified more than once";
        case CompileOptionErrorKind::InvalidLibraryModule:
            return std::format("invalid canonical library module '{}'", *error.option);
        case CompileOptionErrorKind::IncompleteLibraryOptions:
            return "library generation requires --library-domain and at least one --library-module";
        case CompileOptionErrorKind::InvalidSourceInput:
            return std::format(
                "invalid source mapping '{}'; expected --source=<canonical-module>=<path.cv>",
                *error.option
            );
        case CompileOptionErrorKind::UnknownOption:
            return std::format("unknown option '{}'", *error.option);
        case CompileOptionErrorKind::NoSourceInput:
            return "compile requires at least one source file";
    }
    std::unreachable();
}
