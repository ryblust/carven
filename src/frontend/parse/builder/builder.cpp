module carven:frontend.parse.builder.impl;

import :frontend.parse.builder;
import :support.invariant;
import std;

ASTBuilder::ASTBuilder(SourceView source) noexcept
    : source_id(source.source_id),
      source_size(source.text.size()) {}

auto ASTBuilder::append_expression(ASTExpr value) noexcept -> ASTExprID {
    validate_topology(value);
    return storage.expression_table.add(std::move(value));
}

auto ASTBuilder::append_type(ASTType value) noexcept -> ASTTypeID {
    validate_topology(value);
    return storage.type_table.add(std::move(value));
}

auto ASTBuilder::append_statement(ASTStmt value) noexcept -> ASTStmtID {
    validate_topology(value);
    return storage.statement_table.add(std::move(value));
}

auto ASTBuilder::append_pattern(ASTPattern value) noexcept -> ASTPatternID {
    validate_topology(value);
    return storage.pattern_table.add(std::move(value));
}

auto ASTBuilder::append_block(ASTBlock value) noexcept -> ASTBlockID {
    validate_topology(value);
    return storage.block_table.add(std::move(value));
}

auto ASTBuilder::append_branch_block(ASTBranchBlock value) noexcept -> ASTBranchBlockID {
    validate_topology(value);
    return storage.branch_block_table.add(std::move(value));
}

auto ASTBuilder::append_item(ASTItem value) noexcept -> ASTItemID {
    validate_topology(value);
    return storage.item_table.add(std::move(value));
}

auto ASTBuilder::append_module_import(ASTModuleImport value) noexcept -> ASTModuleImportID {
    validate_topology(value);
    return storage.module_import_table.add(std::move(value));
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
        .module_imports = storage.module_import_table.checkpoint(),
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
    storage.module_import_table.rewind(checkpoint.module_imports);
}

auto ASTBuilder::finish(ASTModule ast_module) && noexcept -> SyntaxTree {
    validate(ast_module.span);
    for (const auto import_id : ast_module.module_imports) {
        validate(import_id);
    }
    for (const auto& header : ast_module.cpp_header_imports) {
        validate_topology(header);
    }
    for (const auto& fragment : ast_module.cpp_source_fragments) {
        validate_topology(fragment);
    }
    for (const auto item_id : ast_module.items) {
        validate(item_id);
    }
    return SyntaxTree(std::move(storage), std::move(ast_module), source_id);
}

auto ASTBuilder::validate(Span span) const noexcept -> void {
    if (span.end() > source_size) {
        invariant_violation("parser constructed a span outside its source snapshot");
    }
}

auto ASTBuilder::validate(ASTExprID id) const noexcept -> void {
    if (!storage.expression_table.contains(id)) {
        invariant_violation("parser expression parent references a child not yet constructed");
    }
}

auto ASTBuilder::validate(ASTTypeID id) const noexcept -> void {
    if (!storage.type_table.contains(id)) {
        invariant_violation("parser type parent references a child not yet constructed");
    }
}

auto ASTBuilder::validate(ASTStmtID id) const noexcept -> void {
    if (!storage.statement_table.contains(id)) {
        invariant_violation("parser statement parent references a child not yet constructed");
    }
}

auto ASTBuilder::validate(ASTPatternID id) const noexcept -> void {
    if (!storage.pattern_table.contains(id)) {
        invariant_violation("parser pattern parent references a child not yet constructed");
    }
}

auto ASTBuilder::validate(ASTBlockID id) const noexcept -> void {
    if (!storage.block_table.contains(id)) {
        invariant_violation("parser block parent references a child not yet constructed");
    }
}

auto ASTBuilder::validate(ASTBranchBlockID id) const noexcept -> void {
    if (!storage.branch_block_table.contains(id)) {
        invariant_violation("parser branch-block parent references a child not yet constructed");
    }
}

auto ASTBuilder::validate(ASTItemID id) const noexcept -> void {
    if (!storage.item_table.contains(id)) {
        invariant_violation("syntax root references an item not yet constructed");
    }
}

auto ASTBuilder::validate(ASTModuleImportID id) const noexcept -> void {
    if (!storage.module_import_table.contains(id)) {
        invariant_violation("syntax root references an import not yet constructed");
    }
}
