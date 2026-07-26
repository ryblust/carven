module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.driver.options;

import :compilation.request;
import :driver.options;
import std;

TEST_CASE("Compile options: defaults preserve source inputs") {
    const auto args = std::to_array<const char*>({"main.cv", "nested/worker.cv"});
    const auto result = parse_compile_command_options(args);

    REQUIRE(result.has_value());
    const auto* destination = std::get_if<DirectoryArtifactDestination>(&result->destination);
    REQUIRE(destination != nullptr);
    CHECK_EQ(destination->root, std::filesystem::path("."));
    CHECK_EQ(result->test_mode, TestEmissionMode::None);
    CHECK_FALSE(result->linkage_domain.has_value());
    REQUIRE_EQ(result->input_paths.size(), 2uz);
    CHECK_EQ(result->input_paths[0], "main.cv");
    CHECK_EQ(result->input_paths[1], "nested/worker.cv");
}

TEST_CASE("Compile options: explicit modes retain their selected values") {
    const auto output_args = std::to_array<const char*>({
        "--tests=external",
        "--output-dir=emit",
        "--linkage-domain",
        "domain",
        "main.cv",
    });
    const auto output = parse_compile_command_options(output_args);

    REQUIRE(output.has_value());
    const auto* destination = std::get_if<DirectoryArtifactDestination>(&output->destination);
    REQUIRE(destination != nullptr);
    CHECK_EQ(destination->root, std::filesystem::path("emit"));
    CHECK_EQ(output->test_mode, TestEmissionMode::ExternalRunner);
    REQUIRE(output->linkage_domain.has_value());
    CHECK_EQ(*output->linkage_domain, "domain");

    const auto stdout_args = std::to_array<const char*>({"--stdout", "--tests=default", "main.cv"});
    const auto stdout = parse_compile_command_options(stdout_args);

    REQUIRE(stdout.has_value());
    CHECK(std::holds_alternative<StandardOutputArtifactDestination>(stdout->destination));
    CHECK_EQ(stdout->test_mode, TestEmissionMode::DefaultRunner);

    const auto empty_domain_args = std::to_array<const char*>({"--linkage-domain=", "main.cv"});
    const auto empty_domain = parse_compile_command_options(empty_domain_args);
    REQUIRE(empty_domain.has_value());
    REQUIRE(empty_domain->linkage_domain.has_value());
    CHECK(empty_domain->linkage_domain->empty());
}

TEST_CASE("Compile options: invalid combinations report structured failures") {
    struct InvalidCase final {
        std::vector<const char*> args;
        CompileOptionErrorKind kind;
        std::optional<std::string_view> option;
        std::string_view message;
    };

    const auto cases = std::array {
        InvalidCase {
            .args = {"--tests", "main.cv"},
            .kind = CompileOptionErrorKind::UnknownOption,
            .option = "--tests",
            .message = "unknown option '--tests'",
        },
        InvalidCase {
            .args = {"--stdout", "-o", "emit", "main.cv"},
            .kind = CompileOptionErrorKind::DestinationSpecifiedMoreThanOnce,
            .option = std::nullopt,
            .message = "artifact destination was specified more than once",
        },
        InvalidCase {
            .args = {"-o", "--stdout", "main.cv"},
            .kind = CompileOptionErrorKind::MissingOutputPath,
            .option = "-o",
            .message = "missing output path after '-o'",
        },
        InvalidCase {
            .args = {"--tests=default", "--tests=external", "main.cv"},
            .kind = CompileOptionErrorKind::TestModeSpecifiedMoreThanOnce,
            .option = std::nullopt,
            .message = "test emission mode was specified more than once",
        },
        InvalidCase {
            .args = {"--output-dir=", "main.cv"},
            .kind = CompileOptionErrorKind::EmptyOutputPath,
            .option = std::nullopt,
            .message = "output directory is empty",
        },
        InvalidCase {
            .args = {"--unknown", "main.cv"},
            .kind = CompileOptionErrorKind::UnknownOption,
            .option = "--unknown",
            .message = "unknown option '--unknown'",
        },
        InvalidCase {
            .args = {"main.cv", "--linkage-domain"},
            .kind = CompileOptionErrorKind::MissingLinkageDomain,
            .option = std::nullopt,
            .message = "missing value after '--linkage-domain'",
        },
        InvalidCase {
            .args = {"--linkage-domain=first", "--linkage-domain", "second", "main.cv"},
            .kind = CompileOptionErrorKind::LinkageDomainSpecifiedMoreThanOnce,
            .option = std::nullopt,
            .message = "linkage domain was specified more than once",
        },
        InvalidCase {
            .args = {"--tests=default"},
            .kind = CompileOptionErrorKind::NoSourceInput,
            .option = std::nullopt,
            .message = "no source input",
        },
    };

    for (const auto& test_case : cases) {
        const auto result = parse_compile_command_options(test_case.args);
        CAPTURE(test_case.message);
        REQUIRE_FALSE(result.has_value());
        CHECK_EQ(result.error().kind, test_case.kind);
        CHECK_EQ(result.error().option, test_case.option);
        CHECK_EQ(format_compile_option_error(result.error()), test_case.message);
    }
}
