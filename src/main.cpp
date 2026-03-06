import zero.transpiler.token;
import zero.transpiler.lexer;
import zero.transpiler.parser;

import std;

static constexpr auto SOURCE = std::string_view(
    R"(
        import std using {
            string, vector, println,
        }

        import zero using {
            Device, Adapter, Buffer
        }

        struct ImportModuleInfo {
            module_name: string,
            using_decls: vector<string>
        }

        struct Archive<T> {
            title: string,
            documents: vector<T>,
        }

        fn main() {
            println("Hello World!");
        }
    )"
);

struct ImportModuleInfo final {
    std::string_view module_name;
    std::vector<std::string_view> using_decls;
};

template<> struct std::formatter<ImportModuleInfo> final {
    constexpr auto parse(const std::format_parse_context& ctx) const noexcept {
        return ctx.begin();
    }

    constexpr auto format(const ImportModuleInfo& info, std::format_context& ctx) const noexcept {
        std::format_to(ctx.out(), "Module Name: {}\n", info.module_name);

        if (!info.using_decls.empty()) {
            std::format_to(ctx.out(), "Using Declarations:\n");
        }

        for (const auto decl : info.using_decls) {
            std::format_to(ctx.out(),  "\t{}\n", decl);
        }

        return ctx.out();
    }
};

constexpr auto parse_import_statement(std::span<const Token> tokens) noexcept -> ImportModuleInfo {
    auto module_info = ImportModuleInfo {
        .module_name = tokens[1].lexeme,
        .using_decls = std::vector<std::string_view>()
    };

    if (tokens[2].type == TokenType::Keyword && tokens[2].lexeme == "using") {
        for (auto i = 3uz; i < tokens.size(); i++) {
            if (tokens[i].type == TokenType::Identifier) {
                module_info.using_decls.emplace_back(tokens[i].lexeme);
            }
        }
    }

    return module_info;
}

constexpr auto codegen_from(const ImportModuleInfo& info) noexcept -> std::string {
    auto result = std::string("import ").append(info.module_name).append(";\n");

    if (!info.using_decls.empty()) {
        result.append("\n");
        for (auto i = 0uz; i < info.using_decls.size(); i++) {
            result
                .append("using ")
                .append(info.module_name).append("::")
                .append(info.using_decls[i]).append(";\n");
        }
    }

    return result;
}

struct PODStructInfo final {
    std::string_view name;
};

constexpr auto parse_pod_struct(std::span<const Token> tokens) noexcept -> PODStructInfo {
    auto info = PODStructInfo {
        .name = tokens[1].lexeme,
    };


    return info;
}

auto main() noexcept -> int {
    const auto tokens = tokenize(SOURCE);
    const auto blocks = blockify(tokens);

    std::println("{}", blocks[0].tokens);
    std::println("{}", blocks[1].tokens);
}