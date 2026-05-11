module zero.frontend.parser.ast;

import std;

auto format_parse_error(const ParseError& error) noexcept -> std::string {
    return std::format("error:{}:{}: {}", error.location.line, error.location.column, error.message);
}

auto to_string(BinOp op) noexcept -> const char* {
    using enum BinOp;
    switch (op) {
        case Add: return "+";  case Sub:    return "-";  case Mul:   return "*";  case Div:    return "/";
        case Mod: return "%";  case BitAnd: return "&";  case BitOr: return "|";  case BitXor: return "^";
        case Shl: return "<<"; case Shr:    return ">>"; case Eq:    return "=="; case Ne:     return "!=";
        case Lt:  return "<";  case Le:     return "<="; case Gt:    return ">";  case Ge:     return ">=";
        case LogicalAnd: return "&&"; case LogicalOr: return "||"; default: return "?";
    }
}

auto to_string(UnaryOp op) noexcept -> const char* {
    using enum UnaryOp;
    switch (op) {
        case Plus:       return "+"; case Neg:    return "-";  case BitNot: return "~";
        case LogicalNot: return "!"; case PreInc: return "++"; case PreDec: return "--";
        case AddressOf:  return "&"; case Deref:  return "*";  default:     return "?";
    }
}

auto to_string(PostfixOp op) noexcept -> const char* {
    using enum PostfixOp;
    switch (op) { case PostInc: return "++"; case PostDec: return "--"; default: return "?"; }
}

auto ParseResult::has_errors() const noexcept -> bool {
    return !errors.empty();
}
