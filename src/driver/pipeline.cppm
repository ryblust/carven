export module zero.driver.pipeline;

import zero.common.io;
import zero.common.process;
import zero.common.source;
import zero.frontend.lexer;
import zero.frontend.parser;
import zero.backend.codegen;

import std;

export struct TranspileResult final {
    std::string output;
    std::vector<ParseError> errors;

    constexpr auto has_errors() const noexcept -> bool {
        return !errors.empty();
    }
};

export struct Driver final {
    CppStandard standard = CppStandard::Cpp23;
    bool default_include_std = true;
    std::filesystem::path output_dir = ".zero/build";
    std::string compiler = "c++";

    auto transpile(SourceFile file) const noexcept -> TranspileResult;
    auto run_single_file(SourceFile file) const noexcept -> int;
};

struct BuildArtifacts final {
    std::filesystem::path cpp_path;
    std::filesystem::path exe_path;
};

auto source_stem(std::string_view filename) noexcept -> std::string {
    auto path = std::filesystem::path(filename);
    auto stem = path.stem().string();

    return stem.empty() ? "main" : stem;
}

constexpr auto fnv1a_64(std::string_view value) noexcept -> std::uint64_t {
    auto hash = 14695981039346656037ull;

    for (const auto c : value) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }

    return hash;
}

auto short_hash(std::string_view value) noexcept -> std::string {
    return std::format("{:08x}", static_cast<std::uint32_t>(fnv1a_64(value)));
}

auto stable_source_path(std::string_view filename) noexcept -> std::filesystem::path {
    auto error = std::error_code {};
    auto path = std::filesystem::weakly_canonical(std::filesystem::path(filename), error);

    if (!error) return path;

    path = std::filesystem::absolute(std::filesystem::path(filename), error);

    if (!error) return path;

    return std::filesystem::path(filename);
}

constexpr auto executable_extension() noexcept -> std::string_view {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

auto build_artifacts(std::string_view filename, const std::filesystem::path& output_dir) noexcept -> BuildArtifacts {
    const auto source_path = stable_source_path(filename);
    const auto base_name = std::format("{}_{}",
        source_stem(filename),
        short_hash(source_path.string())
    );

    return {
        .cpp_path = output_dir / std::format("{}.cpp", base_name),
        .exe_path = output_dir / std::format("{}{}", base_name, executable_extension())
    };
}

auto needs_compile(const std::filesystem::path& cpp_path, const std::filesystem::path& exe_path) noexcept -> bool {
    auto error = std::error_code {};

    if (!std::filesystem::exists(exe_path, error)) {
        return true;
    }

    const auto cpp_time = std::filesystem::last_write_time(cpp_path, error);
    if (error) {
        return true;
    }

    const auto exe_time = std::filesystem::last_write_time(exe_path, error);
    if (error) {
        return true;
    }

    return cpp_time > exe_time;
}

auto compiler_from_environment(std::string_view fallback) noexcept -> std::string {
    const auto* value = std::getenv("CXX");

    if (value != nullptr && std::string_view(value).size() > 0) {
        return value;
    }

    return std::string(fallback);
}

constexpr auto display_command(std::span<const std::string> args) noexcept -> std::string {
    auto result = std::string();

    for (auto i = 0uz; i < args.size(); ++i) {
        if (i > 0) result.push_back(' ');

        const auto needs_quotes = args[i].contains(' ') || args[i].contains('\t');
        if (needs_quotes) result.push_back('"');
        result.append(args[i]);
        if (needs_quotes) result.push_back('"');
    }

    return result;
}

auto Driver::transpile(SourceFile file) const noexcept -> TranspileResult {
    auto parse_result = parse(tokenize(file.content), file.content);

    if (parse_result.has_errors()) {
        return {
            .output = {},
            .errors = std::move(parse_result.errors)
        };
    }

    return {
        .output = generate(parse_result.items, file.content, standard, default_include_std),
        .errors = {}
    };
}

auto Driver::run_single_file(SourceFile file) const noexcept -> int {
    const auto transpile_result = transpile(file);

    if (transpile_result.has_errors()) {
        for (const auto& error : transpile_result.errors) {
            std::println("{}", format_parse_error(error));
        }
        return 1;
    }

    if (!ensure_directory(output_dir)) {
        std::println("zero run: error: cannot create output directory '{}'", output_dir.string());
        return 1;
    }

    const auto artifacts = build_artifacts(file.filename, output_dir);

    if (!write_text_file_if_changed(artifacts.cpp_path, transpile_result.output)) {
        std::println("zero run: error: cannot write '{}'", artifacts.cpp_path.string());
        return 1;
    }

    if (needs_compile(artifacts.cpp_path, artifacts.exe_path)) {
        const auto selected_compiler = compiler_from_environment(compiler);
        const auto compile_args = std::vector<std::string> {
            selected_compiler,
            std::format("-std={}", display_cpp_standard(standard)),
            artifacts.cpp_path.string(),
            "-o",
            artifacts.exe_path.string()
        };

        const auto compile_result = run_process(compile_args);
        if (!compile_result.started) {
            std::println("zero run: error: cannot start C++ compiler '{}'", selected_compiler);
            return 1;
        }

        if (compile_result.exit_code != 0) {
            std::println("zero run: error: C++ compile failed");
            std::println("command: {}", display_command(compile_args));
            return compile_result.exit_code;
        }
    }

    const auto run_args = std::vector<std::string> { artifacts.exe_path.string() };
    const auto run_result = run_process(run_args);
    if (!run_result.started) {
        std::println("zero run: error: cannot run '{}'", artifacts.exe_path.string());
        return 1;
    }

    return run_result.exit_code;
}
