export module carven.driver.pipeline;

import carven.common.filesystem;
import carven.common.process;
import carven.common.source;
import carven.driver.report;
import carven.driver.toolchain;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import carven.frontend.sema;
import carven.backend.codegen;
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
    std::string_view output_dir = ".carven/build";
#if defined(_WIN32)
    std::string compiler = "clang-cl";
#else
    std::string compiler = "c++";
#endif
    std::vector<std::string_view> input_files;
};

export constexpr auto parse_flags(std::span<const char* const> flags) noexcept -> std::optional<Driver> {
    auto driver = Driver{};
    auto double_dash = false;

    for (const std::string_view flag : flags) {
        if (double_dash) {
            driver.input_files.push_back(flag);
        } else if (flag == "--") {
            double_dash = true;
        } else if (flag.starts_with("-std=")) {
            const auto standard = flag.substr(5);
            if      (standard == "c++14") driver.language_standard = 14;
            else if (standard == "c++17") driver.language_standard = 17;
            else if (standard == "c++20") driver.language_standard = 20;
            else if (standard == "c++23") driver.language_standard = 23;
            else if (standard == "c++26") driver.language_standard = 26;
            else {
                std::println("carven: error: unknown standard '{}'", standard);
                return std::nullopt;
            }
        } else if (flag.starts_with("--output-dir=")) {
            driver.output_dir = flag.substr(13);
        } else if (flag.starts_with("--compiler=")) {
            driver.compiler = flag.substr(11);
        } else if (flag == "--import-std") {
            driver.import_std = true;
        } else if (flag == "-E") {
            driver.emit_only = true;
        } else if (flag == "--only-tokens") {
            driver.only_tokens = true;
        } else if (flag == "--only-ast") {
            driver.only_ast = true;
        } else if (flag.starts_with('-')) {
            std::println("carven: error: unknown flag '{}'", flag);
            return std::nullopt;
        } else {
            driver.input_files.push_back(flag);
        }
    }

    return driver;
}

export constexpr auto transpile(std::string_view source, std::uint8_t language_standard, bool import_std) noexcept -> TranspileResult {
    const auto parse_result = parse(tokenize(source), source);

    if (!parse_result.errors.empty()) {
        return {
            .output = {},
            .errors = std::move(parse_result.errors)
        };
    }

    auto sema_errors = analyze(parse_result.items, source);
    if (!sema_errors.empty()) {
        return {
            .output = {},
            .errors = std::move(sema_errors)
        };
    }

    return {
        .output = generate(parse_result.items, source, language_standard, import_std || has_std_module(parse_result.items)),
        .errors = {}
    };
}

export auto run_single_file(const Driver& driver, std::string_view source, std::string_view filepath) noexcept -> int {
    const auto transpile_result = transpile(source, driver.language_standard, driver.import_std);

    if (!transpile_result.errors.empty()) {
        report_errors(transpile_result.errors, source, filepath);
        return 1;
    }

    if (driver.emit_only) {
        std::print("{}", transpile_result.output);
        return 0;
    }

    if (!ensure_directory(driver.output_dir)) {
        std::println("carven run: error: cannot create output directory '{}'", driver.output_dir);
        return 1;
    }

    const auto artifacts = build_artifacts(filepath, driver.output_dir);

    if (!write_file_if_changed(artifacts.cpp_path, transpile_result.output)) {
        std::println("carven run: error: cannot write '{}'", artifacts.cpp_path);
        return 1;
    }

    if (needs_compile(artifacts.cpp_path, artifacts.exe_path)) {
        const auto compile_args = build_compile_args(driver.compiler, artifacts.cpp_path, artifacts.exe_path, driver.language_standard);

        const auto compile_result = run_process(compile_args);
        if (compile_result < 0) {
            std::println("carven run: error: cannot start C++ compiler '{}'", driver.compiler);
            return 1;
        }

        if (compile_result != 0) {
            auto command = std::string();
            for (auto i = 0uz; i < compile_args.size(); ++i) {
                if (i > 0) command += ' ';
                command += compile_args[i];
            }
            std::println("carven run: error: C++ compile failed\ncommand: {}", command);
            return compile_result;
        }
    }

    const auto run_args = std::vector { artifacts.exe_path };
    const auto run_result = run_process(run_args);
    if (run_result < 0) {
        std::println("carven run: error: cannot run '{}'", artifacts.exe_path);
        return 1;
    }

    return run_result;
}
