export module zero.frontend.parser;

import zero.common.source;
import zero.frontend.token;
import std;

struct Block final {
    enum class Category : std::uint64_t {
        Import,
        Enum,
        Struct,
        Function
    } category;
    std::span<const Token> tokens;
};

constexpr auto blockify(std::span<const Token> tokens, std::string_view source) noexcept -> std::vector<Block> {
    auto blocks = std::vector<Block>();

    const auto slice = [](const auto begin, const auto end) noexcept {
        return std::span<const Token>(begin, end);
    };

    for (auto i = 0uz; i < tokens.size();) {
        const auto begin = tokens.begin() + i;
        const auto& token = tokens[i];

        if (token.kind != TokenKind::Keyword) {
            i++;
            continue;
        }

        const auto keyword = text_at(source, token.span);

        if (keyword == "import") {
            const auto end = std::ranges::find_if(begin, tokens.end(), [](Token t) {
                return t.kind == TokenKind::SemiColon || t.kind == TokenKind::RightBrace;
            });
            blocks.emplace_back(Block::Category::Import, slice(begin, end == tokens.end() ? end : end + 1));
            i += end == tokens.end() ? end - begin : end - begin + 1;

        } else if (keyword == "enum" || keyword == "struct") {
            const auto cat = keyword == "enum" ? Block::Category::Enum : Block::Category::Struct;
            const auto end = std::ranges::find_if(begin, tokens.end(), [](Token t) {
                return t.kind == TokenKind::RightBrace;
            });
            blocks.emplace_back(cat, slice(begin, end == tokens.end() ? end : end + 1));
            i += end == tokens.end() ? end - begin : end - begin + 1;
        } else if (keyword == "fn") {
            const auto lbrace = std::ranges::find_if(begin, tokens.end(), [](Token t) {
                return t.kind == TokenKind::LeftBrace;
            });
            if (lbrace != tokens.end()) {
                auto depth = 1uz;
                auto end = lbrace + 1;
                for (; end < tokens.end() && depth > 0; ++end) {
                    if (end->kind == TokenKind::LeftBrace) depth++;
                    else if (end->kind == TokenKind::RightBrace) depth--;
                }
                blocks.emplace_back(Block::Category::Function, slice(begin, end == tokens.end() ? end : end + 1));
                i += end == tokens.end() ? end - begin : end - begin + 1;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }

    return blocks;
}

export struct ImportItem final {
    Span module_name;
    std::vector<Span> using_decls;
};

export struct EnumItem final {
    Span name;
    Span size;
    std::vector<Span> fields;
};

export struct StructField final {
    Span name;
    Span type;
};

export struct StructItem final {
    Span name;
    std::vector<StructField> fields;
};

export struct FunctionParam final {
    Span name;
    Span type;
};

export struct FunctionItem final {
    Span name;
    std::vector<FunctionParam> params;
    Span return_type;
    std::span<const Token> body_tokens;
};

constexpr auto parse_import_block(std::span<const Token> tokens) noexcept -> ImportItem {
    auto import_item = ImportItem {
        .module_name = tokens[1].span,
        .using_decls = std::vector<Span>()
    };

    if (tokens[2].kind == TokenKind::Keyword) {
        if (tokens[4].kind == TokenKind::SemiColon) {
            import_item.using_decls.emplace_back(tokens[3].span);
        } else {
            for (auto i = 4uz; i < tokens.size() - 1; ++i) {
                if (tokens[i].kind == TokenKind::Identifier) {
                    import_item.using_decls.emplace_back(tokens[i].span);
                }
            }
        }
    }

    return import_item;
}

constexpr auto parse_enum_block(std::span<const Token> tokens) noexcept -> EnumItem {
    const auto lbrace = std::ranges::find_if(tokens, [](Token t) {
        return t.kind == TokenKind::LeftBrace;
    });

    auto item = EnumItem {
        .name = tokens[1].span,
        .size = Span{},
        .fields = std::vector<Span>()
    };

    if (lbrace != tokens.end() && tokens[2].kind == TokenKind::Colon) {
        item.size = tokens[3].span;
    }

    for (auto it = lbrace + 1; it < tokens.end() - 1; ++it) {
        if (it->kind == TokenKind::Identifier) {
            item.fields.emplace_back(it->span);
        }
    }

    return item;
}

constexpr auto parse_struct_block(std::span<const Token> tokens) noexcept -> StructItem {
    const auto lbrace = std::ranges::find_if(tokens, [](Token t) {
        return t.kind == TokenKind::LeftBrace;
    });

    auto item = StructItem {
        .name = tokens[1].span,
        .fields = std::vector<StructField>()
    };

    auto it = lbrace + 1;
    while (it < tokens.end() - 1) {
        if (it->kind == TokenKind::Identifier) {
            const auto field_name = it->span;
            it += 2; // skip ':'
            const auto field_type = it->span;
            item.fields.emplace_back(field_name, field_type);
        }
        ++it;
    }

    return item;
}

constexpr auto parse_function_block(std::span<const Token> tokens) noexcept -> FunctionItem {
    const auto lparen = std::ranges::find_if(tokens, [](Token t) {
        return t.kind == TokenKind::LeftParen;
    });
    const auto rparen = std::ranges::find_if(lparen + 1, tokens.end(), [](Token t) {
        return t.kind == TokenKind::RightParen;
    });

    auto params = std::vector<FunctionParam>();
    auto it = lparen + 1;
    while (it < rparen) {
        if (it->kind == TokenKind::Identifier) {
            const auto param_name = it->span;
            it += 2;
            const auto param_type = it->span;
            params.emplace_back(param_name, param_type);
        }
        ++it;
    }

    auto return_type = Span{};
    const auto arrow = std::ranges::find_if(rparen + 1, tokens.end(), [](Token t) {
        return t.kind == TokenKind::Arrow;
    });
    if (arrow != tokens.end()) {
        return_type = (arrow + 1)->span;
    }

    const auto lbrace = std::ranges::find_if(tokens, [](Token t) {
        return t.kind == TokenKind::LeftBrace;
    });

    return FunctionItem {
        .name = tokens[1].span,
        .params = std::move(params),
        .return_type = return_type,
        .body_tokens = std::span<const Token>(lbrace, tokens.end())
    };
}

export using TopLevelItem = std::variant<ImportItem, EnumItem, StructItem, FunctionItem>;

constexpr auto parse_blocks(std::span<const Block> blocks) noexcept -> std::vector<TopLevelItem> {
    auto items = std::vector<TopLevelItem>(blocks.size());

    for (auto i = 0uz; i < items.size(); ++i) {
        if (blocks[i].category == Block::Category::Import) {
            items[i] = parse_import_block(blocks[i].tokens);
        } else if (blocks[i].category == Block::Category::Enum) {
            items[i] = parse_enum_block(blocks[i].tokens);
        } else if (blocks[i].category == Block::Category::Struct) {
            items[i] = parse_struct_block(blocks[i].tokens);
        } else if (blocks[i].category == Block::Category::Function) {
            items[i] = parse_function_block(blocks[i].tokens);
        }
    }

    return items;
}

export constexpr auto parse(std::span<const Token> tokens, std::string_view source) noexcept -> std::vector<TopLevelItem> {
    return parse_blocks(blockify(tokens, source));
}
