module carven:driver.options.impl;

import :backend.generation.request;
import :driver.options;
import :support.path;
import std;

namespace {

auto compilation_option_error(CompilationOptionErrorKind kind) noexcept -> CompilationOptionError {
    return {
        .kind = kind,
        .option = std::nullopt,
    };
}

auto compilation_option_error(CompilationOptionErrorKind kind, std::string_view option) noexcept
    -> CompilationOptionError {
    return {
        .kind = kind,
        .option = option,
    };
}

} // namespace

auto parse_compilation_options(
    std::span<const std::string> args,
    CompilationCommand command
) noexcept -> std::expected<CompilationOptions, CompilationOptionError> {
    auto request = CompilationOptions {
        .command = command,
        .destination = std::nullopt,
        .test_mode = TestGenerationMode::None,
        .input_paths = {},
        .timings = false,
        .query_crafts = command == CompilationCommand::Compile,
    };
    auto has_test_option = false;

    const auto set_destination = [&](
                                     ArtifactDestination destination
                                 ) noexcept -> std::expected<void, CompilationOptionError> {
        if (request.destination) {
            return std::unexpected(compilation_option_error(
                CompilationOptionErrorKind::DestinationSpecifiedMoreThanOnce
            ));
        }
        request.destination = std::move(destination);
        return {};
    };

    constexpr auto output_path = [](std::string_view value) static noexcept
        -> std::expected<std::filesystem::path, CompilationOptionError> {
        auto path = path_from_utf8(value);
        if (path.empty()) {
            return std::unexpected(
                compilation_option_error(CompilationOptionErrorKind::EmptyOutputPath)
            );
        }
        return path;
    };
    const auto next_output_path = [&](std::size_t& index, std::string_view option) noexcept
        -> std::expected<std::filesystem::path, CompilationOptionError> {
        if (index + 1 >= args.size() || std::string_view(args[index + 1]).starts_with('-')) {
            return std::unexpected(
                compilation_option_error(CompilationOptionErrorKind::MissingOutputPath, option)
            );
        }
        return output_path(args[++index]);
    };
    for (auto index = 0uz; index < args.size(); ++index) {
        const auto arg = std::string_view(args[index]);
        if (arg == "--timings") {
            request.timings = true;
        } else if (command == CompilationCommand::Compile && arg == "--no-query") {
            request.query_crafts = false;
        } else if (command == CompilationCommand::Compile
                   && (arg == "--tests" || arg == "--tests=runner")) {
            if (has_test_option) {
                return std::unexpected(compilation_option_error(
                    CompilationOptionErrorKind::TestModeSpecifiedMoreThanOnce
                ));
            }
            request.test_mode = arg == "--tests=runner" ? TestGenerationMode::RunnerHeader
                                                        : TestGenerationMode::RunnerEntryPoint;
            has_test_option = true;
        } else if (arg == "-o") {
            auto path = next_output_path(index, arg);
            if (!path) {
                return std::unexpected(path.error());
            }
            if (const auto selected =
                    set_destination(DirectoryArtifactDestination {.root = std::move(*path)});
                !selected) {
                return std::unexpected(selected.error());
            }
        } else if (command == CompilationCommand::Compile && arg == "--stdout") {
            if (const auto selected = set_destination(StandardOutputArtifactDestination {});
                !selected) {
                return std::unexpected(selected.error());
            }
        } else if (arg.starts_with('-')) {
            return std::unexpected(
                compilation_option_error(CompilationOptionErrorKind::UnknownOption, arg)
            );
        } else {
            request.input_paths.push_back(arg);
        }
    }

    if (command == CompilationCommand::Check && request.input_paths.empty()) {
        return std::unexpected(compilation_option_error(CompilationOptionErrorKind::NoSourceInput));
    }
    if (command == CompilationCommand::Compile && !request.destination) {
        request.destination = DirectoryArtifactDestination {.root = "."};
    }
    return request;
}

auto format_compilation_option_error(const CompilationOptionError& error) noexcept -> std::string {
    switch (error.kind) {
        case CompilationOptionErrorKind::DestinationSpecifiedMoreThanOnce:
            return "artifact destination was specified more than once";
        case CompilationOptionErrorKind::TestModeSpecifiedMoreThanOnce:
            return "test emission mode was specified more than once";
        case CompilationOptionErrorKind::MissingOutputPath:
            return std::format("missing output path after '{}'", *error.option);
        case CompilationOptionErrorKind::EmptyOutputPath: return "output path is empty";
        case CompilationOptionErrorKind::UnknownOption:
            return std::format("unknown option '{}'", *error.option);
        case CompilationOptionErrorKind::NoSourceInput: return "at least one input is required";
    }
    std::unreachable();
}
