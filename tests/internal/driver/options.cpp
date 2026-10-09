module carven:test.internal.driver.options;

import :backend.generation.request;
import :driver.options;
import :source.module_path;
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
        expect(!result->artifact_manifest.has_value());
        if (!expect_equal(result->input_paths.size(), 2uz)) {
            return;
        }
        expect_equal(result->input_paths[0], std::string_view("main.cv"));
        expect_equal(result->input_paths[1], std::string_view("nested/worker.cv"));
    };

    "Compile options: explicit modes retain their selected values"_test = [] static noexcept {
        const auto output_args = std::to_array<const char*>({
            "--tests=runner",
            "--output-dir=emit",
            "--linkage-domain=-domain=value",
            "--artifact-manifest=metadata/build.json",
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
        expect_equal(output->artifact_manifest, std::filesystem::path("metadata/build.json"));
        if (!expect(output->linkage_domain.has_value())) {
            return;
        }
        expect_equal(output->linkage_domain->kind(), LinkageDomainKind::Explicit);
        expect_equal(output->linkage_domain->value(), std::string_view("-domain=value"));

        const auto stdout_args =
            std::to_array<const char*>({"--stdout", "--tests=main", "main.cv"});
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
                .args = {"--tests=runner", "--tests=main", "main.cv"},
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
                .name = "empty manifest path",
                .args = {"--artifact-manifest=", "main.cv"},
                .kind = CompileOptionErrorKind::EmptyManifestPath,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "repeated manifest",
                .args =
                    {"--artifact-manifest=first.json",
                     "--artifact-manifest=second.json",
                     "main.cv"},
                .kind = CompileOptionErrorKind::ManifestSpecifiedMoreThanOnce,
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
                .name = "library without identity",
                .args = {"--library-module=crafts.carven.std.math", "main.cv"},
                .kind = CompileOptionErrorKind::IncompleteLibraryOptions,
                .option = std::nullopt,
            },
            InvalidCase {
                .name = "invalid canonical library module",
                .args = {"--library-module=../math", "main.cv"},
                .kind = CompileOptionErrorKind::InvalidLibraryModule,
                .option = "../math",
            },
            InvalidCase {
                .name = "bare compile test mode",
                .args = {"--tests", "main.cv"},
                .kind = CompileOptionErrorKind::UnknownOption,
                .option = "--tests",
            },
            InvalidCase {
                .name = "missing source input",
                .args = {"--tests=main"},
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

    "Compile options: library identity and emission are independent of application tests"_test =
        [] static noexcept {
            auto args = std::vector<const char*> {
                "--library-domain=installed-toolchain",
                "--library-module=crafts.carven.std.math",
                "--library-module=crafts.carven.std.time.clock",
                "--tests=runner",
                "main.cv",
            };
            const auto consumer = parse_compile_command_options(args);
            require(consumer.has_value());
            require(consumer->library.has_value());
            const auto& library = *consumer->library;
            expect_equal(library.linkage_domain.value(), std::string_view("installed-toolchain"));
            require_equal(library.modules.size(), 2uz);
            expect_equal(
                library.modules[1].value(),
                std::string_view("crafts.carven.std.time.clock")
            );
            expect(!library.emit_implementations);
            expect_equal(consumer->test_mode, TestGenerationMode::RunnerHeader);
            args.push_back("--emit-library");
            const auto provider = parse_compile_command_options(args);
            require(provider.has_value());
            expect(provider->library->emit_implementations);
        };
});

} // namespace
