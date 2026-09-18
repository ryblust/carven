module carven:driver.dump.impl;

import :diagnostics.report;
import :driver.dump;
import :driver.timings;
import :frontend.dump.ast;
import :frontend.dump.tokens;
import :frontend.lex;
import :frontend.parse;
import :source.manager;
import :source.text;
import :support.timing;
import std;

namespace {

enum class DumpKind {
    All,
    Tokens,
    AST,
};

struct DumpRequest final {
    DumpKind kind;
    std::string_view input_path;
    bool timings;
};

auto print_help() noexcept -> int {
    std::print(
        "Inspect source syntax.\n"
        "\n"
        "Usage:\n"
        "  carven dump [kind] [options] <source-file>\n"
        "\n"
        "Kinds:\n"
        "  tokens    Print the token stream\n"
        "  ast       Print the syntax tree\n"
        "\n"
        "Options:\n"
        "      --timings Show total and stage timings on stderr\n"
        "  -h, --help    Show this help\n"
        "\n"
        "Reads one source file without semantic analysis.\n"
        "Without a kind, prints both tokens and the syntax tree.\n"
    );
    return 0;
}

auto parse_request(std::span<const char* const> args) noexcept
    -> std::expected<DumpRequest, std::string> {
    auto positional = std::vector<std::string_view>();
    auto show_timings = false;
    for (const auto argument : args) {
        const auto arg = std::string_view(argument);
        if (arg == "--timings") {
            show_timings = true;
        } else if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown option '{}'", arg));
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty()
        || positional.size() > 2
        || (positional.size() == 1 && (positional[0] == "tokens" || positional[0] == "ast"))) {
        return std::unexpected("expected '[tokens|ast] <source-file>'");
    }
    auto kind = DumpKind::All;
    if (positional.size() == 2) {
        if (positional[0] == "tokens") {
            kind = DumpKind::Tokens;
        } else if (positional[0] == "ast") {
            kind = DumpKind::AST;
        } else {
            return std::unexpected(std::format("unknown dump kind '{}'", positional[0]));
        }
    }
    return DumpRequest {.kind = kind, .input_path = positional.back(), .timings = show_timings};
}

} // namespace

auto run_dump_command(std::span<const char* const> args) noexcept -> int {
    if (args.size() == 1
        && (std::string_view(args.front()) == "--help" || std::string_view(args.front()) == "-h")) {
        return print_help();
    }

    const auto request = parse_request(args);
    if (!request) {
        std::println(std::cerr, "carven dump: error: {}", request.error());
        std::println(std::cerr, "Run 'carven dump --help' for usage.");
        return 1;
    }

    auto timings = CommandTimings(request->timings, "dump");
    auto sources = SourceManager();
    auto loading = TimingScope(timings.recorder(), TimingStage::SourceLoading);
    const auto source_id = sources.append_file(request->input_path);
    loading.stop();
    if (!source_id) {
        std::println(
            std::cerr,
            "carven dump: error: {}: '{}'",
            source_id.error().message,
            source_id.error().origin
        );
        return 1;
    }

    auto lexing = TimingScope(timings.recorder(), TimingStage::Lexing);
    const auto lexical = lex(sources.view(*source_id));
    lexing.stop();
    if (request->kind != DumpKind::AST) {
        if (request->kind == DumpKind::All) {
            std::println("==> Tokens <==");
        }
        std::print("{}", render_token_dump(sources, lexical.value));
    }
    if (!lexical.diagnostics.empty()) {
        std::print(std::cerr, "{}", render_diagnostics(lexical.diagnostics, sources));
        return 1;
    }
    if (request->kind == DumpKind::Tokens) {
        timings.set_outcome("finished");
        return 0;
    }

    auto parsing = TimingScope(timings.recorder(), TimingStage::Parsing);
    const auto parsed = parse(sources, lexical.value);
    parsing.stop();
    if (!parsed) {
        std::print(std::cerr, "{}", render_diagnostics(parsed.error(), sources));
        return 1;
    }
    if (request->kind == DumpKind::All) {
        std::println("\n==> AST <==");
    }
    std::print("{}", render_ast_dump(sources, *parsed));
    timings.set_outcome("finished");
    return 0;
}
