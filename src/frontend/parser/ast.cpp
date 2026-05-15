module zero.frontend.parser.ast;

import std;

auto format_parse_error(const ParseError& error) noexcept -> std::string {
    return std::format("error:{}:{}: {}", error.location.line, error.location.column, error.message);
}

auto ParseResult::has_errors() const noexcept -> bool {
    return !errors.empty();
}
