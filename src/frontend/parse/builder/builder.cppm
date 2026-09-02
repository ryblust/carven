module carven:frontend.parse.builder;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.type;

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
    auto finish() && noexcept -> ASTStorage;

private:
    ASTStorage storage;
};
