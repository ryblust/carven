export module zero.frontend.ast_walker;

import zero.common.source;
import zero.frontend.parser.ast;
import std;

export template<typename F>
constexpr auto walk_stmts(const std::vector<Stmt*>& statements, F&& visit) noexcept -> void {
    for (const auto* stmt : statements) visit(*stmt);
}

export template<typename F>
constexpr auto walk_body(const Stmt* body, F&& visit) noexcept -> void {
    if (const auto block = std::get_if<BlockStmt>(body)) {
        for (const auto* stmt : block->statements) visit(*stmt);
    } else {
        visit(*body);
    }
}

export template<typename F, typename G>
constexpr auto walk_for_init(const ForInit& init, F&& visit_var_decl, G&& visit_expr_stmt) noexcept -> void {
    std::visit(Overloaded {
        [&](const VarDecl& d) { visit_var_decl(d); },
        [&](const ExprStmt& es) { visit_expr_stmt(es); }
    }, init);
}
