module zero.frontend.lexer.token;

import std;

auto display_token_type(TokenKind kind) noexcept -> const char* {
    switch (kind) {
        using enum TokenKind;

        case Identifier:            return "Identifier";

        case NumberLiteral:         return "NumberLiteral";
        case CharLiteral:           return "CharLiteral";
        case StringLiteral:         return "StringLiteral";

        case Keyword:               return "Keyword";

        case Comma:                 return "Comma";
        case Dot:                   return "Dot";
        case Colon:                 return "Colon";
        case SemiColon:             return "Semicolon";
        case LeftParen:             return "LeftParen";
        case RightParen:            return "RightParen";
        case LeftBracket:           return "LeftBracket";
        case RightBracket:          return "RightBracket";
        case LeftBrace:             return "LeftBrace";
        case RightBrace:            return "RightBrace";

        case Plus:                  return "Plus";
        case Minus:                 return "Minus";
        case Star:                  return "Star";
        case Slash:                 return "Slash";
        case Percent:               return "Percent";
        case Bang:                  return "Bang";
        case Equal:                 return "Equal";
        case Less:                  return "Less";
        case Greater:               return "Greater";

        case PlusEqual:             return "PlusEqual";
        case MinusEqual:            return "MinusEqual";
        case StarEqual:             return "StarEqual";
        case SlashEqual:            return "SlashEqual";
        case PercentEqual:          return "PercentEqual";
        case BangEqual:             return "BangEqual";
        case EqualEqual:            return "EqualEqual";
        case LessEqual:             return "LessEqual";
        case GreaterEqual:          return "GreaterEqual";
        case PlusPlus:              return "PlusPlus";
        case MinusMinus:            return "MinusMinus";

        case Arrow:                 return "Arrow";
        case FatArrow:              return "FatArrow";
        case ColonColon:            return "ColonColon";

        case AmpersandAmpersand:    return "AmpersandAmpersand";
        case PipePipe:              return "PipePipe";

        case Ampersand:             return "Ampersand";
        case Pipe:                  return "Pipe";
        case Caret:                 return "Caret";
        case Tilde:                 return "Tilde";
        case LeftShift:             return "LeftShift";
        case RightShift:            return "RightShift";
        case AmpersandEqual:        return "AmpersandEqual";
        case PipeEqual:             return "PipeEqual";
        case CaretEqual:            return "CaretEqual";
        case LeftShiftEqual:        return "LeftShiftEqual";
        case RightShiftEqual:       return "RightShiftEqual";

        case End:                   return "End";
        case Error:                 return "Error";
        default:                    return "Unknown";
    }
}
