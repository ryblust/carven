module carven:frontend.parse.builder.impl;

import :frontend.parse.builder;
import std;

auto ASTBuilder::append_expression(ASTExpr value) noexcept -> ASTExprID {
    return storage.expression_table.add(std::move(value));
}

auto ASTBuilder::append_type(ASTType value) noexcept -> ASTTypeID {
    return storage.type_table.add(std::move(value));
}

auto ASTBuilder::append_statement(ASTStmt value) noexcept -> ASTStmtID {
    return storage.statement_table.add(std::move(value));
}

auto ASTBuilder::append_pattern(ASTPattern value) noexcept -> ASTPatternID {
    return storage.pattern_table.add(std::move(value));
}

auto ASTBuilder::append_block(ASTBlock value) noexcept -> ASTBlockID {
    return storage.block_table.add(std::move(value));
}

auto ASTBuilder::append_branch_block(ASTBranchBlock value) noexcept -> ASTBranchBlockID {
    return storage.branch_block_table.add(std::move(value));
}

auto ASTBuilder::append_item(ASTItem value) noexcept -> ASTItemID {
    return storage.item_table.add(std::move(value));
}

auto ASTBuilder::append_import(ASTImportDecl value) noexcept -> ASTImportID {
    return storage.import_table.add(std::move(value));
}

auto ASTBuilder::expression(ASTExprID id) const noexcept -> const ASTExpr& {
    return storage.expression_table.get(id);
}

auto ASTBuilder::type(ASTTypeID id) const noexcept -> const ASTType& {
    return storage.type_table.get(id);
}

auto ASTBuilder::pattern(ASTPatternID id) const noexcept -> const ASTPattern& {
    return storage.pattern_table.get(id);
}

auto ASTBuilder::block(ASTBlockID id) const noexcept -> const ASTBlock& {
    return storage.block_table.get(id);
}

auto ASTBuilder::branch_block(ASTBranchBlockID id) const noexcept -> const ASTBranchBlock& {
    return storage.branch_block_table.get(id);
}

auto ASTBuilder::checkpoint() const noexcept -> Checkpoint {
    return {
        .expressions = storage.expression_table.checkpoint(),
        .types = storage.type_table.checkpoint(),
        .statements = storage.statement_table.checkpoint(),
        .patterns = storage.pattern_table.checkpoint(),
        .blocks = storage.block_table.checkpoint(),
        .branch_blocks = storage.branch_block_table.checkpoint(),
        .items = storage.item_table.checkpoint(),
        .imports = storage.import_table.checkpoint(),
    };
}

auto ASTBuilder::rewind(Checkpoint checkpoint) noexcept -> void {
    storage.expression_table.rewind(checkpoint.expressions);
    storage.type_table.rewind(checkpoint.types);
    storage.statement_table.rewind(checkpoint.statements);
    storage.pattern_table.rewind(checkpoint.patterns);
    storage.block_table.rewind(checkpoint.blocks);
    storage.branch_block_table.rewind(checkpoint.branch_blocks);
    storage.item_table.rewind(checkpoint.items);
    storage.import_table.rewind(checkpoint.imports);
}

auto ASTBuilder::finish() && noexcept -> ASTStorage {
    return std::move(storage);
}
