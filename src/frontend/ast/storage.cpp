module carven:frontend.ast.storage.impl;

import :frontend.ast.storage;
import std;

ASTView::ASTView(const ASTStorage& storage, const ASTModule& ast_module, SourceID source) noexcept
    : ast_storage(&storage),
      source_module(&ast_module),
      source_identity(source) {}

auto ASTView::expression(ASTExprID id) const noexcept -> const ASTExpr& {
    return ast_storage->expression_table.get(id);
}

auto ASTView::type(ASTTypeID id) const noexcept -> const ASTType& {
    return ast_storage->type_table.get(id);
}

auto ASTView::statement(ASTStmtID id) const noexcept -> const ASTStmt& {
    return ast_storage->statement_table.get(id);
}

auto ASTView::pattern(ASTPatternID id) const noexcept -> const ASTPattern& {
    return ast_storage->pattern_table.get(id);
}

auto ASTView::block(ASTBlockID id) const noexcept -> const ASTBlock& {
    return ast_storage->block_table.get(id);
}

auto ASTView::branch_block(ASTBranchBlockID id) const noexcept -> const ASTBranchBlock& {
    return ast_storage->branch_block_table.get(id);
}

auto ASTView::item(ASTItemID id) const noexcept -> const ASTItem& {
    return ast_storage->item_table.get(id);
}

auto ASTView::module_import(ASTModuleImportID id) const noexcept -> const ASTModuleImport& {
    return ast_storage->module_import_table.get(id);
}

auto ASTView::ast_module() const noexcept -> const ASTModule& {
    return *source_module;
}

auto ASTView::expressions() const noexcept -> std::span<const ASTExpr> {
    return ast_storage->expression_table.values();
}

auto ASTView::types() const noexcept -> std::span<const ASTType> {
    return ast_storage->type_table.values();
}

auto ASTView::statements() const noexcept -> std::span<const ASTStmt> {
    return ast_storage->statement_table.values();
}

auto ASTView::patterns() const noexcept -> std::span<const ASTPattern> {
    return ast_storage->pattern_table.values();
}

auto ASTView::blocks() const noexcept -> std::span<const ASTBlock> {
    return ast_storage->block_table.values();
}

auto ASTView::branch_blocks() const noexcept -> std::span<const ASTBranchBlock> {
    return ast_storage->branch_block_table.values();
}

auto ASTView::items() const noexcept -> std::span<const ASTItem> {
    return ast_storage->item_table.values();
}

auto ASTView::module_imports() const noexcept -> std::span<const ASTModuleImport> {
    return ast_storage->module_import_table.values();
}

auto ASTView::source_id() const noexcept -> SourceID {
    return source_identity;
}
