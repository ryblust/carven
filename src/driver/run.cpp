module;

#ifndef _WIN32
#include <cerrno>
#include <cstdlib>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

module carven:driver.run.impl;

import :artifacts.materialize;
import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :driver.analysis;
import :driver.run;
import :semantic.evaluation.output;
import :semantic.semir;
import std;

namespace {

auto fail(std::string_view message) noexcept -> int {
    std::println(std::cerr, "carven: error: {}", message);
    return 1;
}

#ifndef _WIN32

// All child arguments are passed directly, without shell interpretation.
auto run_process(std::vector<std::string> arguments) noexcept -> int {
    auto argv = std::vector<char*> {};
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    auto child = pid_t();
    const auto error = posix_spawnp(&child, argv.front(), nullptr, nullptr, argv.data(), environ);
    if (error != 0) {
        return fail(
            std::format(
                "cannot start '{}': {}",
                arguments.front(),
                std::error_code(error, std::generic_category()).message()
            )
        );
    }
    auto status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            return fail("cannot wait for child process");
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}

class TemporaryDirectory final {
public:
    explicit TemporaryDirectory(std::filesystem::path root) noexcept;
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;
    ~TemporaryDirectory();

private:
    std::filesystem::path root_;
};

TemporaryDirectory::TemporaryDirectory(std::filesystem::path root) noexcept
    : root_(std::move(root)) {}

TemporaryDirectory::~TemporaryDirectory() {
    auto error = std::error_code();
    std::filesystem::remove_all(root_, error);
    if (error) {
        std::println(
            std::cerr,
            "carven: warning: cannot remove '{}': {}",
            root_.string(),
            error.message()
        );
    }
}

auto find_crafts_directory(std::string_view executable) noexcept -> std::filesystem::path {
    auto program = std::filesystem::path(executable);
    auto error = std::error_code();
    if (!program.has_parent_path()) {
        const auto search_path = std::getenv("PATH");
        if (search_path != nullptr) {
            for (const auto part : std::string_view(search_path) | std::views::split(':')) {
                const auto candidate = std::filesystem::path(std::string_view(part)) / program;
                if (std::filesystem::is_regular_file(candidate, error)
                    && access(candidate.c_str(), X_OK) == 0) {
                    program = candidate;
                    break;
                }
            }
        }
    }
    program = std::filesystem::canonical(program, error);
    if (error) {
        return {};
    }
    const auto installed = program.parent_path().parent_path() / "crafts";
    if (std::filesystem::is_regular_file(installed / "carven/runtime/runtime.hpp", error)) {
        return installed;
    }
    // Development binaries must belong to a Carven source checkout.
    for (auto root = program.parent_path(); root != root.parent_path(); root = root.parent_path()) {
        if (std::filesystem::is_regular_file(root / "src/carven.cppm", error)
            && std::filesystem::is_regular_file(root / "xmake.lua", error)
            && std::filesystem::is_regular_file(
                root / "crafts/carven/runtime/runtime.hpp",
                error
            )) {
            return root / "crafts";
        }
    }
    return {};
}

#endif

} // namespace

auto run_native_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int {
    auto input_paths = std::vector<std::string_view> {};
    auto separator = 0uz;
    for (; separator < args.size() && std::string_view(args[separator]) != "--"; ++separator) {
        const auto argument = std::string_view(args[separator]);
        if (argument.starts_with('-')) {
            return fail(std::format("unknown option '{}'", argument));
        }
        input_paths.push_back(argument);
    }
    if (input_paths.empty()) {
        return fail(
            "usage: carven <source-file>... [-- <arguments>...]; source files are required"
        );
    }
#ifdef _WIN32
    (void)executable;
    return fail("native execution currently supports POSIX hosts only");
#else
    auto semantic = analyze_sources(
        input_paths,
        [](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        }
    );
    if (!semantic) {
        return 1;
    }
    const auto has_entry = std::ranges::any_of(
        semantic->declarations().functions(),
        [](const auto& record) static noexcept { return record.value.entry_point.has_value(); }
    );
    if (!has_entry) {
        return fail("running a program requires an entry point");
    }
    const auto artifacts = generate_artifacts(
        std::move(*semantic),
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("carven.run"),
        }
    );
    const auto crafts = find_crafts_directory(executable);
    if (crafts.empty()) {
        return fail("cannot locate Crafts in the Carven installation or source checkout");
    }
    auto error = std::error_code();
    const auto temporary_root = std::filesystem::temp_directory_path(error);
    if (error) {
        return fail(std::format("cannot locate temporary directory: {}", error.message()));
    }
    auto output_directory = (temporary_root / "carven-run-XXXXXX").string();
    if (mkdtemp(output_directory.data()) == nullptr) {
        return fail("cannot create temporary run directory");
    }
    const auto cleanup = TemporaryDirectory(output_directory);
    if (const auto written = write_artifacts(output_directory, artifacts); !written) {
        return fail(written.error());
    }
    const auto binary = (std::filesystem::path(output_directory) / "program").string();
    const auto configured_cxx = std::getenv("CXX");
    auto native_args = std::vector<std::string> {
        configured_cxx != nullptr && *configured_cxx != '\0' ? configured_cxx : "clang++",
        "-std=c++20",
        "-I" + output_directory,
        "-I" + crafts.string(),
        "-I.",
    };
    for (const auto& artifact : artifacts.entries()) {
        if (artifact.role == GeneratedArtifactRole::ModuleImplementation) {
            native_args.push_back(
                (std::filesystem::path(output_directory) / artifact.logical_path).string()
            );
        }
    }
    native_args.insert(native_args.end(), {"-o", binary});
    std::cout.flush();
    std::cerr.flush();
    if (const auto status = run_process(std::move(native_args)); status != 0) {
        return status;
    }
    auto program_args = std::vector<std::string> {binary};
    if (separator < args.size()) {
        for (const auto argument : args.subspan(separator + 1)) {
            program_args.emplace_back(argument);
        }
    }
    return run_process(std::move(program_args));
#endif
}
