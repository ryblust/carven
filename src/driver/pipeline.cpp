module zero.driver.pipeline;

import zero.common.filesystem;
import zero.common.process;
import zero.common.source;
import zero.driver.toolchain;
import zero.frontend.lexer;
import zero.frontend.parser.ast;
import zero.frontend.parser;
import zero.backend.codegen;
import std;

auto TranspileResult::has_errors() const noexcept -> bool {
    return !errors.empty();
}

auto Driver::parse_flags(std::span<const char* const> flags) noexcept -> bool {
    for (const std::string_view flag : flags) {
        if (flag.starts_with("-std=")) {
            const auto standard = flag.substr(5);
            if      (standard == "c++14") language_standard = 14;
            else if (standard == "c++17") language_standard = 17;
            else if (standard == "c++20") language_standard = 20;
            else if (standard == "c++23") language_standard = 23;
            else if (standard == "c++26") language_standard = 26;
            else {
                std::println("zero: error: unknown standard '{}'", standard);
                return false;
            }
        } else if (flag.starts_with("--output-dir=")) {
            output_dir = flag.substr(13);
        } else if (flag == "--no-default-include-std") {
            default_include_std = false;
        } else if (flag == "--only-tokens") {
            only_tokens = true;
        } else if (flag == "--only-ast") {
            only_ast = true;
        } else {
            input_files.push_back(flag);
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
