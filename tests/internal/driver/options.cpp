module carven:test.internal.driver.options;

import :backend.generation.request;
import :driver.options;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Compile options: defaults preserve source inputs", [] static noexcept {
        const auto args = std::to_array<const char*>({"main.cv", "nested/worker.cv"});
        const auto result = parse_compile_command_options(args);

        if (!ct::expect(result.has_value())) {
            return;
        }
        const auto* destination = std::get_if<DirectoryArtifactDestination>(&result->destination);
        if (!ct::expect(destination != nullptr)) {
            return;
        }
        ct::expect_equal(destination->root, std::filesystem::path("."));
        ct::expect_equal(result->test_mode, TestGenerationMode::None);
        ct::expect(!(result->linkage_domain.has_value()));
        if (!ct::expect_equal(result->input_paths.size(), 2uz)) {
            return;
        }
        ct::expect_equal(result->input_paths[0], std::string_view("main.cv"));
        ct::expect_equal(result->input_paths[1], std::string_view("nested/worker.cv"));
    });

    ct::test("Compile options: explicit modes retain their selected values", [] static noexcept {
        const auto output_args = std::to_array<const char*>({
            "--tests=external",
            "--output-dir=emit",
            "--linkage-domain=-domain=value",
            "main.cv",
        });
        const auto output = parse_compile_command_options(output_args);

        if (!ct::expect(output.has_value())) {
            return;
        }
        const auto* destination = std::get_if<DirectoryArtifactDestination>(&output->destination);
        if (!ct::expect(destination != nullptr)) {
            return;
        }
        ct::expect_equal(destination->root, std::filesystem::path("emit"));
        ct::expect_equal(output->test_mode, TestGenerationMode::RunnerHeader);
        if (!ct::expect(output->linkage_domain.has_value())) {
            return;
        }
        ct::expect_equal(output->linkage_domain->kind(), LinkageDomainKind::Explicit);
        ct::expect_equal(output->linkage_domain->value(), std::string_view("-domain=value"));

        const auto stdout_args = std::to_array<const char*>({"--stdout", "--tests", "main.cv"});
        const auto stdout = parse_compile_command_options(stdout_args);

        if (!ct::expect(stdout.has_value())) {
            return;
        }
        ct::expect(std::holds_alternative<StandardOutputArtifactDestination>(stdout->destination));
        ct::expect_equal(stdout->test_mode, TestGenerationMode::RunnerEntryPoint);
    });

    ct::test(
        "Compile options: invalid combinations report structured failures",
        [] static noexcept {
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

            ct::each(cases, &InvalidCase::name, [&](const InvalidCase& test_case) noexcept {
                const auto result = parse_compile_command_options(test_case.args);
                if (!(ct::expect(!(result.has_value())))) {
                    return;
                }
                ct::expect_equal(result.error().kind, test_case.kind);
                ct::expect((result.error().option == test_case.option));
            });
        }
    );
});

} // namespace
