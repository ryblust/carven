module carven:test.internal.driver.options;

import :backend.generation.request;
import :driver.options;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compile options: defaults preserve source inputs"_test = [] static noexcept {
        const auto args = std::to_array<const char*>({"main.cv", "nested/worker.cv"});
        const auto result = parse_compile_command_options(args);

        if (!expect(result.has_value())) {
            return;
        }
        const auto* destination = std::get_if<DirectoryArtifactDestination>(&result->destination);
        if (!expect(destination != nullptr)) {
            return;
        }
        expect_equal(destination->root, std::filesystem::path("."));
        expect_equal(result->test_mode, TestGenerationMode::None);
        expect(!(result->linkage_domain.has_value()));
        if (!expect_equal(result->input_paths.size(), 2uz)) {
            return;
        }
        expect_equal(result->input_paths[0], std::string_view("main.cv"));
        expect_equal(result->input_paths[1], std::string_view("nested/worker.cv"));
    };

    "Compile options: explicit modes retain their selected values"_test = [] static noexcept {
        const auto output_args = std::to_array<const char*>({
            "--tests=external",
            "--output-dir=emit",
            "--linkage-domain=-domain=value",
            "main.cv",
        });
        const auto output = parse_compile_command_options(output_args);

        if (!expect(output.has_value())) {
            return;
        }
        const auto* destination = std::get_if<DirectoryArtifactDestination>(&output->destination);
        if (!expect(destination != nullptr)) {
            return;
        }
        expect_equal(destination->root, std::filesystem::path("emit"));
        expect_equal(output->test_mode, TestGenerationMode::RunnerHeader);
        if (!expect(output->linkage_domain.has_value())) {
            return;
        }
        expect_equal(output->linkage_domain->kind(), LinkageDomainKind::Explicit);
        expect_equal(output->linkage_domain->value(), std::string_view("-domain=value"));

        const auto stdout_args = std::to_array<const char*>({"--stdout", "--tests", "main.cv"});
        const auto stdout = parse_compile_command_options(stdout_args);

        if (!expect(stdout.has_value())) {
            return;
        }
        expect(std::holds_alternative<StandardOutputArtifactDestination>(stdout->destination));
        expect_equal(stdout->test_mode, TestGenerationMode::RunnerEntryPoint);
    };

    "Compile options: invalid combinations report structured failures"_test = [] static noexcept {
        struct InvalidCase final {
            std::string_view name;
            std::vector<const char*> args;
            CompileOptionErrorKind kind;
            std::optional<std::string_view> option;
        };

        const auto cases = std::array {
            InvalidCase {
                .name = "repeated test mode",
                .args = {"--tests", "--tests=default", "main.cv"},
                .kind = CompileOptionErrorKind::TestModeSpecifiedMoreThanOnce,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "conflicting destinations",
                .args = {"--stdout", "-o", "emit", "main.cv"},
                .kind = CompileOptionErrorKind::DestinationSpecifiedMoreThanOnce,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "missing output path",
                .args = {"-o", "--stdout", "main.cv"},
                .kind = CompileOptionErrorKind::MissingOutputPath,
                .option = "-o",
            },
            InvalidCase {
                .name = "empty output path",
                .args = {"--output-dir=", "main.cv"},
                .kind = CompileOptionErrorKind::EmptyOutputPath,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "unknown option",
                .args = {"--unknown", "main.cv"},
                .kind = CompileOptionErrorKind::UnknownOption,
                .option = "--unknown",
            },
            InvalidCase {
                .name = "linkage domain requires equals",
                .args = {"--linkage-domain", "domain", "main.cv"},
                .kind = CompileOptionErrorKind::UnknownOption,
                .option = "--linkage-domain",
            },
            InvalidCase {
                .name = "empty linkage domain",
                .args = {"--linkage-domain=", "main.cv"},
                .kind = CompileOptionErrorKind::EmptyLinkageDomain,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "repeated linkage domain",
                .args = {"--linkage-domain=first", "--linkage-domain=second", "main.cv"},
                .kind = CompileOptionErrorKind::LinkageDomainSpecifiedMoreThanOnce,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "missing source input",
                .args = {"--tests=default"},
                .kind = CompileOptionErrorKind::NoSourceInput,
                .option = std::nullopt,
            },
        };

        each(cases, &InvalidCase::name, [&](const InvalidCase& test_case) noexcept {
            const auto result = parse_compile_command_options(test_case.args);
            if (!(expect(!(result.has_value())))) {
                return;
            }
            expect_equal(result.error().kind, test_case.kind);
            expect((result.error().option == test_case.option));
        });
    };
});

} // namespace
