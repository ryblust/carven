module carven:driver.run.impl;

import :artifacts.materialize;
import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :driver.analysis;
import :driver.process;
import :driver.run;
import :driver.sources;
import :driver.timings;
import :semantic.evaluation.output;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
import :support.path;
import :support.timing;
import std;

namespace {

auto fail(std::string_view message) noexcept -> int {
    std::println(std::cerr, "carven: error: {}", message);
    return 1;
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

} // namespace

auto run_native_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int {
    auto show_timings = false;
    auto tests = false;
    auto input_paths = std::vector<std::string_view> {};
    auto separator = 0uz;
    for (; separator < args.size() && std::string_view(args[separator]) != "--"; ++separator) {
        const auto argument = std::string_view(args[separator]);
        if (argument == "--tests") {
            if (tests) {
                return fail("--tests may be specified only once");
            }
            tests = true;
            continue;
        }
        if (argument == "--timings") {
            show_timings = true;
            continue;
        }
        if (argument.starts_with('-')) {
            return fail(
                std::format("unknown option '{}'\nRun 'carven --help' for usage.", argument)
            );
        }
        input_paths.push_back(argument);
    }
    if (input_paths.empty()) {
        return fail(
            "running a program requires at least one source file\nRun 'carven --help' for usage."
        );
    }
    auto timings = CommandTimings(show_timings, "run");
    const auto sources = collect_command_sources(executable, input_paths, timings.recorder());
    if (!sources) {
        return fail(sources.error());
    }
    auto semantic = load_and_analyze_sources(
        sources->carven,
        [](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        },
        timings.recorder()
    );
    if (!semantic) {
        return 1;
    }
    const auto has_entry = std::ranges::any_of(
        semantic->declarations().functions(),
        [](const auto& record) static noexcept { return record.value.entry_point.has_value(); }
    );
    if (tests
        && !std::ranges::any_of(
            semantic->tests().entries(),
            [](const auto& record) static noexcept { return !record.value.is_const; }
        )) {
        return fail("running tests requires at least one runtime test");
    }
    if (!tests && !has_entry) {
        return fail("running a program requires an entry point");
    }
    auto generation = TimingScope(timings.recorder(), TimingStage::CppGeneration);
    const auto artifacts = generate_artifacts(
        std::move(*semantic),
        TargetPlanningRequest {
            .test_mode = tests ? TestGenerationMode::RunnerEntryPoint : TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("carven.run"),
        }
    );
    generation.stop();
    auto writing = TimingScope(timings.recorder(), TimingStage::ArtifactWriting);
    const auto directory = create_run_directory();
    if (!directory) {
        return fail(directory.error());
    }
    const auto cleanup = TemporaryDirectory(*directory);
    const auto output_directory = path_to_generic_utf8(*directory);
    if (const auto written = write_artifacts(output_directory, artifacts); !written) {
        return fail(written.error());
    }
    writing.stop();
    auto compilation = TimingScope(timings.recorder(), TimingStage::NativeCompilation);
#ifdef _WIN32
    const auto binary = path_to_generic_utf8(*directory / "program.exe");
#else
    const auto binary = path_to_generic_utf8(*directory / "program");
#endif
    const auto configured_cxx = std::getenv("CXX");
    const auto compiler = std::string(
        configured_cxx != nullptr && *configured_cxx != '\0' ? configured_cxx : "clang++"
    );
    auto compiler_name = path_to_generic_utf8(path_from_utf8(compiler).filename());
    std::ranges::transform(
        compiler_name,
        compiler_name.begin(),
        [](unsigned char value) static noexcept { return static_cast<char>(std::tolower(value)); }
    );
    const auto msvc = compiler_name == "cl"
        || compiler_name == "cl.exe"
        || compiler_name == "clang-cl"
        || compiler_name == "clang-cl.exe";
    auto native_sources = std::vector<std::string>();
    for (const auto& artifact : artifacts.entries()) {
        if (artifact.role == GeneratedArtifactRole::ModuleImplementation
            || artifact.role == GeneratedArtifactRole::TestEntry) {
            native_sources.push_back(
                path_to_generic_utf8(*directory / path_from_utf8(artifact.logical_path))
            );
        }
    }
    native_sources.append_range(sources->native);
    const auto includes = std::array {
        output_directory,
        path_to_generic_utf8(sources->crafts),
        std::string("crafts"),
        std::string(".")
    };
    auto native_args = std::vector<std::string> {compiler};
    std::cout.flush();
    std::cerr.flush();
    if (msvc) {
        // Separate object paths prevent equal source basenames in different modules colliding.
        native_args.push_back("/nologo");
        for (auto index = 0uz; index < native_sources.size(); ++index) {
            const auto object =
                path_to_generic_utf8(*directory / std::format("source-{}.obj", index));
            auto compile_args = std::vector<std::string> {
                compiler,
                "/nologo",
                "/std:c++20",
                "/EHsc",
                "/utf-8",
                "/c",
                native_sources[index],
                "/Fo" + object,
                "/Fd" + path_to_generic_utf8(*directory / "compiler.pdb")
            };
            for (const auto& include : includes) {
                compile_args.push_back("/I" + include);
            }
            const auto compiled = run_process(std::move(compile_args));
            if (!compiled) {
                return fail(compiled.error());
            }
            if (*compiled != 0) {
                return *compiled;
            }
            native_args.push_back(object);
        }
        native_args.push_back("/Fe" + binary);
    } else {
        native_args.push_back("-std=c++20");
        for (const auto& include : includes) {
            native_args.push_back("-I" + include);
        }
        native_args.append_range(native_sources);
        native_args.insert(native_args.end(), {"-o", binary});
    }
    const auto compiled = run_process(std::move(native_args));
    if (!compiled) {
        return fail(compiled.error());
    }
    if (*compiled != 0) {
        return *compiled;
    }
    compilation.stop();
    auto program_args = std::vector<std::string> {binary};
    if (separator < args.size()) {
        for (const auto argument : args.subspan(separator + 1)) {
            program_args.emplace_back(argument);
        }
    }
    auto execution = TimingScope(timings.recorder(), TimingStage::Execution);
    const auto executed = run_process(std::move(program_args));
    execution.stop();
    if (executed) {
        timings.set_outcome(std::format("exited with code {}", *executed));
    }
    return executed ? *executed : fail(executed.error());
}
