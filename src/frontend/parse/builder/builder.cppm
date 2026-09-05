module carven:frontend.parse.builder;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :source.text;
import std;

class Parser;

class ASTBuilder final {
public:
    struct Checkpoint final {
        ASTStorage::ExprTable::Checkpoint expressions;
        ASTStorage::TypeTable::Checkpoint types;
        ASTStorage::StatementTable::Checkpoint statements;
        ASTStorage::PatternTable::Checkpoint patterns;
        ASTStorage::BlockTable::Checkpoint blocks;
        ASTStorage::BranchBlockTable::Checkpoint branch_blocks;
        ASTStorage::ItemTable::Checkpoint items;
        ASTStorage::ModuleImportTable::Checkpoint module_imports;
    };

    ASTBuilder(const ASTBuilder&) = delete;
    ASTBuilder(ASTBuilder&&) = default;
    auto operator=(const ASTBuilder&) -> ASTBuilder& = delete;
    auto operator=(ASTBuilder&&) -> ASTBuilder& = default;

private:
    explicit ASTBuilder(SourceView source) noexcept;
    auto append_expression(ASTExpr value) noexcept -> ASTExprID;
    auto append_type(ASTType value) noexcept -> ASTTypeID;
    auto append_statement(ASTStmt value) noexcept -> ASTStmtID;
    auto append_pattern(ASTPattern value) noexcept -> ASTPatternID;
    auto append_block(ASTBlock value) noexcept -> ASTBlockID;
    auto append_branch_block(ASTBranchBlock value) noexcept -> ASTBranchBlockID;
    auto append_item(ASTItem value) noexcept -> ASTItemID;
    auto append_module_import(ASTModuleImport value) noexcept -> ASTModuleImportID;
    auto expression(ASTExprID id) const noexcept -> const ASTExpr&;
    auto type(ASTTypeID id) const noexcept -> const ASTType&;
    auto pattern(ASTPatternID id) const noexcept -> const ASTPattern&;
    auto block(ASTBlockID id) const noexcept -> const ASTBlock&;
    auto branch_block(ASTBranchBlockID id) const noexcept -> const ASTBranchBlock&;
    auto checkpoint() const noexcept -> Checkpoint;
    auto rewind(Checkpoint checkpoint) noexcept -> void;
    auto finish(ASTModule ast_module) && noexcept -> SyntaxTree;

    auto validate(Span span) const noexcept -> void;
    auto validate(ASTExprID id) const noexcept -> void;
    auto validate(ASTTypeID id) const noexcept -> void;
    auto validate(ASTStmtID id) const noexcept -> void;
    auto validate(ASTPatternID id) const noexcept -> void;
    auto validate(ASTBlockID id) const noexcept -> void;
    auto validate(ASTBranchBlockID id) const noexcept -> void;
    auto validate(ASTItemID id) const noexcept -> void;
    auto validate(ASTModuleImportID id) const noexcept -> void;

    template<typename Node>
    auto validate_topology(const Node& node) const noexcept -> void {
        visit_ast_topology(node, [&](auto field) noexcept { validate(field); });
    }

    ASTStorage storage;
    SourceID source_id;
    std::size_t source_size;

    friend class Parser;
};
