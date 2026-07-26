module carven:driver.dump.impl;

import :diagnostics.report;
import :driver.dump;
import :frontend.dump.ast;
import :frontend.dump.tokens;
import :frontend.lex;
import :frontend.parse;
import :source.manager;
import :source.text;
import std;

namespace {

enum class DumpKind {
    Tokens,
    AST,
};

struct DumpRequest final {
    DumpKind kind;
    std::string_view input_path;
};

auto print_help() noexcept -> int {
    std::print(
        "carven dump - Developer syntax inspection\n"
        "\n"
        "USAGE:\n"
        "    carven dump tokens <source-file>\n"
        "    carven dump ast <source-file>\n"
    );
    return 0;
}

auto parse_request(std::span<const char* const> args) noexcept
    -> std::expected<DumpRequest, std::string> {
    if (args.size() != 2) {
        return std::unexpected("expected 'tokens <source-file>' or 'ast <source-file>'");
    }
    const auto command = std::string_view(args[0]);
    const auto input_path = std::string_view(args[1]);
    if (input_path.starts_with('-')) {
        return std::unexpected(std::format("unknown option '{}'", input_path));
    }
    if (command == "tokens") {
        return DumpRequest {.kind = DumpKind::Tokens, .input_path = input_path};
    }
    if (command == "ast") {
        return DumpRequest {.kind = DumpKind::AST, .input_path = input_path};
    }
    return std::unexpected(std::format("unknown dump kind '{}'", command));
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
        return 1;
    }

    auto sources = SourceManager();
    const auto source_id = sources.append_file(request->input_path);
    if (!source_id) {
        std::println(
            std::cerr,
            "carven dump: error: {}: '{}'",
            source_id.error().message,
            source_id.error().origin
        );
        return 1;
    }

    const auto lexical = lex(sources.view(*source_id));
    if (request->kind == DumpKind::Tokens) {
        std::print("{}", render_token_dump(sources, lexical.value));
    }
    if (!lexical.diagnostics.empty()) {
        std::print(std::cerr, "{}", render_diagnostics(lexical.diagnostics, sources));
        return 1;
    }
    if (request->kind == DumpKind::Tokens) {
        return 0;
    }

    const auto parsed = parse(sources, lexical.value);
    if (!parsed) {
        std::print(std::cerr, "{}", render_diagnostics(parsed.error(), sources));
        return 1;
    }
    std::print("{}", render_ast_dump(sources, *parsed));
    return 0;
}
