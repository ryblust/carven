export module carven.driver.command.transpile;

import carven.common.filesystem;
import carven.common.source;
import carven.frontend.lexer;
import carven.frontend.parser;
import carven.frontend.sema;
import carven.backend.codegen;
import carven.driver.diagnostics;
import std;

export struct TranspileCommand final {
    static constexpr auto name = "transpile";
    static constexpr auto description = "Transpile .cv files to C++";
    static constexpr auto help_message =
        R"(carven transpile - Transpile .cv files to C++

USAGE:
    carven transpile [options...] <source-file>...

OPTIONS:
    -std=c++<value>    Target C++ standard for generated output
    -o <path>          Write generated C++ to file instead of stdout
    --import-std       Force #include std headers (auto if source has import std)
)";

    CodegenOptions options;
    std::optional<std::string_view> output_file;
    std::vector<std::string_view> source_files;
};

export auto execute(const TranspileCommand& command) noexcept -> int;

module :private;

namespace {

auto transpile(const TranspileCommand& command) noexcept -> int {
    auto exit_code = 0;

    for (auto i = 0uz; i < command.source_files.size(); ++i) {
        const auto input = command.source_files[i];
        const auto source = SourceFile::from_file(input);

        if (!source) {
            std::println("carven transpile: error: cannot read '{}'", input);
            exit_code = 1;
            continue;
        }

        auto parse_result = parse(tokenize(source->text()), source->text());

        if (!parse_result.errors.empty()) {
            report_errors(parse_result.errors, source->text(), source->filepath());
            exit_code = 1;
            continue;
        }

        auto sema_errors = analyze(parse_result.items, source->text());
        if (!sema_errors.empty()) {
            report_errors(sema_errors, source->text(), source->filepath());
            exit_code = 1;
            continue;
        }

        const auto output = generate(parse_result.items, source->text(), command.options);

        if (command.output_file) {
            const auto path = std::filesystem::path(*command.output_file);

            if (path.has_parent_path()) {
                auto error = std::error_code();
                std::filesystem::create_directories(path.parent_path(), error);

                if (error) {
                    std::println("carven transpile: error: cannot create '{}'", path.parent_path().generic_string());
                    return 1;
                }
            }

            if (!write_file(path, output)) {
                std::println("carven transpile: error: cannot write '{}'", path.generic_string());
                return 1;
            }
        } else {
            if (i > 0) {
                std::print("\n");
            }
            std::print("{}", output);
        }
    }

    return exit_code;
}

}

auto execute(const TranspileCommand& command) noexcept -> int {
    return transpile(command);
}
