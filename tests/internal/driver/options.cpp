module carven:test.internal.driver.options;

import :backend.generation.request;
import :driver.options;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compilation options: defaults preserve source inputs"_test = [] static noexcept {
        const auto args = std::to_array<std::string>({"main.cv", "nested/worker.cv"});
        const auto result = parse_compilation_options(args);

        if (!expect(result.has_value())) {
            return;
        }
        const auto* destination = result->destination
            ? std::get_if<DirectoryArtifactDestination>(&*result->destination)
            : nullptr;
        if (!expect(destination != nullptr)) {
            return;
        }
        expect_equal(destination->root, std::filesystem::path("."));
        expect_equal(result->test_mode, TestGenerationMode::None);
        if (!expect_equal(result->input_paths.size(), 2uz)) {
            return;
        }
        expect_equal(result->input_paths[0], std::string_view("main.cv"));
        expect_equal(result->input_paths[1], std::string_view("nested/worker.cv"));
        const auto check = parse_compilation_options(args, CompilationCommand::Check);
        require(check.has_value());
        expect(!check->destination.has_value());
        expect_equal(check->input_paths, result->input_paths);
    };

    "Compilation options: explicit modes retain their selected values"_test = [] static noexcept {
        const auto output_args = std::to_array<std::string>({
            "--tests=runner",
            "-o",
            "emit",
            "main.cv",
        });
        const auto output = parse_compilation_options(output_args);

        if (!expect(output.has_value())) {
            return;
        }
        const auto* destination = output->destination
            ? std::get_if<DirectoryArtifactDestination>(&*output->destination)
            : nullptr;
        if (!expect(destination != nullptr)) {
            return;
        }
        expect_equal(destination->root, std::filesystem::path("emit"));
        expect_equal(output->test_mode, TestGenerationMode::RunnerHeader);
        const auto stdout_args = std::to_array<std::string>({"--stdout", "--tests", "main.cv"});
        const auto stdout = parse_compilation_options(stdout_args);

        if (!expect(stdout.has_value())) {
            return;
        }
        expect(
            stdout->destination
            && std::holds_alternative<StandardOutputArtifactDestination>(*stdout->destination)
        );
        expect_equal(stdout->test_mode, TestGenerationMode::RunnerEntryPoint);
    };

    "Compilation options: artifact directories use the ordinary input and output path"_test =
        [] static noexcept {
            const auto args =
                std::to_array<std::string>({"standard", "main.cv", "-o", "app.output"});
            for (const auto command : {CompilationCommand::Check, CompilationCommand::Compile}) {
                const auto result = parse_compilation_options(args, command);
                if (!expect(result.has_value())) {
                    return;
                }
                const auto* destination = result->destination
                    ? std::get_if<DirectoryArtifactDestination>(&*result->destination)
                    : nullptr;
                if (!expect(destination != nullptr)) {
                    return;
                }
                expect_equal(destination->root, std::filesystem::path("app.output"));
                expect_equal(result->input_paths.size(), 2uz);
                expect_equal(result->input_paths.front(), std::string_view("standard"));
            }
        };

    "Compilation options: invalid combinations report structured failures"_test =
        [] static noexcept {
            struct InvalidCase final {
                std::string_view name;
                std::vector<std::string> args;
                CompilationOptionErrorKind kind;
                std::optional<std::string_view> option;
            };

            const auto cases = std::array {
                InvalidCase {
                    .name = "repeated test mode",
                    .args = {"--tests=runner", "--tests", "main.cv"},
                    .kind = CompilationOptionErrorKind::TestModeSpecifiedMoreThanOnce,
                    .option = std::nullopt,
                },
                InvalidCase {
                    .name = "conflicting destinations",
                    .args = {"--stdout", "-o", "emit", "main.cv"},
                    .kind = CompilationOptionErrorKind::DestinationSpecifiedMoreThanOnce,
                    .option = std::nullopt,
                },
                InvalidCase {
                    .name = "missing output path",
                    .args = {"-o", "--stdout", "main.cv"},
                    .kind = CompilationOptionErrorKind::MissingOutputPath,
                    .option = "-o",
                },
                InvalidCase {
                    .name = "empty output path",
                    .args = {"-o", "", "main.cv"},
                    .kind = CompilationOptionErrorKind::EmptyOutputPath,
                    .option = std::nullopt,
                },
                InvalidCase {
                    .name = "unknown option",
                    .args = {"--unknown", "main.cv"},
                    .kind = CompilationOptionErrorKind::UnknownOption,
                    .option = "--unknown",
                },
                InvalidCase {
                    .name = "missing source input",
                    .args = {"--tests"},
                    .kind = CompilationOptionErrorKind::NoSourceInput,
                    .option = std::nullopt,
                },
            };

            each(cases, &InvalidCase::name, [&](const InvalidCase& test_case) noexcept {
                const auto result = parse_compilation_options(test_case.args);
                if (!(expect(!(result.has_value())))) {
                    return;
                }
                expect_equal(result.error().kind, test_case.kind);
                expect((result.error().option == test_case.option));
            });
        };

    "Compilation options: mapped inputs retain source ownership"_test = [] static noexcept {
        const auto args = std::to_array<std::string>({
            "--source=app.main=/project/main.cv",
            "--tests",
        });
        const auto request = parse_compilation_options(args);
        require(request.has_value());
        require_equal(request->mapped_inputs.size(), 1uz);
        expect_equal(
            request->mapped_inputs.front().module_path.value(),
            std::string_view("app.main")
        );
        expect_equal(request->test_mode, TestGenerationMode::RunnerEntryPoint);
    };
});

} // namespace
