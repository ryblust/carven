module;
#include <cstdio>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

module carven:formatter.main;

import :diagnostics.report;
import :formatter.batch;
import :formatter.files;
import :source.manager;
import :source.text;
import :support.path;
import std;

namespace {

enum class OutputMode {
    Stdout,
    Check,
    Write,
};

auto write_stdout(std::string_view text) noexcept -> int {
    // Preserve source bytes and report write failures without exceptions.
    const auto written = std::fwrite(text.data(), 1uz, text.size(), stdout);
    const auto flushed = std::fflush(stdout);
    if (written != text.size() || flushed != 0) {
        std::println(stderr, "carven-format: cannot write stdout");
        return 2;
    }
    return 0;
}

auto show_help(std::string_view topic) noexcept -> int {
    if (topic.empty()) {
        return write_stdout(
            "Usage:\n"
            "  carven-format [FILE | -]          Format one input to stdout; omitted input reads stdin.\n"
            "  carven-format check FILE|DIR ...  List files needing formatting without changing them.\n"
            "  carven-format write FILE|DIR ...  Format files in place.\n"
            "  carven-format help [COMMAND]     Show general or command-specific help.\n"
            "\n"
            "check and write accept multiple files and directories in one invocation.\n"
            "Use carven-format check . or carven-format write . for the current directory.\n"
            "There are no flags. Use ./help, ./check, or ./write for command-named files.\n"
            "Exit 0: success; 1: check found differences; 2: an error occurred.\n"
        );
    }
    if (topic == "check") {
        return write_stdout(
            "Usage: carven-format check FILE|DIR ...\n"
            "       carven-format check -\n"
            "\n"
            "Check all inputs without changing them. List each file needing formatting\n"
            "once on stdout, in sorted order, relative to the current directory.\n"
            "Use - alone to check stdin; a difference is reported as stdin.\n"
            "Directories recursively select .cv files, skipping hidden/build directories and symlink entries.\n"
            "Exit 0: no differences; 1: formatting differs; 2: an error occurred.\n"
            "Example: carven-format check src examples extra.cv\n"
        );
    }
    if (topic == "write") {
        return write_stdout(
            "Usage: carven-format write FILE|DIR ...\n"
            "\n"
            "Format all inputs in one invocation and replace changed files in place.\n"
            "Validate the whole batch before writing; unchanged files are not rewritten.\n"
            "Replacement is per-file: a later I/O error can leave earlier files updated.\n"
            "Directories recursively select .cv files, skipping hidden/build directories and symlink entries.\n"
            "Stdin cannot be written.\n"
            "Exit 0: success; 2: an error occurred.\n"
            "Example: carven-format write .\n"
        );
    }
    if (topic == "help") {
        return write_stdout(
            "Usage: carven-format help [COMMAND]\n"
            "Show general help, or help for check, write, or help.\n"
            "Help does not read source files.\n"
        );
    }
    std::println(stderr, "carven-format: unknown help topic '{}'; use 'carven-format help'", topic);
    return 2;
}

} // namespace

extern "C++" auto main(int argc, char** argv) noexcept -> int {
#if defined(_WIN32)
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::println(stderr, "carven-format: cannot enable binary standard streams");
        return 2;
    }
#endif
    auto mode = OutputMode::Stdout;
    auto first_input = 1;
    if (argc > 1) {
        const auto command = std::string_view(argv[1]);
        if (command == "help") {
            if (argc > 3) {
                std::println(
                    stderr,
                    "carven-format: help accepts at most one command\nUsage: carven-format help [COMMAND]"
                );
                return 2;
            }
            return show_help(argc == 3 ? std::string_view(argv[2]) : std::string_view());
        }
        if (command == "check" || command == "write") {
            mode = command == "check" ? OutputMode::Check : OutputMode::Write;
            first_input = 2;
            if (argc == first_input) {
                std::println(
                    stderr,
                    "carven-format: {} requires an input; use 'carven-format {} .' for the current directory\n"
                    "Usage: carven-format {} FILE|DIR ...",
                    command,
                    command,
                    command
                );
                return 2;
            }
        }
    }
    auto inputs = std::vector<std::string_view>();
    for (auto index = first_input; index < argc; ++index) {
        inputs.emplace_back(argv[index]);
    }
    const auto stdin_input = inputs.empty() || (inputs.size() == 1uz && inputs.front() == "-");
    if ((mode == OutputMode::Write && stdin_input)
        || (!stdin_input && std::ranges::find(inputs, "-") != inputs.end())) {
        std::println(stderr, "carven-format: stdin cannot be written or combined with file inputs");
        return 2;
    }
    auto sources = SourceManager();
    auto batch_inputs = std::vector<FormattingInput>();
    if (stdin_input) {
        auto text = std::string(std::istreambuf_iterator<char>(std::cin), {});
        if (std::cin.bad()) {
            std::println(stderr, "carven-format: cannot read stdin");
            return 2;
        }
        const auto id = sources.append_virtual("stdin", std::move(text));
        if (!id) {
            std::println(stderr, "carven-format: {}", id.error().message);
            return 2;
        }
        batch_inputs.push_back(FormattingInput {.path = {}, .source_id = *id});
    } else {
        const auto paths = collect_format_paths(inputs);
        if (!paths) {
            std::println(stderr, "carven-format: {}", paths.error());
            return 2;
        }
        if (mode == OutputMode::Stdout && paths->size() != 1uz) {
            std::println(
                stderr,
                "carven-format: stdout requires exactly one file; use check or write for multiple files"
            );
            return 2;
        }
        for (const auto& path : *paths) {
            const auto id = sources.append_file(path_to_generic_utf8(path));
            if (!id) {
                std::println(
                    stderr,
                    "carven-format: {}: {}",
                    id.error().origin,
                    id.error().message
                );
                return 2;
            }
            batch_inputs.push_back(FormattingInput {.path = path, .source_id = *id});
        }
    }
    const auto batch = format_batch(sources, batch_inputs);
    if (!batch) {
        std::print(stderr, "{}", render_diagnostics(batch.error(), sources));
        return 2;
    }
    if (mode == OutputMode::Check) {
        auto error = std::error_code();
        const auto directory = std::filesystem::current_path(error);
        if (error) {
            std::println(
                stderr,
                "carven-format: cannot resolve current directory: {}",
                error.message()
            );
            return 2;
        }
        const auto report = format_check_report(*batch, directory);
        if (write_stdout(report) != 0) {
            return 2;
        }
        return report.empty() ? 0 : 1;
    }
    if (mode == OutputMode::Write) {
        const auto written = write_formatted_batch(*batch);
        if (!written) {
            std::println(stderr, "carven-format: {}", written.error());
            return 2;
        }
        return 0;
    }
    return write_stdout(batch->files().front().formatted);
}
