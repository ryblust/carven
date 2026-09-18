module carven:driver.run.impl;

import :artifacts.materialize;
import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :driver.analysis;
import :driver.input_path;
import :driver.process;
import :driver.run;
import :semantic.evaluation.output;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
import :support.path;
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

auto find_crafts_directory(std::string_view executable) noexcept -> std::filesystem::path {
    const auto program = current_executable_path(executable);
    if (program.empty()) {
        return {};
    }
    auto error = std::error_code();
    auto installed = program.parent_path().parent_path() / "crafts";
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

struct RunSources final {
    std::vector<std::string> carven;
    std::vector<std::string> native;
};

// Application inputs remain explicit. Only the two fixed Crafts roots are
// collected; imports, manifests, and parent directories are not searched.
auto collect_run_sources(
    std::span<const std::string_view> inputs,
    const std::filesystem::path& crafts
) noexcept -> std::expected<RunSources, std::string> {
    auto result = RunSources {};
    auto seen = std::set<std::filesystem::path>();
    const auto add =
        [&](const std::filesystem::path& path) noexcept -> std::expected<void, std::string> {
        const auto spelling = path_to_generic_utf8(path);
        if (path.extension() == ".cv") {
            const auto module_path = derive_input_module_path(spelling);
            if (!module_path) {
                return std::unexpected(module_path.error());
            }
        }
        auto error = std::error_code();
        const auto identity = std::filesystem::canonical(path, error);
        if (error) {
            return std::unexpected(
                std::format("cannot read source file '{}': {}", spelling, error.message())
            );
        }
        if (seen.insert(identity).second) {
            (path.extension() == ".cv" ? result.carven : result.native).push_back(spelling);
        }
        return {};
    };
    for (const auto input : inputs) {
        // Validate explicit inputs even if they duplicate a discovered file.
        const auto module_path = derive_input_module_path(input);
        if (!module_path) {
            return std::unexpected(module_path.error());
        }
        if (const auto added = add(path_from_utf8(input)); !added) {
            return std::unexpected(added.error());
        }
    }
    const auto roots = std::array {crafts / "carven", std::filesystem::path("crafts")};
    for (const auto& root : roots) {
        auto error = std::error_code();
        const auto exists = std::filesystem::exists(root, error);
        if (!error && !exists && root == std::filesystem::path("crafts")) {
            continue;
        }
        auto iterator = std::filesystem::recursive_directory_iterator(root, error);
        const auto end = std::filesystem::recursive_directory_iterator();
        auto files = std::vector<std::filesystem::path>();
        while (!error && iterator != end) {
            const auto path = iterator->path();
            if ((path.extension() == ".cv" || path.extension() == ".cpp")
                && iterator->is_regular_file(error)) {
                files.push_back(path);
            }
            if (!error) {
                iterator.increment(error);
            }
        }
        if (error) {
            return std::unexpected(
                std::format(
                    "cannot scan Crafts directory '{}': {}",
                    path_to_generic_utf8(root),
                    error.message()
                )
            );
        }
        std::ranges::sort(files);
        for (const auto& path : files) {
            if (const auto added = add(path); !added) {
                return std::unexpected(added.error());
            }
        }
    }
    std::ranges::sort(result.carven);
    std::ranges::sort(result.native);
    return result;
}

} // namespace

auto run_native_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int {
    auto input_paths = std::vector<std::string_view> {};
    auto separator = 0uz;
    for (; separator < args.size() && std::string_view(args[separator]) != "--"; ++separator) {
        const auto argument = std::string_view(args[separator]);
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
    const auto crafts = find_crafts_directory(executable);
    if (crafts.empty()) {
        return fail("cannot locate Crafts in the Carven installation or source checkout");
    }
    const auto sources = collect_run_sources(input_paths, crafts);
    if (!sources) {
        return fail(sources.error());
    }
    // Own all path strings before forming the views consumed by analysis.
    input_paths.clear();
    for (const auto& input : sources->carven) {
        input_paths.push_back(input);
    }
    auto semantic = load_and_analyze_sources(
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
    const auto directory = create_run_directory();
    if (!directory) {
        return fail(directory.error());
    }
    const auto cleanup = TemporaryDirectory(*directory);
    const auto output_directory = path_to_generic_utf8(*directory);
    if (const auto written = write_artifacts(output_directory, artifacts); !written) {
        return fail(written.error());
    }
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
        if (artifact.role == GeneratedArtifactRole::ModuleImplementation) {
            native_sources.push_back(
                path_to_generic_utf8(*directory / path_from_utf8(artifact.logical_path))
            );
        }
    }
    native_sources.append_range(sources->native);
    const auto includes = std::array {
        output_directory,
        path_to_generic_utf8(crafts),
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
    auto program_args = std::vector<std::string> {binary};
    if (separator < args.size()) {
        for (const auto argument : args.subspan(separator + 1)) {
            program_args.emplace_back(argument);
        }
    }
    const auto executed = run_process(std::move(program_args));
    return executed ? *executed : fail(executed.error());
}
