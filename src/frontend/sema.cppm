export module carven.frontend.sema;

import carven.common.source;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

class Sema final {
public:
    constexpr explicit Sema(std::string_view source) noexcept : source(source) {}

    constexpr auto analyze(std::span<const TopLevelItem> items) noexcept -> std::vector<ParseError> {
        for (const auto& item : items) {
            std::visit(Overloaded {
                [&](const ImportItem&   it) noexcept { check_import(it);   },
                [&](const EnumItem&     it) noexcept { check_enum(it);     },
                [&](const StructItem&   it) noexcept { check_struct(it);   },
                [&](const FunctionItem& it) noexcept { check_function(it); },
            }, item);
        }
        return std::move(errors);
    }

private:
    std::string_view source;
    std::vector<ParseError> errors;

    constexpr auto push_error(std::string_view message, Span span) noexcept -> void {
        errors.emplace_back(std::string(message), span);
    }

    constexpr auto check_ident(Span span) noexcept -> void {
        if (slice(source, span).starts_with("__carven_")) {
            push_error("identifiers starting with '__carven_' are reserved", span);
        }
    }

    constexpr auto check_type(const Type* type) noexcept -> void {
        if (type == nullptr) return;

        switch (type->kind) {
            case TypeKind::Name:
                check_ident(static_cast<const NameType*>(type)->span);
                return;
            case TypeKind::Array: {
                check_ident(static_cast<const ArrayType*>(type)->elem_type);
                return;
            }
        }
    }

    constexpr auto expr_span(const Expr& expr) noexcept -> Span {
        switch (expr.kind) {
            case ExprKind::Literal:        return static_cast<const LiteralExpr*>(&expr)->token;
            case ExprKind::Ident:          return static_cast<const IdentExpr*>(&expr)->name;
            case ExprKind::Prefix:         return static_cast<const PrefixExpr*>(&expr)->span;
            case ExprKind::Postfix:        return static_cast<const PostfixExpr*>(&expr)->span;
            case ExprKind::Binary:         return static_cast<const BinaryExpr*>(&expr)->span;
            case ExprKind::Call:           return static_cast<const CallExpr*>(&expr)->lparen;
            case ExprKind::Index:          return static_cast<const IndexExpr*>(&expr)->lbracket;
            case ExprKind::Field:          return static_cast<const FieldExpr*>(&expr)->dot;
            case ExprKind::Group:          return static_cast<const GroupExpr*>(&expr)->lparen;
            case ExprKind::Assign:         return static_cast<const AssignExpr*>(&expr)->eq;
            case ExprKind::CompoundAssign: return static_cast<const CompoundAssignExpr*>(&expr)->span;
            case ExprKind::Comma:          return static_cast<const CommaExpr*>(&expr)->comma;
            case ExprKind::If:             return static_cast<const IfExpr*>(&expr)->keyword;
            case ExprKind::Array:          return static_cast<const ArrayExpr*>(&expr)->lbracket;
            case ExprKind::Match:          return static_cast<const MatchExpr*>(&expr)->keyword;
        }
        return {};
    }

    constexpr auto stmt_span(const Stmt& stmt) noexcept -> Span {
        switch (stmt.kind) {
            case StmtKind::Block:    return static_cast<const BlockStmt*>(&stmt)->lbrace;
            case StmtKind::ExprStmt: return expr_span(*static_cast<const ExprStmt*>(&stmt)->expr);
            case StmtKind::Empty:    return static_cast<const EmptyStmt*>(&stmt)->semicolon;
            case StmtKind::VarDecl:  return static_cast<const VarDecl*>(&stmt)->keyword;
            case StmtKind::Return:   return static_cast<const ReturnStmt*>(&stmt)->keyword;
            case StmtKind::While:    return static_cast<const WhileStmt*>(&stmt)->keyword;
            case StmtKind::For:      return static_cast<const ForStmt*>(&stmt)->keyword;
        }
        return {};
    }

    constexpr auto check_import(const ImportItem& item) noexcept -> void {
        check_ident(item.module_name);
        for (const auto decl : item.using_decls) {
            check_ident(decl);
        }
    }

    constexpr auto check_enum(const EnumItem& item) noexcept -> void {
        check_ident(item.name);
        check_type(item.size);
        for (const auto field : item.fields) {
            check_ident(field);
        }
    }

    constexpr auto check_struct(const StructItem& item) noexcept -> void {
        check_ident(item.name);
        for (const auto field : item.fields) {
            check_ident(field.name);
            check_type(field.type);
        }
    }

    constexpr auto check_function(const FunctionItem& item) noexcept -> void {
        check_ident(item.name);
        for (const auto param : item.params) {
            check_ident(param.name);
            check_type(param.type);
        }
        check_type(item.return_type);
        check_block(*item.body, true);
    }

    constexpr auto check_block(const BlockStmt& block, bool allow_return) noexcept -> void {
        for (const auto stmt : block.statements) {
            check_stmt(*stmt, allow_return);
        }
    }

    constexpr auto check_stmt(const Stmt& stmt, bool allow_return) noexcept -> void {
        switch (stmt.kind) {
            case StmtKind::Block:
                check_block(*static_cast<const BlockStmt*>(&stmt), allow_return);
                return;
            case StmtKind::ExprStmt: {
                const auto s = static_cast<const ExprStmt*>(&stmt);
                const auto is_control_stmt = s->expr->kind == ExprKind::If || s->expr->kind == ExprKind::Match;
                check_expr(*s->expr, !is_control_stmt, allow_return);
                return;
            }
            case StmtKind::Empty:
                return;
            case StmtKind::VarDecl: {
                const auto s = static_cast<const VarDecl*>(&stmt);
                check_ident(s->name);
                check_type(s->type);
                if (s->init != nullptr) check_expr(*s->init, true, allow_return);
                return;
            }
            case StmtKind::Return: {
                const auto s = static_cast<const ReturnStmt*>(&stmt);
                if (!allow_return) {
                    push_error("return is not allowed inside an expression branch", s->keyword);
                }
                if (s->value != nullptr) check_expr(*s->value, true, allow_return);
                return;
            }
            case StmtKind::While: {
                const auto s = static_cast<const WhileStmt*>(&stmt);
                check_expr(*s->condition, true, allow_return);
                check_stmt(*s->body, allow_return);
                return;
            }
            case StmtKind::For: {
                const auto s = static_cast<const ForStmt*>(&stmt);
                std::visit(Overloaded {
                    [&](const VarDecl* d) noexcept {
                        if (d != nullptr) check_stmt(*d, allow_return);
                    },
                    [&](const ExprStmt* es) noexcept {
                        if (es != nullptr) check_expr(*es->expr, true, allow_return);
                    },
                }, s->init);
                if (s->condition != nullptr) check_expr(*s->condition, true, allow_return);
                if (s->step != nullptr) check_expr(*s->step, true, allow_return);
                check_stmt(*s->body, allow_return);
                return;
            }
        }
    }

    constexpr auto check_expr(const Expr& expr, bool value_position, bool allow_return) noexcept -> void {
        switch (expr.kind) {
            case ExprKind::Literal:
                return;
            case ExprKind::Ident:
                check_ident(static_cast<const IdentExpr*>(&expr)->name);
                return;
            case ExprKind::Prefix:
                check_expr(*static_cast<const PrefixExpr*>(&expr)->rhs, true, allow_return);
                return;
            case ExprKind::Postfix:
                check_expr(*static_cast<const PostfixExpr*>(&expr)->lhs, true, allow_return);
                return;
            case ExprKind::Binary: {
                const auto e = static_cast<const BinaryExpr*>(&expr);
                check_expr(*e->lhs, true, allow_return);
                check_expr(*e->rhs, true, allow_return);
                return;
            }
            case ExprKind::Call: {
                const auto e = static_cast<const CallExpr*>(&expr);
                check_expr(*e->callee, true, allow_return);
                for (const auto arg : e->args) check_expr(*arg, true, allow_return);
                return;
            }
            case ExprKind::Index: {
                const auto e = static_cast<const IndexExpr*>(&expr);
                check_expr(*e->lhs, true, allow_return);
                check_expr(*e->index, true, allow_return);
                return;
            }
            case ExprKind::Field: {
                const auto e = static_cast<const FieldExpr*>(&expr);
                check_expr(*e->lhs, true, allow_return);
                check_ident(e->field);
                return;
            }
            case ExprKind::Group:
                check_expr(*static_cast<const GroupExpr*>(&expr)->inner, true, allow_return);
                return;
            case ExprKind::Assign: {
                const auto e = static_cast<const AssignExpr*>(&expr);
                check_expr(*e->lhs, true, allow_return);
                check_expr(*e->rhs, true, allow_return);
                return;
            }
            case ExprKind::CompoundAssign: {
                const auto e = static_cast<const CompoundAssignExpr*>(&expr);
                check_expr(*e->lhs, true, allow_return);
                check_expr(*e->rhs, true, allow_return);
                return;
            }
            case ExprKind::Comma: {
                const auto e = static_cast<const CommaExpr*>(&expr);
                check_expr(*e->lhs, true, allow_return);
                check_expr(*e->rhs, true, allow_return);
                return;
            }
            case ExprKind::If:
                check_if(*static_cast<const IfExpr*>(&expr), value_position, allow_return);
                return;
            case ExprKind::Array: {
                for (const auto element : static_cast<const ArrayExpr*>(&expr)->elements) check_expr(*element, true, allow_return);
                return;
            }
            case ExprKind::Match:
                check_match(*static_cast<const MatchExpr*>(&expr), value_position, allow_return);
                return;
        }
    }

    constexpr auto check_value_branch(const BlockStmt& block, std::string_view label) noexcept -> void {
        if (block.statements.empty()) {
            push_error(std::string(label) + " must end with an expression without ';'", block.rbrace);
            return;
        }

        for (auto i = 0uz; i + 1 < block.statements.size(); ++i) {
            check_stmt(*block.statements[i], false);
        }

        const auto last = block.statements.back();
        if (last->kind != StmtKind::ExprStmt) {
            push_error(std::string(label) + " must end with an expression without ';'", stmt_span(*last));
            check_stmt(*last, false);
            return;
        }

        const auto expr_stmt = static_cast<const ExprStmt*>(last);
        if (!expr_stmt->semicolon.empty()) {
            push_error(std::string(label) + " final expression must omit ';'", expr_stmt->semicolon);
        }
        check_expr(*expr_stmt->expr, true, false);
    }

    constexpr auto check_if(const IfExpr& expr, bool value_position, bool allow_return) noexcept -> void {
        check_expr(*expr.condition, true, allow_return);

        if (value_position) {
            if (expr.else_branch == nullptr) {
                push_error("if expression requires an else branch", expr.keyword);
            }
            check_value_branch(*expr.then_branch, "if expression branch");
            if (expr.else_branch != nullptr) {
                check_value_branch(*expr.else_branch, "if expression branch");
            }
            return;
        }

        check_block(*expr.then_branch, allow_return);
        if (expr.else_branch != nullptr) {
            check_block(*expr.else_branch, allow_return);
        }
    }

    constexpr auto check_match(const MatchExpr& expr, bool value_position, bool allow_return) noexcept -> void {
        check_expr(*expr.value, true, allow_return);

        auto has_wildcard = false;
        for (auto i = 0uz; i < expr.arms.size(); ++i) {
            const auto& arm = expr.arms[i];
            if (has_wildcard) {
                push_error("match wildcard arm must be last", arm.arrow);
            }
            if (arm.is_wildcard) {
                has_wildcard = true;
            }
            for (const auto pattern : arm.patterns) {
                check_expr(*pattern, true, allow_return);
            }
            if (value_position) {
                check_value_branch(*arm.body, "match expression arm");
            } else {
                check_block(*arm.body, allow_return);
            }
        }

        if (value_position && !has_wildcard) {
            push_error("match expression requires a wildcard '_' arm", expr.keyword);
        }
    }
};

export constexpr auto analyze(std::span<const TopLevelItem> items, std::string_view source) noexcept -> std::vector<ParseError> {
    return Sema(source).analyze(items);
}
