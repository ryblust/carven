module carven:frontend.dump.ast;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.literal;
import :source.manager;
import :source.text;
import std;

auto format_dump_span(Span span) noexcept -> std::string;

auto render_ast_dump(const SourceManager& sources, const SyntaxTree& syntax_tree) noexcept
    -> std::string;

class ASTDumper final {
public:
    ASTDumper(ASTView ast, std::string_view source_text, std::string_view source_origin) noexcept;

    auto render() noexcept -> std::string;

private:
    ASTView ast;
    std::string_view source_text;
    std::string_view source_origin;
    std::string output;

    auto source_label(Span span) const noexcept -> std::string;
    auto append_line(std::string_view prefix, bool is_last, std::string_view label) noexcept
        -> void;
    static auto child_prefix(std::string_view prefix, bool is_last) noexcept -> std::string;

    template<typename Range, typename Renderer>
    auto render_list(
        std::string_view prefix,
        bool is_last,
        std::string_view name,
        const Range& values,
        Renderer renderer
    ) noexcept -> void {
        append_line(prefix, is_last, std::format("{} ({})", name, values.size()));
        const auto nested_prefix = child_prefix(prefix, is_last);
        for (auto index = 0uz; index < values.size(); ++index) {
            renderer(values[index], nested_prefix, index + 1 == values.size());
        }
    }

    auto render_span_field(
        std::string_view prefix,
        bool is_last,
        std::string_view name,
        Span span
    ) noexcept -> void;
    auto render_type(
        ASTTypeID type,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_expression(
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_expression(
        const ASTLiteral& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTNameExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTContextualCaseExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTGroupExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTArrayExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTConstructionExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTPrefixExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTAccessExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTBinaryExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTCastExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTCallExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTIndexExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTMemberExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTLambdaExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTPropagationExpr& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTIfForm& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTMatchForm& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const ASTTryForm& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_expression(
        const CppRegion& value,
        ASTExprID expression,
        std::string_view prefix,
        bool is_last,
        std::string_view field
    ) noexcept -> void;
    auto render_statement(ASTStmtID statement, std::string_view prefix, bool is_last) noexcept
        -> void;
    auto render_literal(
        const ASTLiteral& literal,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_numeric_literal(
        Span span,
        const NumericLiteralValue& value,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_cpp_region(
        const CppRegion& region,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_construction_type(
        const ASTConstructionType& type,
        std::string_view prefix,
        bool is_last
    ) noexcept -> void;
    auto render_variable_declaration(
        const ASTVariableDecl& declaration,
        std::string_view prefix,
        bool is_last,
        Span outer_span
    ) noexcept -> void;
    auto render_assignment(
        const ASTAssignment& assignment,
        std::string_view prefix,
        bool is_last,
        Span outer_span
    ) noexcept -> void;
    auto render_update(
        const ASTUpdate& update,
        std::string_view prefix,
        bool is_last,
        Span outer_span
    ) noexcept -> void;
    auto render_control_transfer(
        const ASTControlTransfer& transfer,
        std::string_view prefix,
        bool is_last,
        Span outer_span
    ) noexcept -> void;
    auto render_branch_block(
        ASTBranchBlockID block,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_ordinary_block(
        ASTBlockID block,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_if_form(
        const ASTIfForm& form,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_match_form(
        const ASTMatchForm& form,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_try_form(
        const ASTTryForm& form,
        std::string_view prefix,
        bool is_last,
        std::string_view field = {}
    ) noexcept -> void;
    auto render_throw_clause(
        const std::optional<ASTThrowClause>& clause,
        std::string_view prefix,
        bool is_last
    ) noexcept -> void;
    auto render_import(ASTImportID declaration, std::string_view prefix, bool is_last) noexcept
        -> void;
    auto render_top_level_item(ASTItemID item, std::string_view prefix, bool is_last) noexcept
        -> void;
    auto render_pattern(ASTPatternID pattern, std::string_view prefix, bool is_last) noexcept
        -> void;
    auto render_for_header(
        const ASTForHeader& header,
        std::string_view prefix,
        bool is_last
    ) noexcept -> void;

    auto render_named_type_children(
        const ASTNamedType& named,
        std::string_view prefix,
        bool is_last
    ) noexcept -> void;
    auto render_function_type_children(
        const ASTFunctionType& function,
        std::string_view prefix
    ) noexcept -> void;
};
