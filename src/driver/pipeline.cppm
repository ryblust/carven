export module zero.driver.pipeline;

import zero.common.file;
import zero.common.process;
import zero.common.source;
import zero.driver.toolchain;
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
    std::uint8_t language_standard = 23;
    bool default_include_std = true;
    std::filesystem::path output_dir = ".zero/build";
#if defined(_WIN32)
    std::string compiler = "clang-cl";
#else
    std::string compiler = "c++";
#endif
    std::vector<std::string_view> input_files;

    auto parse_flags(int argc, char** argv) noexcept -> bool;
    auto has_input_files() const noexcept -> bool { return !input_files.empty(); }
    auto transpile(SourceFile file) const noexcept -> TranspileResult;
    auto run_single_file(SourceFile file) const noexcept -> int;
};

auto Driver::parse_flags(int argc, char** argv) noexcept -> bool {
    for (auto i = 2; i < argc; ++i) {
        const auto arg = std::string_view(argv[i]);

        if (arg.starts_with("-std=")) {
            const auto val = arg.substr(5);
            if      (val == "c++14") language_standard = 14;
            else if (val == "c++17") language_standard = 17;
            else if (val == "c++20") language_standard = 20;
            else if (val == "c++23") language_standard = 23;
            else if (val == "c++26") language_standard = 26;
            else {
                std::println("zero: error: unknown standard '{}'", val);
                return false;
            }
        } else if (arg.starts_with("--output-dir=")) {
            output_dir = arg.substr(13);
        } else if (arg == "--no-default-include-std") {
            default_include_std = false;
        } else {
            input_files.push_back(arg);
        }
    }

    return true;
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
        .output = generate(parse_result.items, file.content, language_standard, default_include_std),
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

    if (!write_file_if_changed(artifacts.cpp_path, transpile_result.output)) {
        std::println("zero run: error: cannot write '{}'", artifacts.cpp_path.string());
        return 1;
    }

    if (needs_compile(artifacts.cpp_path, artifacts.exe_path)) {
        const auto selected_compiler = compiler_from_environment(compiler);
        const auto is_clang_cl = selected_compiler.contains("clang-cl");
        auto compile_args = std::vector<std::string> {
            selected_compiler,
        };

        if (is_clang_cl) {
            compile_args.emplace_back("-Xclang");
            compile_args.emplace_back(std::format("-std=c++{}", language_standard));
        } else {
            compile_args.emplace_back(std::format("-std=c++{}", language_standard));
        }

        compile_args.emplace_back(artifacts.cpp_path.string());
        compile_args.emplace_back("-o");
        compile_args.emplace_back(artifacts.exe_path.string());

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

    const auto run_args = std::vector<std::string>{ artifacts.exe_path.string() };
    const auto run_result = run_process(run_args);
    if (!run_result.started) {
        std::println("zero run: error: cannot run '{}'", artifacts.exe_path.string());
        return 1;
    }

    return run_result.exit_code;
}
