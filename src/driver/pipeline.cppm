export module zero.driver.pipeline;

import zero.common.filesystem;
import zero.common.process;
import zero.common.source;
import zero.driver.toolchain;
import zero.frontend.lexer;
import zero.frontend.ast;
import zero.frontend.parser;
import zero.backend.codegen;
import std;

export struct TranspileResult final {
    std::string output;
    std::vector<ParseError> errors;
};

export struct Driver final {
    std::uint8_t language_standard = 23;
    bool import_std = false;
    bool emit_only = false;
    bool only_tokens = false;
    bool only_ast = false;
    std::filesystem::path output_dir = ".zero/build";
#if defined(_WIN32)
    std::string compiler = "clang-cl";
#else
    std::string compiler = "c++";
#endif
    std::vector<std::string_view> input_files;

};

export constexpr auto parse_flags(std::span<const char* const> flags) noexcept -> std::optional<Driver> {
    auto driver = Driver{};
    for (const std::string_view flag : flags) {
        if (flag.starts_with("-std=")) {
            const auto standard = flag.substr(5);
            if      (standard == "c++14") driver.language_standard = 14;
            else if (standard == "c++17") driver.language_standard = 17;
            else if (standard == "c++20") driver.language_standard = 20;
            else if (standard == "c++23") driver.language_standard = 23;
            else if (standard == "c++26") driver.language_standard = 26;
            else {
                std::println("zero: error: unknown standard '{}'", standard);
                return std::nullopt;
            }
        } else if (flag.starts_with("--output-dir=")) {
            driver.output_dir = flag.substr(13);
        } else if (flag == "--import-std") {
            driver.import_std = true;
        } else if (flag == "-E") {
            driver.emit_only = true;
        } else if (flag == "--only-tokens") {
            driver.only_tokens = true;
        } else if (flag == "--only-ast") {
            driver.only_ast = true;
        } else if (flag.starts_with('-')) {
            std::println("zero: error: unknown flag '{}'", flag);
            return std::nullopt;
        } else {
            driver.input_files.push_back(flag);
        }
    }

    return driver;
}

export constexpr auto transpile(const Driver& driver, SourceFile file) noexcept -> TranspileResult {
    auto parse_result = parse(tokenize(file.content), file.content);

    if (!parse_result.errors.empty()) {
        return {
            .output = {},
            .errors = std::move(parse_result.errors)
        };
    }

    auto emit_std_module = driver.import_std;
    if (!emit_std_module) {
        for (const auto& item : parse_result.items) {
            if (const auto import_module = std::get_if<ImportItem>(&item)) {
                if (import_module->is_std_module) {
                    emit_std_module = true;
                    break;
                }
            }
        }
    }

    return {
        .output = generate(parse_result.items, file.content, driver.language_standard, emit_std_module),
        .errors = {}
    };
}

export auto run_single_file(const Driver& driver, SourceFile file) noexcept -> int {
    const auto transpile_result = transpile(driver, file);

    if (!transpile_result.errors.empty()) {
        std::println("{}", transpile_result.errors);
        return 1;
    }

    if (driver.emit_only) {
        std::print("{}", transpile_result.output);
        return 0;
    }

    if (!ensure_directory(driver.output_dir)) {
        std::println("zero run: error: cannot create output directory '{}'", driver.output_dir.string());
        return 1;
    }

    const auto artifacts = build_artifacts(file.filename, driver.output_dir);

    if (!write_file_if_changed(artifacts.cpp_path, transpile_result.output)) {
        std::println("zero run: error: cannot write '{}'", artifacts.cpp_path.string());
        return 1;
    }

    if (needs_compile(artifacts.cpp_path, artifacts.exe_path)) {
        const auto selected_compiler = compiler_from_environment(driver.compiler);
        const auto is_clang_cl = selected_compiler.contains("clang-cl");
        auto compile_args = std::vector<std::string> {
            selected_compiler,
        };
        if (is_clang_cl) {
            compile_args.emplace_back("-Xclang");
            compile_args.emplace_back(std::format("-std=c++{}", driver.language_standard));
        } else {
            compile_args.emplace_back(std::format("-std=c++{}", driver.language_standard));
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

    const auto run_args = std::vector<std::string> { artifacts.exe_path.string() };
    const auto run_result = run_process(run_args);
    if (!run_result.started) {
        std::println("zero run: error: cannot run '{}'", artifacts.exe_path.string());
        return 1;
    }

    return run_result.exit_code;
}
