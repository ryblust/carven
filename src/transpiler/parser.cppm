export module zero.transpiler.parser;

import zero.transpiler.token;
import std;

namespace {

struct Block final {
    enum class Category : std::uint64_t {
        Import,
        Struct,
        Function
    } category;
    std::span<const Token> tokens;
};

constexpr auto blockify(std::span<const Token> tokens) noexcept -> std::vector<Block> {
    return {};
}

}

export struct ImportModuleInfo final {
    std::string_view module_name;
    std::vector<std::string_view> using_decls;
};

struct StructureInfo final {};
struct FunctionInfo final {};

using ASTNode = std::variant<ImportModuleInfo, StructureInfo, FunctionInfo>;

constexpr auto parse_import_statement(std::span<const Token> tokens) noexcept -> ImportModuleInfo {
    auto module_info = ImportModuleInfo {
        .module_name = tokens[1].lexeme,
        .using_decls = std::vector<std::string_view>()
    };

    if (tokens[2].type == TokenType::Keyword) {
        if (tokens[4].type == TokenType::SemiColon) {
            module_info.using_decls.emplace_back(tokens[3].lexeme);
        } else {
            for (auto i = 4uz; i < tokens.size() - 1; i++) {
                if (tokens[i].type == TokenType::Identifier) {
                    module_info.using_decls.emplace_back(tokens[i].lexeme);
                }
            }
        }
    }

    return module_info;
}

export constexpr auto parse(std::span<const Token> tokens) noexcept -> std::unique_ptr<ASTNode> {

}

// struct NumberLiteral final {
//     std::string_view value;
// };

// struct StringLiteral final {
//     std::string_view value;
// };

// struct Identifier final {
//     std::string_view value;
// };

// struct CallExpression;
// struct BinaryExpression;
// struct UnaryExpression;

// using Expression = std::variant<
//     Identifier, NumberLiteral, StringLiteral,
//     CallExpression, BinaryExpression, UnaryExpression
// >;

// struct BinaryExpression final {
//     std::unique_ptr<Expression> left;
//     std::unique_ptr<Expression> right;
//     TokenType op;
// };

// struct UnaryExpression final {
//     TokenType op;
//     std::unique_ptr<Expression> operand;
// };

// struct CallExpression final {
//     std::unique_ptr<Expression> callee;
//     std::vector<std::unique_ptr<Expression>> arguments;
// };

// struct ExpressionStatement;
// struct LetStatement;
// struct ReturnStatement;
// struct BlockStatement;
// struct ConditionStatement;

// using Statement = std::variant<
//     ExpressionStatement,
//     LetStatement,
//     ReturnStatement,
//     BlockStatement,
//     ConditionStatement
// >;

// struct ExpressionStatement final {
//     std::unique_ptr<Expression> expr;
// };

// struct LetStatement final {
//     std::string_view name;
//     std::unique_ptr<Expression> initializer;
// };

// struct ReturnStatement final {
//     std::unique_ptr<Expression> value;
// };

// struct BlockStatement final {
//     std::vector<std::unique_ptr<Statement>> statements;
// };

// struct ConditionStatement final {
//     std::unique_ptr<Expression> condition;
//     std::unique_ptr<Statement> then_branch;
//     std::unique_ptr<Statement> else_branch;
// };

// static const auto test_if_statement = ConditionStatement {
//     .condition = std::make_unique<Expression>(BinaryExpression {
//         .left  = std::make_unique<Expression>(Identifier("a")),
//         .right = std::make_unique<Expression>(Identifier("b")),
//         .op    = TokenType::EqualEqual
//     }),
//     .then_branch   = std::make_unique<Statement>(ExpressionStatement {
//         .expr      = std::make_unique<Expression>(BinaryExpression {
//             .left  = std::make_unique<Expression>(Identifier("a")),
//             .right = std::make_unique<Expression>(Identifier("b")),
//             .op    = TokenType::Equal
//         })
//     }),
//     .else_branch = nullptr
// };

// class Parser final {
// public:
//     explicit constexpr Parser(std::span<const Token> t) noexcept
//         : tokens(t), pos(0) {}
//
//     constexpr auto parse() noexcept {}
//
// private:
//     constexpr auto current_token_type() const noexcept -> TokenType {
//         return eof() ? TokenType::End : tokens[pos].type;
//     }
//
//     constexpr auto eof() const noexcept -> bool {
//         return pos >= tokens.size();
//     }
//
//     constexpr auto match(TokenType type) const noexcept -> bool {
//         return eof() ? false : tokens[pos].type == type;
//     }
//
//     constexpr auto peek(std::size_t offset = 1) const noexcept -> TokenType {
//         const auto index = pos + offset;
//
//         return index < tokens.size() ? tokens[index].type : TokenType::End;
//     }
// private:
//     std::span<const Token> tokens;
//     std::size_t pos;
// };