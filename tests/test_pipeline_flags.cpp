#include "test_helpers.h"

import carven.driver.command;
import std;

static auto parse(std::string_view command, std::span<const char* const> args) noexcept -> std::expected<CommandInvocation, CommandError> {
    return parse_command(command, args, "/tmp/carven");
}

TEST_CASE("Command parser: run") {
    SUBCASE("project default") {
        const auto args = std::array<const char*, 0>{};
        const auto invocation = parse("run", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<RunRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        const auto mode = std::get_if<ProjectRun>(&request->mode);
        REQUIRE(mode != nullptr);
        CHECK(!mode->target.has_value());
        CHECK(mode->args.empty());
    }

    SUBCASE("single file with forwarded args") {
        const auto args = std::array { "main.cv", "--", "one", "two" };
        const auto invocation = parse("run", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<RunRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        const auto mode = std::get_if<SingleFileRun>(&request->mode);
        REQUIRE(mode != nullptr);
        CHECK_EQ(mode->source_file, "main.cv");
        REQUIRE_EQ(mode->args.size(), 2u);
        CHECK_EQ(mode->args[0], "one");
        CHECK_EQ(mode->args[1], "two");
    }

    SUBCASE("project target with forwarded args") {
        const auto args = std::array { "-std=c++20", "--import-std", "app", "--", "--name", "Ada" };
        const auto invocation = parse("run", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<RunRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK_EQ(request->language_standard, 20);
        CHECK(request->import_std);

        const auto mode = std::get_if<ProjectRun>(&request->mode);
        REQUIRE(mode != nullptr);
        REQUIRE(mode->target.has_value());
        CHECK_EQ(*mode->target, "app");
        REQUIRE_EQ(mode->args.size(), 2u);
        CHECK_EQ(mode->args[0], "--name");
        CHECK_EQ(mode->args[1], "Ada");
    }

    SUBCASE("runtime args without target is rejected") {
        const auto args = std::array { "--", "--version" };
        const auto invocation = parse("run", args);
        REQUIRE(!invocation.has_value());
        CHECK_EQ(invocation.error().command, "run");
        CHECK(invocation.error().message.contains("explicit target"));
    }
}

TEST_CASE("Command parser: build") {
    SUBCASE("project default") {
        const auto args = std::array<const char*, 0>{};
        const auto invocation = parse("build", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<BuildRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        const auto mode = std::get_if<ProjectBuild>(&request->mode);
        REQUIRE(mode != nullptr);
        CHECK(!mode->target.has_value());
    }

    SUBCASE("single file") {
        const auto args = std::array { "-std=c++26", "--import-std", "main.cv" };
        const auto invocation = parse("build", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<BuildRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK_EQ(request->language_standard, 26);
        CHECK(request->import_std);

        const auto mode = std::get_if<SingleFileBuild>(&request->mode);
        REQUIRE(mode != nullptr);
        CHECK_EQ(mode->source_file, "main.cv");
    }

    SUBCASE("project target") {
        const auto args = std::array { "app" };
        const auto invocation = parse("build", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<BuildRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        const auto mode = std::get_if<ProjectBuild>(&request->mode);
        REQUIRE(mode != nullptr);
        REQUIRE(mode->target.has_value());
        CHECK_EQ(*mode->target, "app");
    }
}

TEST_CASE("Command parser: init, dump, check") {
    SUBCASE("init") {
        const auto args = std::array { "-std=c++20", "hello" };
        const auto invocation = parse("init", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<InitRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK_EQ(request->language_standard, 20);
        CHECK_EQ(request->project_dir, "hello");
    }

    SUBCASE("dump") {
        const auto args = std::array { "--only-tokens", "main.cv" };
        const auto invocation = parse("dump", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<DumpRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK_EQ(request->source_file, "main.cv");
        CHECK(request->only_tokens);
        CHECK(!request->only_ast);
    }

    SUBCASE("check") {
        const auto args = std::array { "main.cv" };
        const auto invocation = parse("check", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<CheckRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK_EQ(request->source_file, "main.cv");
    }
}

TEST_CASE("Command parser: transpile") {
    SUBCASE("stdout mode accepts multiple files") {
        const auto args = std::array { "a.cv", "b.cv" };
        const auto invocation = parse("transpile", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<TranspileRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK(!request->output_file.has_value());
        REQUIRE_EQ(request->source_files.size(), 2u);
        CHECK_EQ(request->source_files[0], "a.cv");
        CHECK_EQ(request->source_files[1], "b.cv");
    }

    SUBCASE("output mode requires one file") {
        const auto args = std::array { "-std=c++20", "--import-std", "-o", "out.cpp", "main.cv" };
        const auto invocation = parse("transpile", args);
        REQUIRE(invocation.has_value());

        const auto request = std::get_if<TranspileRequest>(&invocation->request);
        REQUIRE(request != nullptr);
        CHECK_EQ(request->language_standard, 20);
        CHECK(request->import_std);
        REQUIRE(request->output_file.has_value());
        CHECK_EQ(*request->output_file, "out.cpp");
        REQUIRE_EQ(request->source_files.size(), 1u);
        CHECK_EQ(request->source_files[0], "main.cv");
    }
}

TEST_CASE("Command parser: errors") {
    SUBCASE("unknown flag") {
        const auto args = std::array { "--unknown" };
        const auto invocation = parse("run", args);
        CHECK(!invocation.has_value());
    }

    SUBCASE("unknown standard") {
        const auto args = std::array { "-std=c++11" };
        const auto invocation = parse("run", args);
        CHECK(!invocation.has_value());
    }

    SUBCASE("legacy output dir is rejected") {
        const auto args = std::array { "--output-dir=build/cache" };
        const auto invocation = parse("run", args);
        CHECK(!invocation.has_value());
    }

    SUBCASE("legacy emit flag is rejected") {
        const auto args = std::array { "-E" };
        const auto invocation = parse("run", args);
        CHECK(!invocation.has_value());
    }

    SUBCASE("missing output path") {
        const auto args = std::array { "-o" };
        const auto invocation = parse("transpile", args);
        CHECK(!invocation.has_value());
    }

    SUBCASE("transpile output mode rejects multiple inputs") {
        const auto args = std::array { "-o", "out.cpp", "a.cv", "b.cv" };
        const auto invocation = parse("transpile", args);
        CHECK(!invocation.has_value());
    }

    SUBCASE("dump requires input") {
        const auto args = std::array<const char*, 0>{};
        const auto invocation = parse("dump", args);
        CHECK(!invocation.has_value());
    }
}
